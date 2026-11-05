# CI's self-hosted runner: retired

From plan step 6.4, CI is one job on GitHub-hosted `windows-latest`
([`.github/workflows/ci.yml`](../.github/workflows/ci.yml); its header says why). Nothing
targets the self-hosted runner any more, and the optional `RUNNER_ADMIN_TOKEN` secret that
let CI see whether it was online is no longer read.

**Take the runner off the machine.** The repository is public, and a registered,
online runner is a machine-takeover risk there: a pull request from a fork runs the
workflow file from its own branch, so it can target the runner's labels and execute
code on the box with the runner's privileges. With no job of ours using it, there is
nothing to lose by removing it.

| | |
| --- | --- |
| Host | the development machine |
| Install root | `C:\actions-runner` |
| Runner name | `tmdqa-selfhosted-1` (labels `self-hosted`, `Windows`, `X64`, `tmdqa`) |
| Service | `actions.runner.kboss805-tmDataQualityAnalyzer.tmdqa-selfhosted-1`, as `NT AUTHORITY\NETWORK SERVICE` |

From an **elevated** shell in `C:\actions-runner` (the service and its registration are
removed together; there is no `svc.cmd` on Windows):

```bash
$remove = gh api -X POST repos/kboss805/tmDataQualityAnalyzer/actions/runners/remove-token -q .token
.\config.cmd remove --token $remove
```

Then check that GitHub lists no runner, delete `C:\actions-runner`, and delete the
`RUNNER_ADMIN_TOKEN` repository secret and the fine-grained token behind it:

```bash
gh api repos/kboss805/tmDataQualityAnalyzer/actions/runners -q '.total_count'
```

```bash
gh secret delete RUNNER_ADMIN_TOKEN --repo kboss805/tmDataQualityAnalyzer
```

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
