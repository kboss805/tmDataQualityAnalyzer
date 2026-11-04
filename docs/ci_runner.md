# CI — the self-hosted Windows runner

Operational notes for the machine that runs CI. The **workflow** side is documented
in the header comment of [`.github/workflows/ci.yml`](../.github/workflows/ci.yml)
(runner routing, the ASCII/PowerShell-5.1 constraints, the Qt-free job); this
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

1. **The toolchain**, pre-installed rather than provisioned per run: MSVC 2022 (the
   Visual Studio 2022 C++ Build Tools) and the .NET 10 SDK, installed machine-wide so the
   service account can run `dotnet`. The build and test scripts dot-source
   `. .\scripts\env.ps1` themselves when `msbuild` or `mt` is missing — each step is a
   fresh shell, so PATH set in one step does **not** carry into the next.
1. **Nothing else.** Until plan step 6.2 the box also needed the Qt `msvc2022_64` kit and
   `jom` (Qt's parallel `nmake`, which cut a full run from 286 s to 93 s), and CI overlaid
   the full PRN recording (`TMDQA_CH10_FULL` in `C:\actions-runner\.env`) for a benchmark
   that now runs at release on the development machine instead. The kit, `jom` and the
   recording can stay on the box or go; no step reads them. The Qt kit staying is why
   `core-without-qt` runs on a hosted image rather than here.

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
- **No interactive desktop**, so no window can be shown. The app's tests run on Avalonia's
  headless platform, which needs none; `test_app.ps1` prints the account and session so
  the log records it.

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

`.env` has survived a reconfigure in practice; nothing in CI reads it any more.

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
