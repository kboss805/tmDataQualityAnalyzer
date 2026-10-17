# =============================================================================
# gen_vs_project.ps1 - generate Visual Studio projects for the app and its tests.
#
# Produces, in the repository root and under vs\ (all of it gitignored):
#
#   tmDataQualityAnalyzer.sln
#   vs\app\tmDataQualityAnalyzer.vcxproj          (+ .filters)
#   vs\tests\tmDataQualityAnalyzer_tests.vcxproj  (+ .filters)
#
# then open the .sln in Visual Studio 2026 (Community is fine) and press F5.
#
# WHY GENERATED AND NOT COMMITTED
#
# Same reason compile_flags.txt is: the project bakes in absolute paths to this checkout
# and to the Qt kit, so a committed copy would be wrong on every other machine. The .pro
# files stay the single source of truth for what gets built; this script is a view of them.
# Re-run it after changing a .pro (added or removed files), after a Qt bump, or after
# switching Visual Studio versions - the platform toolset is taken from whichever compiler
# scripts\env.ps1 selects.
#
# HOW IT WORKS
#
# `qmake -tp vc` writes a standalone .vcxproj: moc and rcc are ordinary custom build
# steps, so the Qt VS Tools extension is NOT required. Six things in its output are wrong
# for this project, and the script corrects them rather than shipping a project that
# builds but behaves differently from the qmake/jom build:
#
#  1. EXE LOCATION. The app finds its settings\ folder by walking up to two levels from
#     the exe (build\debug\ -> repo root), and the tests find their fixtures one level up
#     from theirs (tests\debug\ -> tests\data). So the exe MUST land in build\<cfg>\ and
#     tests\<cfg>\, exactly as under jom. Left alone qmake would put it beside the project
#     file, the app would not find its default settings, and every fixture test would skip.
#     Only the exe goes there: objects, moc and rcc output stay under vs\, so Visual
#     Studio and jom never share an intermediate file (the stale-object trap CLAUDE.md
#     documents is exactly what mixing two build systems in one directory would invite).
#
#  2. THE VENDORED C LIBRARY'S WARNINGS. thirdparty.pri silences lib\irig106 with
#     `QMAKE_CFLAGS += /w` so its pre-existing warnings do not bury real ones. The
#     generator drops that, and an unmodified project emitted four warnings, every one in
#     lib\irig106, which the project does not own and must not edit. They are switched
#     off per file here, mirroring thirdparty.pri exactly.
#
#  3. QT'S OWN WARNINGS. thirdparty.pri also passes /external:anglebrackets /external:W0 so
#     Qt 6.12's deprecation warning (qguiapplication.h, compressEvent - we never call it)
#     cannot fail the zero-warning gate. qmake has no property for those, so it writes them
#     as raw text, and Visual Studio's template emits its own /external:W3 as well: cl then
#     reports D9025 "overriding /external:W3 with /external:W0" on every build. They are
#     replaced here by MSBuild's real properties, so each is set exactly once.
#
#  4. PARALLEL PDB WRITES. The qmake/jom build compiles with -FS; the generated project does
#     not. Building the solution then compiles both projects at once and they race on the
#     compiler's PDB service, failing most solution builds with C1090 'PDB API call
#     failed, error code 3'. Each project built alone was fine. /FS is set as a property.
#
#  5. THE QT RUNTIME. A freshly built exe cannot start without Qt's DLLs beside it. Qt VS
#     Tools would solve that with a PATH entry; CLAUDE.md rules that out (a hard-coded Qt
#     path silently rots on the next QT_VERSION bump), so a post-build step runs
#     deploy_qt_local.ps1 instead, which reads the kit from env.ps1 every time.
#
#  6. THE SOLUTION. qmake does not write one for two separate .pro files, so this does.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\gen_vs_project.ps1
#
# Requires Windows PowerShell 5.1 (the same shell the release scripts run under).
# =============================================================================

$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root      = Split-Path -Parent $ScriptDir

# Puts qmake and the MSVC environment on PATH and tells us which Qt kit is in use.
. (Join-Path $ScriptDir 'env.ps1')

$VsDir     = Join-Path $Root 'vs'
$MsbuildNs = 'http://schemas.microsoft.com/developer/msbuild/2003'
$DeployPs1 = Join-Path $ScriptDir 'deploy_qt_local.ps1'

# {8BC9CEB8-...} is Visual Studio's project-type GUID for C++ projects.
$CppProjectType = '{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}'

