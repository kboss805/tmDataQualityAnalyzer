# ---------------------------------------------------------------------------
# thirdparty.pri — vendored-library compilation hygiene
#
# lib/irig106 and lib/qcustomplot are third-party and protected (see the
# "Protected Files" section of docs/CLAUDE.md). They carry pre-existing compiler
# warnings we can't fix at the source. Compile them WITHOUT the app's
# -Wall -Wextra so their noise doesn't bury real app-code warnings — and so the
# whole build is warning-clean, letting CI's warning gate trust every line
# instead of a lib/-filtered subset.
#
# Included by tmDataQualityAnalyzer.pro and tests/tests.pro.
# ---------------------------------------------------------------------------

# irig106 is the ONLY C in the project — every app source is C++ — so clearing
# the C warning flags silences the vendored C sources (irig106ch10.c and friends)
# with zero effect on application code. Scoped per compiler: -w (GCC) / /w (MSVC).
QMAKE_CFLAGS_WARN_ON =
win32-g++:  QMAKE_CFLAGS += -w
win32-msvc: QMAKE_CFLAGS += /w
unix:       QMAKE_CFLAGS += -w

# qcustomplot.cpp is the only third-party C++ translation unit. Its warnings are
# all Qt-API deprecations (QDateTime::toTimeSpec / QDate::startOfDay /
# QImage::mirrored) that we can't touch in vendored code. Silence that one
# warning class project-wide: app code is deprecation-clean today, and a
# deliberate Qt version bump would surface any genuinely removed API as a hard
# compile error regardless of this flag.
#
# (Scoping this to qcustomplot.cpp alone would need either a per-file custom
# compiler — brittle across the debug/release/tests object dirs — or splitting
# the vendored code into a separate static-lib subproject, which would disturb
# the single-.pro build that build_release.ps1 and CI depend on. If preserving
# app-code deprecation warnings ever matters, that static-lib split is the
# follow-up; for now the trade is a good one.)
#
# MSVC equivalent: C4996 is the deprecation warning. If a real MSVC build surfaces
# additional vendored-only qcustomplot warning classes under /W3, add their /wd####
# codes here (vendored code we can't touch), NOT by lowering the app's warning level.
win32-g++:  QMAKE_CXXFLAGS += -Wno-deprecated-declarations
win32-msvc: QMAKE_CXXFLAGS += /wd4996
unix:       QMAKE_CXXFLAGS += -Wno-deprecated-declarations
