# CI — the self-hosted Windows runner

Operational notes for the machine that runs CI. The **workflow** side is documented
in the header comment of [`.github/workflows/ci.yml`](../.github/workflows/ci.yml)
(runner routing, the ASCII/PowerShell-5.1 constraints, the fixture overlay); this
file covers the **box**: how the runner is installed, what it needs on disk, and how
to bring it back when it stops picking up jobs.

CI is not hard-dependent on this machine. If the runner is offline — or a
contributor pushes without access to it — the `pick-runner` job routes to the
GitHub-hosted `windows-latest` fallback and the suite still runs in full. Losing the
box slows CI down; it does not block merges.

## The runner at a glance

| | |
| --- | --- |
| Host | kevin's dev machine |
| Install root | `C:\actions-runner` (actions/runner v2.335.1, win-x64) |
| Runner name | `tmdqa-selfhosted-1` |
| Labels | `self-hosted`, `Windows`, `X64`, **`tmdqa`** |
| Service | `actions.runner.kboss805-tmDataQualityAnalyzer.tmdqa-selfhosted-1` |
| Service account | `NT AUTHORITY\NETWORK SERVICE`, StartType `Automatic` |

The workflow targets `runs-on: [self-hosted, windows, tmdqa]`, so the **`tmdqa`
label is load-bearing** — a runner registered without it will never be selected.

### Security gate

Self-hosted runners on a **public** repo are a machine-takeover risk: a fork PR can
execute arbitrary code on the box with the runner's privileges, and the runner is
persistent, not ephemeral. This repo is **private with 0 forks**, which is why
self-hosting is acceptable here. **If the repo is ever made public, take the runner
offline first** and either restrict the self-hosted job to `push`/`workflow_dispatch`
on `main` or require approval for all outside-collaborator workflow runs.

## What the box provides

1. **The toolchain**, pre-installed rather than provisioned per run: MSVC 2022 and
   the Qt `msvc2022_64` kit matching `QT_VERSION` in `ci.yml`. Every workflow step
   that runs a Qt or MSVC binary dot-sources `. .\scripts\env.ps1` — each step is a
   fresh shell, so PATH set in one step does **not** carry into the next. A step that
   skips it fails in about a second with a missing-DLL error.
1. **`jom`** — installed at `C:\Qt\Tools\jom\jom.exe`. It is the parallel drop-in for
   `nmake`, which has no `-j` and so builds on a single core. `env.ps1` probes
   `<Qt>\Tools\jom\jom.exe` and `<Qt>\Tools\QtCreator\bin\jom\jom.exe`, honours
   `TMDQ_JOM` as an explicit override, and exports **`TMDQ_MAKE`** as either `jom` or
   `nmake`; every build invokes `$env:TMDQ_MAKE`. It is purely an accelerator — a box
   without `jom` builds identically, just slower, which is what the GitHub-hosted
   fallback runner does.

   It cut a full run from **286 s to 93 s (-67%)** on this 28-core box: the app build
   88 s → 25 s, the test build 157 s → 23 s. Each build step logs which tool it used,
   so a regression to `nmake` is visible in the log rather than silent.

   Because the runner is a **service running as NETWORK SERVICE**, `jom` must live
   where that account can read it — the `C:\Qt` tree is fine (its ACL grants
   `Authenticated Users`, which covers service accounts), a user profile is not. No
   service restart is needed after installing: detection probes the filesystem at
   build time rather than relying on the service's cached `PATH`.
2. **The full-size PRN recording.** `C:\actions-runner\.env` sets:

   ```
   TMDQA_CH10_FULL=C:\ProgramData\tmdqa-ci-fixtures\prn_testfile_full.ch10
   ```

   a 640 MB copy of the complete recording. The "Overlay full-size PRN fixture" step
   copies it over the committed sample so the heavy throughput benchmark has real
   data to walk. It lives in `ProgramData`, **not** in a user profile, because
   `NETWORK SERVICE` cannot read `C:\Users\kevin\...`; from there the overlay silently
   no-ops and the benchmark falls back to the small sample. It also lives outside the
   checkout so CI never depends on the dev tree's state.

   If the variable is unset or the path is missing, CI emits a warning and continues
   on the committed sample — the run is still green.

## Service mode: what differs from an interactive shell

The service runs as `NETWORK SERVICE` in **Session 0**, with no desktop and no user
profile. Four consequences are already handled in `ci.yml`; they are recorded here
because each one looks like a code failure at first glance:

- **`pwsh` does not exist.** PowerShell 7 is installed as a per-user MSIX/Store
  package, which a service account cannot launch. All Windows steps use
  `shell: powershell` (5.1, in System32). `winget --scope machine` cannot fix this —
  the package is MSIX-only.
- **PowerShell 5.1 decodes step scripts as Windows-1252, not UTF-8.** A UTF-8 dash
  becomes bytes containing a smart quote, which 5.1 reads as a string delimiter:
  *"The string is missing the terminator"*. **`ci.yml` must stay pure ASCII.**
- **5.1 turns native-tool stderr into a terminating error** under GitHub's default
  `$ErrorActionPreference='stop'` — `cl` prints its banner to stderr. Every Windows
  step sets `$ErrorActionPreference = 'Continue'` and gates failure on explicit
  `$LASTEXITCODE` checks instead.
- **No interactive desktop**, so widgets cannot expose windows. Test steps set
  `QT_QPA_PLATFORM: offscreen` in both jobs.

## Operating the runner

Check whether it is up (this is also exactly what `pick-runner` asks GitHub):

```bash
gh api repos/kboss805/tmDataQualityAnalyzer/actions/runners -q '.runners[] | "\(.name) \(.status)"'
```

Restart the service (elevated shell):

```bash
Restart-Service actions.runner.kboss805-tmDataQualityAnalyzer.tmdqa-selfhosted-1
```

### Reinstalling or re-registering

**There is no `svc.cmd` on Windows** — that script is Linux/macOS only. The service
is installed by re-running `config.cmd` with `--runasservice`, and `--replace` alone
is not sufficient: the local configuration must be removed first. From an **elevated**
shell in `C:\actions-runner` (the agent cannot elevate, so this is a human step):

```bash
$remove = gh api -X POST repos/kboss805/tmDataQualityAnalyzer/actions/runners/remove-token -q .token
.\config.cmd remove --token $remove
$reg = gh api -X POST repos/kboss805/tmDataQualityAnalyzer/actions/runners/registration-token -q .token
.\config.cmd --url https://github.com/kboss805/tmDataQualityAnalyzer --token $reg --name tmdqa-selfhosted-1 --labels tmdqa --work _work --unattended --runasservice
```

`.env` has survived a reconfigure in practice, but **verify `TMDQA_CH10_FULL` is
still set afterwards** — if it is lost, the heavy benchmark quietly degrades to the
small sample.

### Runner routing needs an optional secret

`pick-runner` can only see the runner's online status with a token that has
repository **Administration: read**; the default `GITHUB_TOKEN` does not. That is the
optional repo secret **`RUNNER_ADMIN_TOKEN`** (a fine-grained, repo-scoped PAT).
Without it CI always takes the hosted path — safe and zero-config for any
contributor, just slower.

## Known environmental flakiness

`api.github.com` and `productionresultssa12.blob.core.windows.net` have intermittently
failed DNS resolution on this machine (`No such host is known`). Symptoms: token
fetches and `gh run list` need retries, and the runner's live-log/artifact upload to
Azure blob storage can fail mid-run. The upload failure is **non-fatal** — the build
and tests are entirely local, so the run still completes and reports. This is a
machine networking issue, not a workflow defect.