# -----------------------------------------------------------------------------
# qmake -tp vc, run in its own directory, with stderr kept off the PowerShell error
# stream. qmake prints a "Could not parse Compiler option" warning for each flag the
# generator has no property for (/external:..., /await:strict); Windows PowerShell 5.1
# turns any native stderr into a NativeCommandError, which looked exactly like a failure
# ("exit -1") the first time this was tried, with a good project written all the while.
# The options are not lost - qmake appends them to AdditionalOptions, as it says.
# -----------------------------------------------------------------------------
function Invoke-QmakeVc {
    param([string]$ProFile, [string]$WorkDir, [string]$ExpectedProject)

    # Replace only what this script OWNS - the project files - and leave the directory
    # alone. It also holds the compiled objects, moc output and PDBs, and wiping those on
    # every run meant adding one source file cost a full 165 s rebuild. MSBuild tracks
    # each file's command line, so anything whose flags genuinely changed still rebuilds.
    # The expected project is removed first so a failed qmake cannot leave a stale one
    # behind that looks like it just succeeded.
    New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
    foreach ($stale in @($ExpectedProject, ($ExpectedProject + '.filters'), 'qmake.log')) {
        Remove-Item -Force -ErrorAction SilentlyContinue -LiteralPath (Join-Path $WorkDir $stale)
    }

    $log = Join-Path $WorkDir 'qmake.log'
    Push-Location $WorkDir
    try {
        $cmd = 'qmake -tp vc "{0}" -spec win32-msvc CONFIG+=suppress_vcproj_warnings > "{1}" 2>&1' -f $ProFile, $log
        & cmd /c $cmd
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }

    $proj = Join-Path $WorkDir $ExpectedProject
    if ($code -ne 0 -or -not (Test-Path -LiteralPath $proj)) {
        Write-Host "qmake -tp vc failed for $ProFile (exit $code). Output:" -ForegroundColor Red
        if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" }
        }
        throw "qmake did not produce $ExpectedProject"
    }
    return $proj
}

