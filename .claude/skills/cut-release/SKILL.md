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

## Step 5 — Verify before building
With the **build-and-test** skill: build the app clean (0 warnings) and run the **full** test suite
(all green, 0 failed/0 skipped). Do not package a release over a red or warning-laden build. Confirm
`git status` is clean except for the intended version/notes changes.

## Step 6 — Build, sign, and package
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

## Step 7 — Confirm the artifacts
The script prints the final paths. Expect, in `deploy/`:
- `tmDataQualityAnalyzer-vX.Y.Z_portable.zip`
- `tmDataQualityAnalyzer-vX.Y.Z_setup.exe`

Verify both exist and carry the new version in their names, and that the exe version resource matches
(right-click → Properties → Details, or trust the single-source derivation). Report the paths and
sizes back to the user.

## Step 8 — Commit / tag (only if asked)
Don't commit, tag, or push unless the user asks. When they do: commit the `constants.h` + notes
changes together with a `feat: release vX.Y.Z` style message, and tag `vX.Y.Z` to match the existing
tag convention. The release artifacts in `deploy/` are build outputs — follow the repo's existing
practice on whether they're tracked.

## Guardrails
- One version source (`constants.h`). Never hand-edit `version_autogen.h`, the `.pro` VERSION, or the
  `.rc` — they're derived.
- Two notes files, two audiences: `docs/CLAUDE.md` history = developer; `RELEASENOTES.txt` = end user.
- A release must come from a green, zero-warning, full-suite-passing build.
