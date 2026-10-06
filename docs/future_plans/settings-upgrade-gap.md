# Settings on upgrade: US8.0 claims a merge that does not exist

**Status:** scoped, not started. Found while evaluating a move to the Qt Installer
Framework (2026-10-05) — the port would have meant reimplementing something that was
never built.

## The gap

Three US8.0 acceptance criteria in `docs/claude.md` are marked `[x]`. Only the first is
true:

| criterion | reality |
| --- | --- |
| The installer never overwrites an existing user TOML settings file. | **Met.** `onlyifdoesntexist` on the `settings\*` entry in `deploy/tmDataQualityAnalyzer.iss`. |
| If the shipped default TOML has new fields the user's file lacks, the installer saves the new version as `new_x.toml` instead of overwriting. | **Not implemented.** Nothing writes a `new_*.toml`. |
| The installer carries over as many parameter values as possible from the user's old TOML into the new `new_x.toml`. | **Not implemented.** There is no merge anywhere. |

Verified by reading the installer and searching the tree: `tmDataQualityAnalyzer.iss` is
95 declarative lines with **no `[Code]` section at all**, and no `new_x`, migration,
schema-version or merge logic exists in `src/`, `deploy/` or `scripts/`.

## What actually happens on upgrade

`onlyifdoesntexist` copies a settings file **only if it is absent**. So a user who
installed an older version keeps their nine `settings\**` TOMLs untouched forever, and
any field added to a shipped default after their install never reaches them. The file is
preserved, which is the important half — but it also never grows.

## Why this has not bitten yet, and when it would

The readers are written defensively, which is doing the work the installer is credited
with:

- `FrameSetup::readReceiverParams()` accepts the current `[Receivers] Count` /
  `ChannelsPerReceiver` **and** the older `[Parameters]` spelling (v2.12.1).
- `loadFrameSyncFromToml()` treats an absent `Randomized` key as "leave the operator's
  current choice alone" rather than defaulting it to false (v2.13.0).
- `StreamConfigSchema::fromJson()` leaves in-class defaults for anything omitted.

So today a stale settings file degrades to "missing keys fall back to defaults", not to
a wrong result. It becomes a real defect the moment a shipped default gains a field whose
absence cannot be distinguished from a deliberate choice — the `Randomized` case is
exactly that shape and was only safe because the reader was written to notice.

## Two ways to close it

**Option A — correct the criteria to describe what ships.** Replace the two false
criteria with what is actually guaranteed: the user's file is never overwritten, and
readers tolerate files written by any earlier version. Cheapest, honest, and arguably
the right contract — no one has asked for a merge in fourteen releases.

**Option B — implement it.** A merge needs: a `[Code]` section (or a first-run step in
the app), per-file comparison of shipped vs installed keys, writing `new_<name>.toml`
with the user's values carried over, and telling the user it happened. Note this moves
the installer from zero custom code to scripted, which is the thing that currently makes
it cheap to maintain.

**Recommendation: Option A**, plus a one-line note in `UserGuide.txt` saying settings
files are never modified on upgrade and new shipped defaults appear only in a fresh
install. Revisit B only if a shipped default ever needs a field that an old file's
silence cannot express.

## Acceptance (Option A)

- The two false criteria are replaced, not deleted — the behaviour they intended is
  stated as what actually ships.
- `UserGuide.txt` says what an upgrade does to `settings\`.
- The defensive-reader behaviour above is named in `docs/claude.md`, since it is what
  makes the weaker guarantee safe, and a future change that removes it would otherwise
  look harmless.

## Not in scope

The `uninsneveruninstall` flag (settings survive uninstall) is correct and tested, and
is a separate guarantee from this one.
