---
name: cut-release
description: >-
  Cut a new versioned release of tmDataQualityAnalyzer — bump the version, update the release notes
  and history, and build the signed MSI + portable ZIP. Use this WHENEVER the user wants to
  "cut a release", "bump the version", "do a v3.x release", "build the installer", "ship a new
  version", or "update the release notes for the next version". The version is single-sourced in one
  struct and propagated by the build, the release notes live in two separate places with different
  audiences, and packaging runs a specific signing/WiX script — doing any of these by hand and
  out of order produces a mismatched or unsigned build. This skill encodes the exact, ordered process.
---

# Cut a Release — tmDataQualityAnalyzer

The version number has **one** source of truth and several derived consumers. Edit the source, let the
build derive the rest, update the human-facing notes, then package. Do the steps in order.

**From v3.0.0 the release is the Avalonia application** (`src/TmDq.App` over `tmdq_core.dll`), packaged by
`deploy\build_package.ps1`. The v2.x application and its packaging were deleted in plan step
6.1.

## Step 1 — Decide the version

Semantic-ish: patch for fixes/polish, minor for new user-facing features (a new US), major for big
shifts. Confirm the target version with the user if it isn't explicit. Current version lives in
`src/core/include/constants.h` (`AppVersion::kMajor/kMinor/kPatch`).

## Step 2 — Bump the single source of truth

Edit **only** `src/core/include/constants.h`:

```cpp
struct AppVersion {
    static constexpr int kMajor = N;   // ← bump as decided: the three numbers of the new version
    static constexpr int kMinor = N;
    static constexpr int kPatch = N;
```

Do **not** add a version literal anywhere else. `eng/AppVersion.props` reads these three
constants for every .NET project (never type a version in a `.csproj`), `build_package.ps1` reads
them for the package names and passes them to `build_msi.ps1` (an MSI version is numeric, X.Y.Z), and the manual build stamps them into the
manuals. If you find a hardcoded version string elsewhere, that's a bug to remove, not a place
to update. (Test fixtures written BY a version, such as the v2.14.2 templates, keep theirs: they record
who wrote them.)

## Step 3 — Update the developer changelog

In `docs/changelog.md`, turn the `### Unreleased` section at the top into `### vX.Y.Z — <Title>`, with a
short lead saying what the release is, and keep the entries under it. Add tight, factual bullets for
anything that landed without an entry, saying what changed and which user story (US#) it serves — match
the voice of the existing entries (they reference concrete classes/methods). Then bump the
**Project Version** line near the top of `docs/guide.md`.

## Step 4 — Update the user-facing "What's New"

Rewrite `deploy/RELEASENOTES.txt` for the new version — this is shown as the installer's "What's New"
page (`InfoBeforeFile`), so it is **end-user language**, not developer changelog. Lead with the
version header line (`tmDataQualityAnalyzer vX.Y.Z - What's New`) and group bullets as
New / Improved / Fixed. No class names or file paths — describe what the user can now do.

(The portable `deploy/README_portable.txt`, shipped in the ZIP as `README.txt`, rarely changes per release;
update it only if install/run instructions changed.)

### Also check every other artifact that SHIPS

These reach users but nothing in CI exercises them, so they rot silently:

- **The manuals** — `assets/usermanual.html` (Help > User Manual, compiled into the app) and
  `UserManual.html` (the full manual, shipped beside it: generated and untracked, `deploy\build_package.ps1` makes it from the
  base manual and the committed figures and stops if that changes the base). Their subtitle and footer carry the version from
  `AppVersion`, stamped by a full `powershell -ExecutionPolicy Bypass -File eng\build_manual.ps1` after the bump.
  **Never `-NoCapture` after a bump**: four figures show the log's version line, so they must be re-rendered, and
  `docs\manual_images\rendered-version.txt` must name the release's version or the consistency check fails (v3.3.0
  shipped four figures that said v3.2.0 for this reason). `-NoCapture` is for a prose-only change between releases.
  The manual read "Version 2.7.0" at v2.9.0; the consistency check below is why that cannot recur.
- **`README.md`** — the system requirements, the build commands and the repo tree.
- **`.claude/CLAUDE.md`** — the "Current version" line in the opening paragraph. It sat at 2.7.0
  through two releases, because nothing points at it.

