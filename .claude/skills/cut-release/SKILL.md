---
name: cut-release
description: >-
  Cut a new versioned release of tmDataQualityAnalyzer — bump the version, update the release notes
  and history, and build the signed installer + portable ZIP. Use this WHENEVER the user wants to
  "cut a release", "bump the version", "do a v2.x release", "build the installer", "ship a new
  version", or "update the release notes for the next version". The version is single-sourced in one
  struct and propagated by the build, the release notes live in two separate places with different
  audiences, and packaging runs a specific signing/Inno-Setup script — doing any of these by hand and
  out of order produces a mismatched or unsigned build. This skill encodes the exact, ordered process.
---

# Cut a Release — tmDataQualityAnalyzer

The version number has **one** source of truth and several derived consumers. Edit the source, let the
build derive the rest, update the human-facing notes, then package. Do the steps in order.

**From v3.0.0 the release is the Avalonia application** (`src/TmDq.App` over `tmdq_core.dll`), packaged by
`deploy\avalonia\build_avalonia_package.ps1`. The Qt application and its packaging (v2.x's) were deleted in plan step
6.1.

## Step 1 — Decide the version

Semantic-ish: patch for fixes/polish, minor for new user-facing features (a new US), major for big
shifts. Confirm the target version with the user if it isn't explicit. Current version lives in
`include/constants.h` (`AppVersion::kMajor/kMinor/kPatch`).

## Step 2 — Bump the single source of truth

Edit **only** `include/constants.h`:

```cpp
struct AppVersion {
    static constexpr int kMajor = 2;   // ← bump as decided
    static constexpr int kMinor = 6;
    static constexpr int kPatch = 0;
```

Do **not** add a version literal anywhere else. `eng/AppVersion.props` reads these three
constants for every .NET project (never type a version in a `.csproj`), `build_avalonia_package.ps1` reads
them for the package names and passes them to Inno Setup, and the manual build stamps them into the
manuals. If you find a hardcoded version string elsewhere, that's a bug to remove, not a place
to update. (Test fixtures written BY a version, such as the v2.14.2 templates, keep theirs: they record
who wrote them.)

## Step 3 — Update the developer changelog

In `docs/changelog.md`, turn the `### Unreleased` section at the top into `### vX.Y.Z — <Title>`, with a
short lead saying what the release is, and keep the entries under it. Add tight, factual bullets for
anything that landed without an entry, saying what changed and which user story (US#) it serves — match
the voice of the existing entries (they reference concrete classes/methods). Then bump the
**Project Version** line near the top of `docs/claude.md`.

## Step 4 — Update the user-facing "What's New"

Rewrite `deploy/RELEASENOTES.txt` for the new version — this is shown as the installer's "What's New"
page (`InfoBeforeFile`), so it is **end-user language**, not developer changelog. Lead with the
version header line (`tmDataQualityAnalyzer vX.Y.Z - What's New`) and group bullets as
New / Improved / Fixed. No class names or file paths — describe what the user can now do.

(The portable `deploy/README_portable.txt`, shipped in the ZIP as `README.txt`, rarely changes per release;
update it only if install/run instructions changed.)

### Also check every other artifact that SHIPS

These reach users but nothing in CI exercises them, so they rot silently:

- **The manuals** — `resources/usermanual.html` (Help > User Manual, compiled into the app) and
  `UserManual.html` (the full manual, shipped beside it). Their subtitle and footer carry the version from
  `AppVersion`, stamped by `powershell -ExecutionPolicy Bypass -File scripts\build_manual.ps1 -NoCapture`
  after the bump (drop `-NoCapture` to re-render the figures when the UI changed). The manual read
  "Version 2.7.0" at v2.9.0; the consistency check below is why that cannot recur.
- **`README.md`** — the system requirements, the build commands and the repo tree.
- **`.claude/CLAUDE.md`** — the "Current version" line in the opening paragraph. It sat at 2.7.0
  through two releases, because nothing points at it.

**Run the consistency check rather than grepping by hand:**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\check_release_consistency.ps1 -ReleaseMode
```

It derives the version from `AppVersion` and asserts every user-facing literal matches, that the
changelog has a `### vX.Y.Z` section, and (in `-ReleaseMode`) that no `Unreleased` section is left
behind. A **missing marker is a failure**, not a pass, so rewording one of those lines breaks the
check loudly instead of silently disabling it. `build_release.ps1` runs it before packaging and CI
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
(`scripts\test_native.ps1`, after `scripts\build_core_dll.ps1`), the managed library
(`scripts\test_dotnet.ps1`) and the app (`scripts\test_app.ps1`), all with zero warnings. After a change
to processing, the plot's series or the CSV writer, also run the soak (`scripts\run_soak.ps1`), which
holds the app to the bytes v2.14.2 exported. Then run the throughput guard:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_throughput_bench.ps1
```

It builds the native tests in **Release** (the benchmark is only meaningful against an optimised
build), runs the whole native suite, then runs the one benchmark case **pinned to the performance
cores** and prints a ratio block. It fails unless doctest reports that exactly that case ran and
passed.

**The benchmark is a native doctest case now**, "PipelineRunner throughput: ..." in
`native/tmdq_core_tests/test_pipelinerunner.cpp`; it measures `PipelineRunner`. It used to be the Qt
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

`build_avalonia_package.ps1` runs a **signing pre-flight as step 0** (`deploy\avalonia\signing.ps1`), before the build: it signs a
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
powershell -ExecutionPolicy Bypass -File deploy\avalonia\build_avalonia_package.ps1
```

It performs (and prints progress for) all of: the consistency check (`-ReleaseMode`) → the signing
pre-flight → `tmdq_core.dll` in Release → a self-contained **single-file** publish → the manifest check
(UTF-8 code page) → staging (settings, the VC++ runtime the DLL imports, the licence, the generated
`THIRD-PARTY-NOTICES.txt`) → signing **our two binaries only** (the exe and `tmdq_core.dll`; it refuses to
sign anything else it finds unsigned) → verifying every PE file's signature → a smoke run of the staged
app on the PRN sample → the portable ZIP and the Inno installer (`deploy\avalonia\tmDataQualityAnalyzer.iss`,
also signed). It reads the version from `constants.h`, so there is no version argument.

- **Signing:** uses the cert SHA-1 in `$env:SIGN_CERT_SHA1` (falls back to the bundled Certum OSS
  thumbprint). To build unsigned, pass `-SignCertSha1 ''`. Any signing failure is a listed problem and
  a non-zero exit.
- **The installer replaces a v2.x install** (the Qt installer's AppId and folder): it deletes the Qt
  `bin\`, `UserGuide.txt` and the old `UserManual.html`, keeps `settings\`, and moves the `.ch10`
  association. Preferences carry over by the app's own first-run import of the Qt INI file.
- **Inno Setup:** if `iscc` isn't found no installer is produced, and the run says so and fails.
- Antivirus can briefly lock freshly-built exes; the ZIP step already retries — don't panic on a
  transient "file in use".

## Step 8 — Confirm the artifacts

The script prints the final paths. Expect, in `deploy/avalonia/`:

- `tmDataQualityAnalyzer-vX.Y.Z_portable.zip`
- `tmDataQualityAnalyzer-vX.Y.Z_setup.exe`

**Verify by hand — do NOT trust the script's summary.** It once printed "Build and packaging
complete!" and listed an installer path that was never written, exiting 0, while signing had failed
for everything. It reports honestly now, but its trustworthiness is exactly what was broken, so
check independently:

```powershell
foreach ($t in @('deploy\avalonia\staging\app\tmDataQualityAnalyzer.exe',
                 'deploy\avalonia\staging\app\tmdq_core.dll',
                 "deploy\avalonia\staging\tmDataQualityAnalyzer-v$v`_portable\tmDataQualityAnalyzer.exe",
                 "deploy\avalonia\tmDataQualityAnalyzer-v$v`_setup.exe")) {
  '{0,-8} {1}' -f (Get-AuthenticodeSignature $t).Status, (Get-Item $t).VersionInfo.FileVersion
}
```

An Inno `setup.exe` carries its version in `ProductVersion`, not `FileVersion`.

Then **extract the exe from the portable ZIP and check that too**. Signing runs BEFORE the ZIP and
the installer compile, so a failed signing still produces a ZIP — containing an unsigned exe — while
no installer exists at all. **The ZIP existing is not evidence the release is good**; that asymmetry
is how v2.8.0 nearly shipped unsigned.

Expect `Status = Valid` and the new version on every one. Report the paths, sizes and signature
status back to the user.

## Step 9 - Install it on a clean machine

Everything up to here ran on a machine that already holds this application, its settings
and its registry state, so it can only prove the artifacts are correctly
signed and versioned. Whether the installer **works somewhere new** is a different claim,
and v2.13.1 could not make it: that needed "a genuinely clean Windows target; Windows 11
Home offers neither Sandbox nor Hyper-V."

On Windows Pro/Enterprise with the **Windows Sandbox** feature enabled (one line; semicolons separate
the payload, because the recordings' names contain commas):

```powershell
powershell -ExecutionPolicy Bypass -File deploy\sandbox\run_sandbox_test.ps1 -Verifier verify_avalonia_install.ps1 -Setup "deploy\avalonia\tmDataQualityAnalyzer-v$v`_setup.exe" -Payload "deploy\avalonia\tmDataQualityAnalyzer-v$v`_portable.zip;deploy\avalonia\staging\unsigned;tests\data\test files\prn_testfile, sample.ch10;deploy\tmDataQualityAnalyzer-v2.14.2_setup.exe"
```

It generates the `.wsb` (absolute host paths, which is why it is generated rather than committed),
launches a disposable VM that has never seen this application, and prints the results back here.
Exits non-zero on failure. `deploy\tmDataQualityAnalyzer-v2.14.2_setup.exe` is the last Qt release
(the GitHub release's asset, if it is not on disk).

What it establishes that nothing else can:

- **Smart App Control** is on, and **an unsigned build of the same exe is blocked**: without that
  negative control a pass proves nothing about the signatures.
- **The upgrade from v2.x:** v2.14.2 installed with its default tasks, a settings file edited and the
  Qt INI written, then this setup over it: the Qt `bin\` and text guide gone, the full manual this
  version's, the edited settings file kept, `.ch10` opening the new exe, one entry in Installed apps,
  and the first run importing the Qt preferences.
- **The DLL closure:** the installed app and the portable ZIP open their windows and process the PRN
  sample end to end; `tmdq_core.dll` loads with the VC++ runtime beside it; every PE file verifies.
- **Uninstall**: program files and the ProgID go, `settings\` stays (`uninsneveruninstall`,
  US8.0), the `.ch10` value is cleared while its now-empty key is left behind on purpose.

**Two US8.0 criteria the Sandbox cannot check** (it installs silently and elevated): on the release
build, a user **without admin rights** installs to a directory of their choosing ("Install for me
only"), and the installer **shows its progress**. Ask the user to run the setup by hand for these.

**A red result is not automatically a product defect.** The first run of this harness
reported three failures and every one was the harness: a Windows system DLL caught by a
loose name filter, a registry KEY checked where the VALUE is the contract, and a check for
a setting that landed after the build under test. Confirm the check before chasing the code.

**Do not force-kill the Sandbox window.** It leaves the container half torn down and the
next launch fails silently with no error; close it normally. The launcher refuses to start
while one is running, for that reason.

## Step 10 - Commit / tag (only if asked)

Don't commit, tag, or push unless the user asks. When they do: commit the `constants.h` + notes
changes together with a `feat: release vX.Y.Z` style message, and tag `vX.Y.Z` (annotated) to match the
existing tag convention. The GitHub release is titled `vX.Y.Z`, carries the setup.exe and the portable
ZIP, and its body is the top section of `RELEASENOTES.txt`. Publishing it is outward-facing: confirm
with the user first. The release artifacts are build outputs and are not tracked.

## Guardrails

- One version source (`constants.h`). Never hand-edit `version_autogen.h`, the `.pro` VERSION, or the
  `.rc` — they're derived.
- Two notes files, two audiences: `docs/changelog.md` = developer; `RELEASENOTES.txt` = end user.
- A release must come from a green, zero-warning, full-suite-passing build.
