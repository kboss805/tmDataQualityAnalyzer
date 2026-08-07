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

Do **not** add a version literal anywhere else. qmake parses this header and generates
`version_autogen.h` for the Windows resource (`.rc`), and `deploy/build_release.ps1` /
`tmDataQualityAnalyzer.iss` read the same three constants at build time. If you find a hardcoded
version string elsewhere, that's a bug to remove, not a place to update.

## Step 3 — Update the developer changelog
In `docs/CLAUDE.md`, under **Version History**, add a new `### vX.Y.Z — <Title>` section at the top of
the list. Write tight, factual bullets describing what changed and which user story (US#) it serves —
match the voice of the existing entries (they reference concrete classes/methods). Also bump the
**Project Version** line near the top of that file.

## Step 4 — Update the user-facing "What's New"
Rewrite `deploy/RELEASENOTES.txt` for the new version — this is shown as the installer's "What's New"
page (`InfoBeforeFile`), so it is **end-user language**, not developer changelog. Lead with the
version header line (`tmDataQualityAnalyzer vX.Y.Z - What's New`) and group bullets as
New / Improved / Fixed. No class names or file paths — describe what the user can now do.

(The portable `deploy/README_portable.txt` rarely changes per release; update it only if install/run
instructions changed.)

### Also check every other artifact that SHIPS

These reach users but nothing in CI exercises them, so they rot silently:

- **`UserGuide.txt`** — copied into BOTH the installer and the portable ZIP. It sat eighteen months
  out of date describing a toolbar that no longer existed, shipping alongside an accurate in-app
  manual that said something different.
- **`resources/usermanual.html`** — Help > User Manual. Its subtitle carries a version string; it
  read "Version 2.7.0" at v2.9.0. Refresh screenshots with
  `powershell -File scriptsuild_manual.ps1` if the UI changed.
- **`README.md`** — the "developed on Qt x.y.z" line and the repo tree.
- **`CLAUDE.md`** — the "Current version" line in the opening paragraph. It sat at 2.7.0
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

**What the check cannot judge: whether the screenshots still show the truth.** Look at the figures
in `resources/usermanual.html` and ask whether anything in this release changes what they depict.
v2.9.1's headline fix removed the empty axis that four of the seven figures were displaying - a
manual illustrating the bug it just fixed. No script can catch that; it is a judgement call, so make
it deliberately every release rather than assuming the figures are fine.

## Step 5 — Verify before building
With the **build-and-test** skill: build the app clean (0 warnings) and run the **full** test suite
(all green, 0 failed/0 skipped). Do not package a release over a red or warning-laden build. Confirm
`git status` is clean except for the intended version/notes changes.

## Step 6 — Smoke-test signing BEFORE packaging

Packaging takes ~10 minutes and signs at the very end. **Prove the certificate can actually sign
first** — this costs 45 seconds and saved a whole cycle the one time it was skipped:

```powershell
$t = 'C:\path	ony.exe'   # copy a built exe to scratch first
$p = Start-Process signtool -ArgumentList @('sign','/sha1',$env:SIGN_CERT_SHA1,
     '/tr','http://timestamp.digicert.com','/td','sha256','/fd','sha256',$t) -NoNewWindow -PassThru
if ($p.WaitForExit(45000)) { "exit $($p.ExitCode)" } else { $p.Kill(); "HUNG - token locked" }
```

**The certificate being present in the store proves nothing.** It is token-backed (Certum
SimplySign): with the token locked it still shows as valid with `HasPrivateKey = True`. The failure
looks like either `SignTool Error: No certificates were found...` or signtool **hanging** on the PIN
prompt. If it hangs, the user must log in to SimplySign — you cannot.

## Step 7 — Build, sign, and package
Run the release script from the project root in **PowerShell**:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\build_release.ps1
```

It performs (and prints progress for) all of: qmake release build → windeployqt → code-sign the exe →
portable layout → portable ZIP → Inno Setup installer (also signed). It reads the version from
`constants.h` automatically, so there is no version argument to pass.

- **Signing:** uses the cert SHA-1 in `$env:SIGN_CERT_SHA1` (falls back to the bundled Certum OSS
  thumbprint). To build unsigned, pass `-SignCertSha1 ''`. Signing failures warn but don't abort —
  check the output if a signed artifact is required.
- **Inno Setup:** if `iscc` isn't found the installer step is skipped with a message; install Inno
  Setup 6 if you need the `_setup.exe`.
- Antivirus can briefly lock freshly-built exes; the ZIP step already retries — don't panic on a
  transient "file in use".

## Step 8 — Confirm the artifacts
The script prints the final paths. Expect, in `deploy/`:
- `tmDataQualityAnalyzer-vX.Y.Z_portable.zip`
- `tmDataQualityAnalyzer-vX.Y.Z_setup.exe`

**Verify by hand — do NOT trust the script's summary.** It once printed "Build and packaging
complete!" and listed an installer path that was never written, exiting 0, while signing had failed
for everything. It reports honestly now, but its trustworthiness is exactly what was broken, so
check independently:

```powershell
foreach ($t in @('deploy\staging\installerin	mDataQualityAnalyzer.exe',
                 "deploy\staging\portable	mDataQualityAnalyzer-v$v`_portable	mDataQualityAnalyzer.exe",
                 "deploy	mDataQualityAnalyzer-v$v`_setup.exe")) {
  '{0,-8} {1}' -f (Get-AuthenticodeSignature $t).Status, (Get-Item $t).VersionInfo.FileVersion
}
```

Then **extract the exe from the portable ZIP and check that too**. Signing runs BEFORE the ZIP and
the installer compile, so a failed signing still produces a ZIP — containing an unsigned exe — while
no installer exists at all. **The ZIP existing is not evidence the release is good**; that asymmetry
is how v2.8.0 nearly shipped unsigned.

Expect `Status = Valid` and the new version on every one. Report the paths, sizes and signature
status back to the user.

## Step 9 — Commit / tag (only if asked)
Don't commit, tag, or push unless the user asks. When they do: commit the `constants.h` + notes
changes together with a `feat: release vX.Y.Z` style message, and tag `vX.Y.Z` to match the existing
tag convention. The release artifacts in `deploy/` are build outputs — follow the repo's existing
practice on whether they're tracked.

## Guardrails
- One version source (`constants.h`). Never hand-edit `version_autogen.h`, the `.pro` VERSION, or the
  `.rc` — they're derived.
- Two notes files, two audiences: `docs/CLAUDE.md` history = developer; `RELEASENOTES.txt` = end user.
- A release must come from a green, zero-warning, full-suite-passing build.