**Run the consistency check rather than grepping by hand:**

```powershell
powershell -ExecutionPolicy Bypass -File eng\check_release_consistency.ps1 -ReleaseMode
```

It derives the version from `AppVersion` and asserts every user-facing literal matches, that the
changelog has a `### vX.Y.Z` section, and (in `-ReleaseMode`) that no `Unreleased` section is left
behind. A **missing marker is a failure**, not a pass, so rewording one of those lines breaks the
check loudly instead of silently disabling it. `build_package.ps1` runs it before packaging and CI
runs it without `-ReleaseMode` on every PR, so drift is caught long before a release.

**What the check cannot judge: whether the screenshots still show the truth.** The figures are rendered
from the application by `ManualFiguresTests`, which checks each caption's claims, but a caption cannot say
everything a picture shows. Look at the figures in the manuals and ask whether anything in this release
changes what they depict.
v2.9.1's headline fix removed the empty axis that four of the seven figures were displaying - a
manual illustrating the bug it just fixed. No script can catch that; it is a judgement call, so make
it deliberately every release rather than assuming the figures are fine.

## Step 5 — Verify before building

Run every suite, each against the baseline in `.claude/CLAUDE.md`: the native core
(`eng\test_native.ps1`, after `eng\build_core_dll.ps1`), the managed library
(`eng\test_dotnet.ps1`) and the app (`eng\test_app.ps1`), all with zero warnings. After a change
to processing, the plot's series or the CSV writer, also run the soak (`eng\run_soak.ps1`), which
holds the app to the bytes v2.14.2 exported. Then run the throughput guard:

```powershell
powershell -ExecutionPolicy Bypass -File eng\run_throughput_bench.ps1
```

It builds the native tests in **Release** (the benchmark is only meaningful against an optimised
build), runs the whole native suite, then runs the one benchmark case **pinned to the performance
cores** and prints a ratio block. It fails unless doctest reports that exactly that case ran and
passed.

**The benchmark is a native doctest case now**, "PipelineRunner throughput: ..." in
`tests/core/test_pipelinerunner.cpp`; it measures `PipelineRunner`. It used to be the v2.x
suite's one standing skip — opt-in behind `TMDQA_RUN_HEAVY_BENCH`, nothing set that variable, and
*it asserted nothing even when it ran*, so the decommutation pipeline had no performance guard at
all and the suite's "413/0/1" baseline was that hole. The script sets the variable, the benchmark
asserts its ratios, and once asked for it cannot skip: a recording that cannot satisfy it is a
failure.

It needs `tests/data/test files/prn_testfile, full.ch10` (~640 MB, gitignored). The committed
sample is **not** a substitute — it carries PCM data for 2 of the 4 channels the benchmark
times — and the script says so up front instead of letting the benchmark fail late.

CI leaves the benchmark off (the case is skipped there).

Do not package a release over a red or warning-laden build. Confirm `git status` is clean
except for the intended version/notes changes.

## Step 6 — Signing is proven automatically (nothing to do by hand)

`build_package.ps1` runs a **signing pre-flight as step 0** (`deploy\signing.ps1`), before the build: it signs a
throwaway copy under a timeout and aborts if the certificate cannot produce a signature. This
used to be a manual ritual here, and skipping it once cost a whole packaging cycle.

What that protects against, and why it needs an actual signature rather than a store lookup:

- **The certificate being present proves nothing.** It is token-backed (Certum SimplySign) and
  with the token locked it still reports as valid with `HasPrivateKey = True`.
- The two failure shapes are `SignTool Error: No certificates were found...` and signtool
  **hanging** on the PIN prompt. Both now fail in the first seconds with a message naming the
  cause, rather than at step 5 of 8.
- **If it reports a locked token, the user must log in to SimplySign — you cannot.**

`signtool` is discovered from the newest installed Windows SDK rather than a pinned path, so an
SDK update no longer silently demotes it to the bare name on `PATH`.

To build unsigned, pass `-SignCertSha1 ''`; the pre-flight is skipped with a note.

## Step 7 — Build, sign, and package

Run the release script from the project root under **Windows PowerShell 5.1** (`powershell`, not `pwsh`):

```powershell
powershell -ExecutionPolicy Bypass -File deploy\build_package.ps1
```