# -----------------------------------------------------------------------------
# Corrects the generated project (points 1-3 in the header).
# -----------------------------------------------------------------------------
function Update-VcxProj {
    param(
        [string]$Path,
        [string]$DebugOutDir,
        [string]$ReleaseOutDir,
        [bool]$IsTests
    )

    $doc = New-Object System.Xml.XmlDocument
    $doc.PreserveWhitespace = $true
    $doc.Load($Path)
    $nsm = New-Object System.Xml.XmlNamespaceManager($doc.NameTable)
    $nsm.AddNamespace('m', $MsbuildNs)

    # 1. Executable location. Only OutDir moves; IntDir stays inside vs\.
    $moved = 0
    foreach ($n in $doc.SelectNodes('//m:OutDir', $nsm)) {
        if ($n.GetAttribute('Condition') -match 'Debug\|x64') { $n.InnerText = $DebugOutDir } else { $n.InnerText = $ReleaseOutDir }
        $moved++
    }
    if ($moved -lt 2) { throw "expected an OutDir for each of Debug and Release in $Path, found $moved" }

    # 2. Mirror thirdparty.pri: no warnings from the vendored C library.
    $silenced = 0
    foreach ($n in $doc.SelectNodes('//m:ClCompile[@Include]', $nsm)) {
        if ($n.GetAttribute('Include') -like '*\lib\irig106\*') {
            if (-not $n.SelectSingleNode('m:WarningLevel', $nsm)) {
                $w = $doc.CreateElement('WarningLevel', $MsbuildNs)
                $w.InnerText = 'TurnOffAllWarnings'
                [void]$n.AppendChild($w)
            }
            $silenced++
        }
    }
    # A silent zero would mean the library moved and the suppression quietly stopped
    # applying - exactly the failure this script exists to prevent.
    if ($silenced -lt 1) { throw "no lib\irig106 sources found in $Path to silence" }

    # 2b. Qt's headers are third party too: thirdparty.pri passes /external:anglebrackets
    # /external:W0 so Qt 6.12's own C4996 (qguiapplication.h, compressEvent) cannot trip the
    # zero-warning gate. qmake has no property for those flags, so it drops them into
    # AdditionalOptions as raw text - and Visual Studio's template ALSO emits its own
    # /external:W3, so cl reports "D9025: overriding '/external:W3' with '/external:W0'"
    # on every build. MSBuild has first-class properties for both; use them and strip the
    # raw copies, so there is exactly one setting of each rather than two fighting.
    $external = 0
    foreach ($opts in $doc.SelectNodes('//m:ItemDefinitionGroup/m:ClCompile/m:AdditionalOptions', $nsm)) {
        $text = $opts.InnerText
        if ($text -notmatch '/external:W0') { continue }
        $opts.InnerText = ($text -replace '/external:anglebrackets', '') -replace '/external:W0', ''
        $clc = $opts.ParentNode
        if (-not $clc.SelectSingleNode('m:TreatAngleIncludeAsExternal', $nsm)) {
            $a = $doc.CreateElement('TreatAngleIncludeAsExternal', $MsbuildNs)
            $a.InnerText = 'true'
            [void]$clc.AppendChild($a)
        }
        if (-not $clc.SelectSingleNode('m:ExternalWarningLevel', $nsm)) {
            $b = $doc.CreateElement('ExternalWarningLevel', $MsbuildNs)
            $b.InnerText = 'TurnOffAllWarnings'
            [void]$clc.AppendChild($b)
        }
        $external++
    }
    # Same guard as above: zero would mean thirdparty.pri changed and Qt's warnings are
    # about to start failing the build in Visual Studio without anyone being told why.
    if ($external -lt 1) { throw "no /external:W0 found in $Path - did thirdparty.pri change?" }

    # 2c. /FS. The qmake/jom build passes -FS to every compile; the generated project does
    # not. Without it, building the solution compiles BOTH projects at once and they race
    # on the compiler's PDB service: most solution builds failed with
    #     error C1090: PDB API call failed, error code '3': ...\vc145.pdb
    # (that is the PDB library's EC_FILE_SYSTEM) - in the tests project, or in either. Each
    # project built alone was fine, which is what pointed at concurrency rather than at the
    # project. MSBuild's own description of this property is "Synchronous write to pdb when
    # building in parallel."
    $fs = 0
    foreach ($clc in $doc.SelectNodes('//m:ItemDefinitionGroup/m:ClCompile', $nsm)) {
        if (-not $clc.SelectSingleNode('m:ForceSynchronousPDBWrites', $nsm)) {
            $f = $doc.CreateElement('ForceSynchronousPDBWrites', $MsbuildNs)
            $f.InnerText = 'true'
            [void]$clc.AppendChild($f)
        }
        $fs++
    }
    if ($fs -lt 1) { throw "no ClCompile settings found in $Path to add /FS to" }

    # 3. Qt runtime beside the exe, after every successful link.
    $testsFlag = ''
    if ($IsTests) { $testsFlag = ' -Tests' }
    foreach ($group in $doc.SelectNodes('//m:ItemDefinitionGroup', $nsm)) {
        $cond = $group.GetAttribute('Condition')
        $cfg = ''
        if ($cond -match 'Debug\|x64')   { $cfg = 'debug' }
        if ($cond -match 'Release\|x64') { $cfg = 'release' }
        if ($cfg -eq '') { continue }

        $pb = $doc.CreateElement('PostBuildEvent', $MsbuildNs)
        $command = $doc.CreateElement('Command', $MsbuildNs)
        $command.InnerText = 'powershell -NoProfile -ExecutionPolicy Bypass -File "{0}" -Config {1}{2}' -f $DeployPs1, $cfg, $testsFlag
        $message = $doc.CreateElement('Message', $MsbuildNs)
        $message.InnerText = 'Deploying the Qt runtime beside the exe'
        [void]$pb.AppendChild($command)
        [void]$pb.AppendChild($message)
        [void]$group.AppendChild($pb)
    }

    $doc.Save($Path)
    return [pscustomobject]@{ OutDirsMoved = $moved; CFilesSilenced = $silenced; ExternalSet = $external }
}

function Get-ProjectGuid {
    param([string]$Path)
    $doc = New-Object System.Xml.XmlDocument
    $doc.Load($Path)
    $nsm = New-Object System.Xml.XmlNamespaceManager($doc.NameTable)
    $nsm.AddNamespace('m', $MsbuildNs)
    $g = $doc.SelectSingleNode('//m:ProjectGuid', $nsm)
    if (-not $g) { throw "no ProjectGuid in $Path" }
    return $g.InnerText.Trim()
}

# -----------------------------------------------------------------------------
# Generate both projects.
# -----------------------------------------------------------------------------
Write-Host ''
Write-Host '=== Generating Visual Studio projects ===' -ForegroundColor Cyan
Write-Host ('  compiler   ' + (Split-Path -Parent (Get-Command cl).Source))
Write-Host ('  Qt kit     ' + $env:QTDIR)

