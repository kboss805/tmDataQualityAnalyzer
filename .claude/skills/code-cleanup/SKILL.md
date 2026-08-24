---
name: code-cleanup
description: >-
  Run a whole-codebase cleanup sweep of tmDataQualityAnalyzer: find orphaned/dead code (unreferenced
  functions, classes, files, includes, constants), readability and naming-convention drift, MVVM
  layering violations, and documentation that has fallen out of sync with the code (the test catalog
  in docs/CLAUDE.md, User Story acceptance criteria). Use this WHENEVER the user asks for a "code
  cleanup", "cleanup pass", "find dead code", "check for orphans", "tidy up the codebase", or wants a
  periodic health check of the project as a whole — as opposed to reviewing one change, which is
  tm-code-review's job. This is repo-wide and diff-independent: it looks at the whole tree, not just
  what changed recently. The main hazard is false positives — several things in this codebase look
  orphaned but are deliberately kept (see loadCsvFile in PlotViewModel, kept as the exportCsv partner
  and a test fixture even with no production caller) — so this skill's first move is always checking
  docs/CLAUDE.md and the project's auto-loaded MEMORY.md for a documented reason before flagging
  anything as dead.
---

# Code Cleanup Sweep — tmDataQualityAnalyzer

This is a **whole-codebase** pass, not a diff review — run it periodically or when asked for a
cleanup, not automatically before every commit (that's **tm-code-review**, which is diff-scoped and
catches this project's domain-invariant bugs). The two are complementary: tm-code-review asks "did
this change break something," this skill asks "does the whole tree still look like it was written on
purpose."

## Before flagging anything: check for a documented reason

**The single biggest risk here is a false positive**, because "no caller found by grep" and "dead"
are not the same thing in this codebase. `PlotViewModel::loadCsvFile()`/`loadCsvFileAsync()` have no
production call site — production feeds the plot via `addStreamData()` from in-memory
`ProcessedStreamData` — and they still look deletable. They are not: they're the symmetric partner
of the live `exportCsv()`, their signals drive `PlotWidget`'s loading overlay, and ~20
`TestPlotViewModel` cases use them as a fixture. Deleting them would look like a harmless cleanup
and would actually remove real, deliberate coverage. That exact case is recorded in this project's
memory (`plotviewmodel-csv-load-is-intended-not-dead.md`) precisely so it doesn't get re-discovered
and re-broken.

Before flagging a function, class, file, or constant as unused:

1. **Search `docs/CLAUDE.md`.** The Architecture/Component sections document a number of
   intentionally-kept, low-call-count paths and explain *why* they exist.
2. **Search the auto-loaded `MEMORY.md`** (this session's persistent project memory, described in
   the root `CLAUDE.md`'s "Memory" section). Design decisions and prior review remediation live
   here, not just in the code — the `loadCsvFile` case above is exactly this.
3. **Check whether it's a test-only symbol.** A helper called only from `tests/` is not orphaned —
   it's doing its job. Grep `tests/` explicitly rather than trusting a `src/`-only search.
4. **Check for Qt meta-object usage.** A slot connected with the new-style pointer syntax
   (`&Class::method`, which this project uses throughout `setUpConnections()`) shows up in a normal
   grep; `Q_INVOKABLE` methods and `Q_PROPERTY` accessors reached only through the meta-object
   system will not necessarily show a direct call site. Check for those macros on a method before
   flagging it.

If none of the four account for it, it's a real candidate — but report it as "no caller found in
`src/` or `tests/`, and no documented reason in `CLAUDE.md`/`MEMORY.md`" rather than a flat
"unused," so the user can correct you if there's context you're missing (a downstream consumer, a
planned feature, an in-progress branch).

## What's off-limits

`lib/irig106/**` is third-party and protected — never edit it, and never flag it for cleanup even if
something in it looks dead, unused, or oddly styled by this project's conventions. It is maintained
upstream; this project's job is to wrap it, not tidy it. (See the root `CLAUDE.md` "Hard rules".)

## Finding orphaned code

This is naturally a broad sweep across `src/`, `include/`, and `tests/` — if you're running it as an
agent, consider delegating the initial grep pass to an Explore agent rather than doing dozens of
searches inline.

**Files**: cross-check what's on disk against what's actually built. A `.cpp`/`.h` under `src/` or
`include/` that isn't listed in `tmDataQualityAnalyzer.pro`'s `SOURCES`/`HEADERS` (or `tests/tests.pro`'s,
for test files) is dead weight in the repo regardless of its contents — nothing compiles it, and it
isn't covered by "check the tests" either.

**Functions/classes/constants**: grep the symbol name across the whole tree and count
non-declaration hits. One hit (the declaration) or hits only inside its own `.cpp` means nothing
outside calls it — check for a documented reason (above) before flagging. The three constants
namespaces (`PCMConstants`, `UIConstants`, `PlotConstants`) are worth a pass on their own: the
v2.8.0 cleanup found ~20 unreferenced constants and 37 unused Qt includes that had accumulated over
time, so this class of drift is real and recurring here, not hypothetical.

**Unused includes**: a header pulled in but nothing from it referenced in that translation unit.
`clangd --check=<file>` (see `docs/CLAUDE.md` → clangd / IntelliSense) can surface some of this
directly, once `compile_flags.txt` is regenerated and current.

## Readability and coding-standard drift

Don't restate the rules here — `docs/CLAUDE.md`'s **Coding Conventions** section (naming, the `m_`
prefix, include ordering, memory management) is the single source of truth, and copying it into this
skill risks the copy going stale while the real rules move on. Read it, then check the tree against
it:

- Naming: PascalCase classes, `kPascalCase` constants in namespaces, `m_`-prefixed snake_case
  members, camelCase methods/slots — flag drift; don't silently mass-rename if the pattern turns out
  to be widespread, since that's a bigger conversation than a cleanup sweep.
- Include ordering (Google C++ style, six groups) — mechanical, safe to note file-by-file.
- **MVVM layering**: View files (`src/view/`) should contain no business logic — look for anything
  in `mainview.cpp` or a `*dialog.cpp` that computes rather than displays. Model files should never
  reach into Qt widgets. This project is explicit about the boundary (see `docs/CLAUDE.md`
  Architecture section), and a leak here is worth flagging even without an accompanying functional
  bug.

## Stale documentation

`docs/CLAUDE.md` describes the codebase in prose, and prose drifts silently in a way code doesn't
(nothing fails to compile when a comment goes stale). Two spots worth checking every sweep:

- **The Test Suites catalog.** Compare its list of suites and coverage claims against what's
  actually registered in `tests/main.cpp` and `tests/tests.pro`. A suite renamed, extended, or
  removed without updating its one-paragraph description is silent drift that compounds over
  releases.
- **User Story acceptance criteria.** Spot-check a story or two against the behavior it actually
  describes, especially in areas that saw recent changes. A `[x]` next to something the code no
  longer does is worse than an open item, because nothing signals it needs attention.

This is a sampling check, not a line-by-line re-audit of every story on every sweep — use judgment
about which areas have seen recent churn and are most likely to have drifted.

## Reporting findings

Group by category (Dead code / Readability & convention drift / MVVM layering / Stale docs). For
each: **file:line** (or just the filename, for a whole orphaned file), what you found, your
confidence that it's actually unused (say explicitly whether you checked `CLAUDE.md`/`MEMORY.md`),
and a concrete suggested fix. List real findings only — an empty category is a fine result, not a
failure to find something.

**This sweep proposes; it doesn't delete.** Deletions, renames, and doc rewrites need the user's
go-ahead before you apply them. Present findings, let the user pick what to act on, then apply only
those — the same git-safety discipline this project applies everywhere else (see the root
`CLAUDE.md` guidance on investigating before removing unfamiliar state).

## After applying any fix

Deleting a file or a symbol changes what compiles. Run the **build-and-test** skill afterward — a
green full suite (0 warnings, 0 failed) is the bar here, same as for any other change.
