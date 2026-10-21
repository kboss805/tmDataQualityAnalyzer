# =============================================================================
# tmdq_core.pro - the interop spike's native DLL.
#
# Builds the decommutation core behind the flat C ABI in tmdq_abi.h. Deliberately
# minimal: QT = core only, no gui/widgets/svg/printsupport, and no QtConcurrent (the
# ABI uses std::thread so the DLL does not need the Qt global thread pool, nor a
# QCoreApplication in the host).
#
# This still LINKS Qt Core - de-Qt'ing the core is separate work that belongs on main,
# where it keeps the Qt application building at every step. The ABI is Qt-free; what
# sits behind it is not yet, and does not need to be to prove the boundary.
#
# Build (from this directory, after dot-sourcing scripts/env.ps1):
#   qmake tmdq_core.pro -spec win32-msvc CONFIG+=release
#   & $env:TMDQ_MAKE -f Makefile.Release
#
# Or just run run_spike.ps1, which does this and then drives the DLL from C#.
# =============================================================================

# FINDING, from trying QT -= gui first: the model layer transitively needs **QtGui**,
# and it should not. include/constants.h holds PlotConstants' QColor arrays
# (kFrameSyncLockPrimaryColors and friends) in the same header as PCMConstants and
# AppVersion, so every model translation unit includes <QColor> and the build dies with
#
#   constants.h(11): fatal error C1083: Cannot open include file: 'QColor'
#
# That is a real, small blocker for the de-Qt work on main: splitting constants.h into
# processing constants and presentation constants would let a native core drop QtGui
# entirely. Recorded in README.md. Carrying gui here keeps the spike moving without
# pretending the coupling is not there.
QT       += core gui
TEMPLATE  = lib
CONFIG   += c++17 dll
CONFIG   -= app_bundle

TARGET = tmdq_core

DEFINES += TMDQ_ABI_BUILD

# Same six-group include set the app and tests use, one level deeper.
INCLUDEPATH += \
    $$PWD/../../include \
    $$PWD/../../include/dto \
    $$PWD/../../include/model \
    $$PWD/../../lib/irig106/include

win32 {
    LIBS += -lws2_32   # WSASocketW(), as the app needs
    LIBS += -luser32
}

# Compile the vendored third-party lib without the app's warning flags, so irig106's
# pre-existing warnings do not bury real ones. Same include the app and tests use.
include($$PWD/../../thirdparty.pri)

SOURCES += \
    $$PWD/tmdq_abi.cpp

HEADERS += \
    $$PWD/tmdq_abi.h

# --- Model layer -------------------------------------------------------------
# Only what a lock-only run actually needs to link. Notably absent: the viewmodel and
# view layers, the CSV/template schemas, and calibrationextractor - a Frame Sync Lock
# stream touches none of them, and keeping them out is what makes this DLL small enough
# to be worth measuring.
SOURCES += \
    $$PWD/../../src/model/channeldata.cpp \
    $$PWD/../../src/model/chapter10reader.cpp \
    $$PWD/../../src/model/ch10packetreader.cpp \
    $$PWD/../../src/model/framesetup.cpp \
    $$PWD/../../src/model/frameprocessor.cpp \
    $$PWD/../../src/model/stepdetector.cpp \
    $$PWD/../../src/model/tomldocument.cpp

HEADERS += \
    $$PWD/../../include/model/chapter10reader.h \
    $$PWD/../../include/model/ch10packetreader.h \
    $$PWD/../../include/model/framesetup.h \
    $$PWD/../../include/model/frameprocessor.h \
    $$PWD/../../include/model/packetqueue.h \
    $$PWD/../../include/model/stepdetector.h \
    $$PWD/../../include/model/tomldocument.h

# --- irig106 library ---------------------------------------------------------
# Dependency order, not alphabetized, matching the app and tests.
SOURCES += \
    $$PWD/../../lib/irig106/src/irig106ch10.c \
    $$PWD/../../lib/irig106/src/i106_time.c \
    $$PWD/../../lib/irig106/src/i106_data_stream.c \
    $$PWD/../../lib/irig106/src/i106_decode_time.c \
    $$PWD/../../lib/irig106/src/i106_decode_tmats.c \
    $$PWD/../../lib/irig106/src/i106_decode_tmats_g.c \
    $$PWD/../../lib/irig106/src/i106_decode_tmats_r.c \
    $$PWD/../../lib/irig106/src/i106_decode_tmats_m.c \
    $$PWD/../../lib/irig106/src/i106_decode_tmats_p.c \
    $$PWD/../../lib/irig106/src/i106_decode_tmats_b.c \
    $$PWD/../../lib/irig106/src/i106_decode_tmats_c.c \
    $$PWD/../../lib/irig106/src/i106_decode_tmats_d.c \
    $$PWD/../../lib/irig106/src/i106_decode_pcmf1.c
