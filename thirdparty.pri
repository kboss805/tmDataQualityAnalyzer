# ---------------------------------------------------------------------------
# thirdparty.pri - vendored-library compilation hygiene
#
# lib/irig106 is third-party and protected (see the "Protected Files" section of
# docs/claude.md). It carries pre-existing compiler warnings we can't fix at the
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

# ---------------------------------------------------------------------------
# Qt's own headers are third party too.
#
# Qt 6.12 deprecated QCoreApplication::compressEvent, and QGuiApplication overrides
# it — so qguiapplication.h(169) emits C4996 for EVERY translation unit that includes
# <QGuiApplication>, 13 of them in the app. We never call compressEvent; there is no
# fix available in our code, only suppression. And CI's gate matches
# 'warning [A-Z]\d+', so a clean build cannot go green while they are visible.
#
# This is NOT /wd4996. That flag lived in this file, suppressed deprecation warnings
# for APPLICATION code as collateral, and was deliberately removed in v2.9.0 (see the
# note above). Re-adding it would undo that.
#
# /external: is MSVC's purpose-built mechanism instead. It suppresses warnings whose
# LOCATION is an external header while leaving our own files fully warned — and a
# deprecated API called from our code is reported at OUR call site, so those keep
# warning exactly as v2.9.0 intended. anglebrackets covers Qt, the CRT and the Windows
# SDK in one rule; every first-party header in this project is quote-included.
#
# Stable since VS2019 16.10 (no /experimental: needed); this toolchain is 19.51.
QMAKE_CXXFLAGS += /external:anglebrackets /external:W0
