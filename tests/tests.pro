QT += core gui widgets printsupport testlib concurrent svg

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tmDataQualityAnalyzer_tests

INCLUDEPATH += \
    $$PWD/../include \
    $$PWD/../lib/irig106/include \
    $$PWD/../lib/qcustomplot

win32 {
    LIBS += -lws2_32
    QMAKE_CXXFLAGS += -Wa,-mbig-obj  # Required for QCustomPlot large object file on MinGW
}

# Application sources (exclude main.cpp to avoid duplicate main)
SOURCES += \
    $$PWD/../src/channeldata.cpp \
    $$PWD/../src/chapter10reader.cpp \
    $$PWD/../src/ch10packetreader.cpp \
    $$PWD/../src/framesetup.cpp \
    $$PWD/../src/mainviewmodel.cpp \
    $$PWD/../src/processingcoordinator.cpp \
    $$PWD/../src/mainview.cpp \
    $$PWD/../src/streamconfigdialog.cpp \
    $$PWD/../src/timeextractionwidget.cpp \
    $$PWD/../src/frameprocessor.cpp \
    $$PWD/../src/plotviewmodel.cpp \
    $$PWD/../src/plotwidget.cpp \
    $$PWD/../src/exportdialog.cpp \
    $$PWD/../src/tomlconfighelper.cpp \
    $$PWD/../lib/qcustomplot/qcustomplot.cpp

# Application headers
HEADERS += \
    $$PWD/../include/channeldata.h \
    $$PWD/../include/chapter10reader.h \
    $$PWD/../include/ch10packetreader.h \
    $$PWD/../include/packetqueue.h \
    $$PWD/../include/constants.h \
    $$PWD/../include/framesetup.h \
    $$PWD/../include/mainviewmodel.h \
    $$PWD/../include/processingcoordinator.h \
    $$PWD/../include/mainview.h \
    $$PWD/../include/streamconfig.h \
    $$PWD/../include/streamconfigdialog.h \
    $$PWD/../include/processedstreamdata.h \
    $$PWD/../include/frameprocessor.h \
    $$PWD/../include/processingparams.h \
    $$PWD/../include/timefields.h \
    $$PWD/../include/timeextractionwidget.h \
    $$PWD/../include/plotviewmodel.h \
    $$PWD/../include/plotwidget.h \
    $$PWD/../include/exportdialog.h \
    $$PWD/../include/tomlconfighelper.h \
    $$PWD/../lib/qcustomplot/qcustomplot.h

# irig106 library sources
SOURCES += \
    $$PWD/../lib/irig106/src/irig106ch10.c \
    $$PWD/../lib/irig106/src/i106_time.c \
    $$PWD/../lib/irig106/src/i106_data_stream.c \
    $$PWD/../lib/irig106/src/i106_decode_time.c \
    $$PWD/../lib/irig106/src/i106_decode_tmats.c \
    $$PWD/../lib/irig106/src/i106_decode_tmats_g.c \
    $$PWD/../lib/irig106/src/i106_decode_tmats_r.c \
    $$PWD/../lib/irig106/src/i106_decode_tmats_m.c \
    $$PWD/../lib/irig106/src/i106_decode_tmats_p.c \
    $$PWD/../lib/irig106/src/i106_decode_tmats_b.c \
    $$PWD/../lib/irig106/src/i106_decode_tmats_c.c \
    $$PWD/../lib/irig106/src/i106_decode_tmats_d.c \
    $$PWD/../lib/irig106/src/i106_decode_pcmf1.c

# irig106 library headers
HEADERS += \
    $$PWD/../lib/irig106/include/irig106ch10.h \
    $$PWD/../lib/irig106/include/i106_data_stream.h \
    $$PWD/../lib/irig106/include/i106_decode_time.h \
    $$PWD/../lib/irig106/include/i106_time.h \
    $$PWD/../lib/irig106/include/i106_stdint.h \
    $$PWD/../lib/irig106/include/config.h \
    $$PWD/../lib/irig106/include/i106_decode_tmats.h \
    $$PWD/../lib/irig106/include/i106_decode_tmats_g.h \
    $$PWD/../lib/irig106/include/i106_decode_tmats_r.h \
    $$PWD/../lib/irig106/include/i106_decode_tmats_m.h \
    $$PWD/../lib/irig106/include/i106_decode_tmats_p.h \
    $$PWD/../lib/irig106/include/i106_decode_tmats_b.h \
    $$PWD/../lib/irig106/include/i106_decode_tmats_c.h \
    $$PWD/../lib/irig106/include/i106_decode_tmats_d.h \
    $$PWD/../lib/irig106/include/i106_decode_tmats_common.h \
    $$PWD/../lib/irig106/include/i106_decode_pcmf1.h

# Test sources
SOURCES += \
    main.cpp \
    tst_channeldata.cpp \
    tst_chapter10reader.cpp \
    tst_constants.cpp \
    tst_mainview.cpp \
    tst_mainviewmodel_helpers.cpp \
    tst_framesetup.cpp \
    tst_plotviewmodel.cpp \
    tst_plotwidget.cpp \
    tst_frameprocessor.cpp \
    tst_timeextractionwidget.cpp \
    tst_processingcoordinator.cpp \
    tst_streamconfigdialog.cpp \
    tst_exportdialog.cpp

# Test headers (needed for MOC processing)
HEADERS += \
    tst_channeldata.h \
    tst_chapter10reader.h \
    tst_constants.h \
    tst_mainview.h \
    tst_mainviewmodel_helpers.h \
    tst_framesetup.h \
    tst_plotviewmodel.h \
    tst_plotwidget.h \
    tst_frameprocessor.h \
    tst_timeextractionwidget.h \
    tst_processingcoordinator.h \
    tst_streamconfigdialog.h \
    tst_exportdialog.h
