# CI's self-hosted runner: removed

From plan step 6.4, CI is one job on GitHub-hosted `windows-2022`
([`.github/workflows/ci.yml`](../.github/workflows/ci.yml); its header says why). The
self-hosted runner it replaced was taken off the development machine on 2026-11-05.

| | |
| --- | --- |
| Runner name | `tmdqa-selfhosted-1` (labels `self-hosted`, `Windows`, `X64`, `tmdqa`) |
| Service | `actions.runner.kboss805-tmDataQualityAnalyzer.tmdqa-selfhosted-1`, as `NT AUTHORITY\NETWORK SERVICE` |
| Install root | `C:\actions-runner` |
| In use | from PRs #41 and #42 until #249 (plan step 6.4) |

**What was removed, and how it was checked:**

- **The registration and the service**, together, by `config.cmd remove` with a remove
  token, from an elevated shell (there is no `svc.cmd` on Windows). GitHub lists no
  runner for the repository, and no `actions.runner.*` service, registry key or
  `Runner.Listener` process is left.
- **`C:\actions-runner`**, deleted.
- **The `RUNNER_ADMIN_TOKEN` repository secret**, which let CI see whether the runner was
  online; the repository has no secrets now. The fine-grained token the secret held is
  revoked in its owner's GitHub settings, which nothing in the repository can check.

**Why it went rather than stayed idle:** the repository is public, and a registered,
online runner is a machine-takeover risk there. A pull request from a fork runs the
workflow file from its own branch, so it can target the runner's labels and execute code
on the box with the runner's privileges. With no job using it, there was nothing to lose.

**Do not register a self-hosted runner for this repository again** while it is public.
If CI ever needs something the hosted image lacks, install it in a step of the hosted job.

## What the self-hosted runner taught, still true of the hosted job

- **Windows PowerShell 5.1 decodes a step's script as Windows-1252**, so `ci.yml` stays
  pure ASCII: a UTF-8 dash becomes bytes holding a smart quote, which 5.1 reads as a
  string delimiter ("The string is missing the terminator").
- **5.1 turns a native tool's stderr into a terminating error** under GitHub's default
  `$ErrorActionPreference='stop'` (`cl` prints its banner there), so each step sets
  `Continue` and gates on `$LASTEXITCODE`.
- **The app's tests need no desktop:** they ran on the runner as `NETWORK SERVICE` in
  Session 0 on Avalonia's headless platform (plan step 3.2), and `test_app.ps1` prints
  the account and session it runs as.
- **A file the core has just written may be held for a moment** by the virus scanner;
  the tests read and delete through `TempFiles`, which waits that out (PRs #218, #221,
  #247).