New-Item -ItemType Directory -Force -Path $VsDir | Out-Null

$appDir   = Join-Path $VsDir 'app'
$testsDir = Join-Path $VsDir 'tests'

$appProj = Invoke-QmakeVc -ProFile (Join-Path $Root 'tmDataQualityAnalyzer.pro') `
                          -WorkDir $appDir -ExpectedProject 'tmDataQualityAnalyzer.vcxproj'
$appFix = Update-VcxProj -Path $appProj `
                         -DebugOutDir   (Join-Path $Root 'build\debug\') `
                         -ReleaseOutDir (Join-Path $Root 'build\release\') `
                         -IsTests $false
Write-Host ('  app        {0}  (OutDir x{1}, {2} vendored C files silenced, /external x{3})' -f `
            $appProj.Substring($Root.Length + 1), $appFix.OutDirsMoved, $appFix.CFilesSilenced, $appFix.ExternalSet)

$testsProj = Invoke-QmakeVc -ProFile (Join-Path $Root 'tests\tests.pro') `
                            -WorkDir $testsDir -ExpectedProject 'tmDataQualityAnalyzer_tests.vcxproj'
$testsFix = Update-VcxProj -Path $testsProj `
                           -DebugOutDir   (Join-Path $Root 'tests\debug\') `
                           -ReleaseOutDir (Join-Path $Root 'tests\release\') `
                           -IsTests $true
Write-Host ('  tests      {0}  (OutDir x{1}, {2} vendored C files silenced, /external x{3})' -f `
            $testsProj.Substring($Root.Length + 1), $testsFix.OutDirsMoved, $testsFix.CFilesSilenced, $testsFix.ExternalSet)

# -----------------------------------------------------------------------------
# The solution. The app comes first, which makes it the startup project (F5).
# -----------------------------------------------------------------------------
$appGuid   = Get-ProjectGuid $appProj
$testsGuid = Get-ProjectGuid $testsProj

$nl = "`r`n"
$sln  = ''
$sln += 'Microsoft Visual Studio Solution File, Format Version 12.00' + $nl
$sln += '# Visual Studio Version 18' + $nl
$sln += 'VisualStudioVersion = 18.0.0.0' + $nl
$sln += 'MinimumVisualStudioVersion = 10.0.40219.1' + $nl
$sln += 'Project("{0}") = "tmDataQualityAnalyzer", "vs\app\tmDataQualityAnalyzer.vcxproj", "{1}"' -f $CppProjectType, $appGuid
$sln += $nl + 'EndProject' + $nl
$sln += 'Project("{0}") = "tmDataQualityAnalyzer_tests", "vs\tests\tmDataQualityAnalyzer_tests.vcxproj", "{1}"' -f $CppProjectType, $testsGuid
$sln += $nl + 'EndProject' + $nl
$sln += 'Global' + $nl
$sln += "`tGlobalSection(SolutionConfigurationPlatforms) = preSolution" + $nl
$sln += "`t`tDebug|x64 = Debug|x64" + $nl
$sln += "`t`tRelease|x64 = Release|x64" + $nl
$sln += "`tEndGlobalSection" + $nl
$sln += "`tGlobalSection(ProjectConfigurationPlatforms) = postSolution" + $nl
foreach ($g in @($appGuid, $testsGuid)) {
    foreach ($c in @('Debug', 'Release')) {
        $sln += "`t`t$g.$c|x64.ActiveCfg = $c|x64" + $nl
        $sln += "`t`t$g.$c|x64.Build.0 = $c|x64" + $nl
    }
}
$sln += "`tEndGlobalSection" + $nl
$sln += "`tGlobalSection(SolutionProperties) = preSolution" + $nl
$sln += "`t`tHideSolutionNode = FALSE" + $nl
$sln += "`tEndGlobalSection" + $nl
$sln += 'EndGlobal' + $nl

$slnPath = Join-Path $Root 'tmDataQualityAnalyzer.sln'
[System.IO.File]::WriteAllText($slnPath, $sln, (New-Object System.Text.UTF8Encoding($true)))

Write-Host ('  solution   tmDataQualityAnalyzer.sln  (startup project: the app)')
Write-Host ''
Write-Host 'Open tmDataQualityAnalyzer.sln in Visual Studio and press F5.' -ForegroundColor Green
Write-Host 'Re-run this script after changing a .pro file, bumping Qt, or switching VS versions.'
exit 0
