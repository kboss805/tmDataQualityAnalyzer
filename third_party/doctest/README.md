# doctest

The single-header test framework the native core's tests use instead of Qt Test (plan decision D2:
a vendored header, so it needs no package manager and builds the same under qmake and MSBuild).

- **Version:** 2.4.12
- **Source:** <https://raw.githubusercontent.com/doctest/doctest/v2.4.12/doctest/doctest.h>
  and `LICENSE.txt` from the same tag (MIT, Copyright (c) 2016-2023 Viktor Kirilov).
- **SHA-256 of `doctest.h` as fetched:** `94029a7d32da24a56249658147dbd2b33ff0b9ed665295cbbaf19aafff5b0ced`
- **Do not edit it.** Take a new release by replacing both files and updating the version and hash here.

Used by `native/tmdq_core_tests/`, built with MSBuild and no Qt.