It performs (and prints progress for) all of: the consistency check (`-ReleaseMode`) → the signing
pre-flight → `tmdq_core.dll` in Release, checked (`eng\check_release_core.ps1`: it must not import the debug
C runtime, which is what a Debug core looks like; checked again on the staged copy) → a self-contained
**single-file** publish → the manifest check
(UTF-8 code page) → staging (settings, the VC++ runtime the DLL imports, the licence, the generated
`THIRD-PARTY-NOTICES.txt`) → signing **our two binaries only** (the exe and `tmdq_core.dll`; it refuses to
sign anything else it finds unsigned) → verifying every PE file's signature → a smoke run of the staged
app on the PRN sample → the portable ZIP and the MSI (`deploy\tmDataQualityAnalyzer.wxs`, built by
`deploy\build_msi.ps1` and signed). It reads the version from `constants.h`, so there is no version argument.

- **Signing:** uses the cert SHA-1 in `$env:SIGN_CERT_SHA1` (falls back to the bundled Certum OSS
  thumbprint). To build unsigned, pass `-SignCertSha1 ''`. Any signing failure is a listed problem and
  a non-zero exit.
- **The MSI is per-machine only** (a user without admin rights takes the portable ZIP). It does **not** upgrade a copy
  that an Inno Setup installer put there (every release before the MSI): it refuses, says to uninstall that one first
  (Settings, Apps), and the settings and preferences are kept. Say so in the release notes of the first MSI release. An
  MSI over an older MSI is an upgrade that keeps `settings\` (never overwritten, never removed); the older MSI over a
  newer one is refused. An administrator's switches are public properties (`ASSOCIATE_CH10=0`, `FULLMANUAL=0`,
  `DESKTOPSHORTCUT=1`, `INSTALLFOLDER=...`): they are in the header of the `.wxs` and in `docs/guide.md`.
- **WiX:** a repo-local dotnet tool pinned in `dotnet-tools.json` (6.0.2), with its UI extension fetched into the
  gitignored `.wix\` cache the first time. A failed restore or extension fetch fails the MSI step, which the run lists.
- Antivirus can briefly lock freshly-built exes; the ZIP step already retries — don't panic on a
  transient "file in use".

## Step 8 — Confirm the artifacts

The script prints the final paths. Expect, in `deploy/`:

- `tmDataQualityAnalyzer-vX.Y.Z_portable.zip`
- `tmDataQualityAnalyzer-vX.Y.Z_setup.msi`

**Verify by hand — do NOT trust the script's summary.** It once printed "Build and packaging
complete!" and listed an installer path that was never written, exiting 0, while signing had failed
for everything. It reports honestly now, but its trustworthiness is exactly what was broken, so
check independently:

```powershell
foreach ($t in @('deploy\staging\app\tmDataQualityAnalyzer.exe',
                 'deploy\staging\app\tmdq_core.dll',
                 "deploy\staging\tmDataQualityAnalyzer-v$v`_portable\tmDataQualityAnalyzer.exe",
                 "deploy\tmDataQualityAnalyzer-v$v`_setup.msi")) {
  '{0,-8} {1}' -f (Get-AuthenticodeSignature $t).Status, (Get-Item $t).VersionInfo.FileVersion
}
```

An MSI has no file-version resource, so its line shows a signature status and no version: its version is in its name
and in its ProductVersion property (the Installed apps entry shows it after an install, which the Sandbox test in the next
step reads and compares).

Then **extract the exe from the portable ZIP and check that too**. Signing runs BEFORE the ZIP and
the MSI build, so a failed signing still produces a ZIP — containing an unsigned exe — while
no MSI exists at all. **The ZIP existing is not evidence the release is good**; that asymmetry
is how v2.8.0 nearly shipped unsigned.

Expect `Status = Valid` and the new version on every one. Report the paths, sizes and signature
status back to the user.

## Step 9 - Install it on a clean machine

Everything up to here ran on a machine that already holds this application, its settings
and its registry state, so it can only prove the artifacts are correctly
signed and versioned. Whether the installer **works somewhere new** is a different claim,
and v2.13.1 could not make it: that needed "a genuinely clean Windows target; Windows 11
Home offers neither Sandbox nor Hyper-V."

On Windows Pro/Enterprise with the **Windows Sandbox** feature enabled. The payload the verifier reads defaults to the
portable ZIP beside the MSI, `deploy\staging\unsigned`, the committed PRN sample, v2.14.2's setup (the
main checkout's `deploy\` when this is a worktree) and a NEWER MSI of the same product, which the runner builds
itself (unsigned, the next patch version, from the staged files) for the upgrade; `-Payload` overrides it,
separated by semicolons because the recordings' names contain commas:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\sandbox\run_sandbox_test.ps1 -Setup "deploy\tmDataQualityAnalyzer-v$v`_setup.msi"
```

It generates the `.wsb` (absolute host paths, which is why it is generated rather than committed),
launches a disposable VM that has never seen this application, and prints the results back here.
Exits non-zero on failure. It takes **15 to 20 minutes**: in the Sandbox every `msiexec /i` of the 50 MB MSI spends
about two minutes before it starts (the same package does not, on a development machine), and the test installs
it five times; `progress.txt` beside `results.txt` (under `deploy\sandbox\.work\out`) says how far it has got.
`deploy\tmDataQualityAnalyzer-v2.14.2_setup.exe` is the last v2.x release (the GitHub release's asset, if it is
not on disk): the MSI's refusal to install beside an Inno install needs a real one.

What it establishes that nothing else can:

- **Smart App Control** is on, and **an unsigned build of the same exe is blocked**: without that
  negative control a pass proves nothing about the signatures.
- **The refusal beside an Inno install:** v2.14.2 installed, the MSI stops with exit 1603 and the message that says to
  uninstall that one first, and installs nothing.
- **Install, upgrade, uninstall:** every file arrives, one entry in Installed apps at the MSI's version, `.ch10`
  opening the installed exe, a Start menu entry and no desktop shortcut; the newer MSI over it replaces the version and
  keeps an edited settings file; the older MSI over the newer is refused; an uninstall removes the program and the
  association and leaves `settings\`, the edit included.
- **The switches:** `ASSOCIATE_CH10=0 FULLMANUAL=0 DESKTOPSHORTCUT=1` give no association, no full manual and a
  desktop shortcut.
- **The DLL closure:** the installed app and the portable ZIP open their windows and process the PRN
  sample end to end; `tmdq_core.dll` loads with the VC++ runtime beside it; every PE file verifies.

**What the Sandbox cannot check** (it drives `msiexec` silently and elevated): the **interactive wizard**, and
**US8.0's criteria about the installer** (see the note in `docs/guide.md`: the MSI is per-machine only, so "Install for me
only" is the portable ZIP's). Ask the user to click through the wizard once on the release build: the folder, the
feature list, the progress and the finish page's launch box.

**A red result is not automatically a product defect.** The first run of this harness
reported three failures and every one was the harness: a Windows system DLL caught by a
loose name filter, a registry KEY checked where the VALUE is the contract, and a check for
a setting that landed after the build under test. Confirm the check before chasing the code.

**Do not force-kill the Sandbox window.** It leaves the container half torn down and the
next launch fails silently with no error; close it normally, then **wait until `WindowsSandboxServer` has
gone** (a few seconds: the process of the closed window outlives it). The launcher refuses to start
while one is running, for that reason.

## Step 10 - Commit / tag (only if asked)

Don't commit, tag, or push unless the user asks. When they do: commit the `constants.h` + notes
changes together with a `feat: release vX.Y.Z` style message, and tag `vX.Y.Z` (annotated) to match the
existing tag convention. The GitHub release is titled `vX.Y.Z`, carries the setup.msi, the portable
ZIP and the symbols ZIP (`tmDataQualityAnalyzer-vX.Y.Z_symbols.zip`, which `build_package.ps1` makes: not shipped
to users, but the only thing that turns a crash report's frames, or a dump, of that version into source lines, so
attach it every time), and its body is the top section of `RELEASENOTES.txt`. Publishing it is outward-facing:
confirm with the user first. The release artifacts are build outputs and are not tracked.

## Guardrails

- One version source (`constants.h`). Never type a version into a `.csproj`, the WiX source or a manual:
  `eng/AppVersion.props`, the package script (which hands it to `build_msi.ps1`) and `eng\build_manual.ps1` derive it.
- Two notes files, two audiences: `docs/changelog.md` = developer; `RELEASENOTES.txt` = end user.
- A release must come from a green, zero-warning, full-suite-passing build.
