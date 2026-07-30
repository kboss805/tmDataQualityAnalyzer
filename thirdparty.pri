# ---------------------------------------------------------------------------
# thirdparty.pri - vendored-library compilation hygiene
#
# lib/irig106 is third-party and protected (see the "Protected Files" section of
# docs/CLAUDE.md). It carries pre-existing compiler warnings we can't fix at the
# source. Compile it without the app's default warning level so its noise doesn't
# bury real app-code warnings - and so the whole build is warning-clean, letting
# CI's warning gate trust every line instead of a lib/-filtered subset.
#
# Included by tmDataQualityAnalyzer.pro and tests/tests.pro.
# ---------------------------------------------------------------------------

# NOTE: this file previously also set /wd4996 project-wide, because qcustomplot.cpp
# emitted Qt deprecation warnings that could not be fixed in vendored code. That
# suppression applied to APPLICATION code too, hiding any deprecation it introduced.
# QCustomPlot is gone, so the flag is gone with it and app-code deprecation warnings
# are visible - and gated - again.

# irig106 is the ONLY C in the project — every app source is C++ — so clearing
# the C warning flags silences the vendored C sources (irig106ch10.c and friends)
# with zero effect on application code.
QMAKE_CFLAGS_WARN_ON =
QMAKE_CFLAGS += /w
