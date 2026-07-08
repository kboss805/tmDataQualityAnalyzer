QT += core gui widgets printsupport testlib concurrent svg

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tmDataQualityAnalyzer_tests

INCLUDEPATH += \
    $$PWD/../include \
    $$PWD/../include/dto \
    $$PWD/../include/model \
    $$PWD/../include/viewmodel \
    $$PWD/../include/view \
    $$PWD/../lib/irig106/include \
    $$PWD/../lib/qcustomplot

win32 {
    LIBS += -lws2_32
    QMAKE_CXXFLAGS += -Wa,-mbig-obj  # Required for QCustomPlot large object file on MinGW
}

# Compile the vendored third-party libs (lib/irig106, lib/qcustomplot) without the
# app's -Wall -Wextra so their pre-existing warnings don't bury real app warnings.
include($$PWD/../thirdparty.pri)

# Application sources (exclude main.cpp to avoid duplicate main), grouped by layer.
SOURCES += \
    $$PWD/../src/model/channeldata.cpp \
    $$PWD/../src/model/chapter10reader.cpp \
    $$PWD/../src/model/ch10packetreader.cpp \
    $$PWD/../src/model/framesetup.cpp \
    $$PWD/../src/model/frameprocessor.cpp \
    $$PWD/../src/model/stepdetector.cpp \
    $$PWD/../src/model/calibrationextractor.cpp \
    $$PWD/../src/model/csvseriesparser.cpp \
    $$PWD/../src/model/seriescolumnschema.cpp \
    $$PWD/../src/model/streamconfigschema.cpp \
    $$PWD/../src/model/tomlconfighelper.cpp \
    $$PWD/../src/viewmodel/mainviewmodel.cpp \
    $$PWD/../src/viewmodel/processingcoordinator.cpp \
    $$PWD/../src/viewmodel/plotviewmodel.cpp \
    $$PWD/../src/view/mainview.cpp \
    $$PWD/../src/view/streamconfigdialog.cpp \
    $$PWD/../src/view/plotwidget.cpp \
    $$PWD/../src/view/plotcustomizationdialog.cpp \
    $$PWD/../src/view/exportdialog.cpp \
    $$PWD/../src/view/processingprogressdialog.cpp \
    $$PWD/../lib/qcustomplot/qcustomplot.cpp

# Application headers, grouped by layer.
HEADERS += \
    $$PWD/../include/constants.h \
    $$PWD/../include/model/channeldata.h \
    $$PWD/../include/model/chapter10reader.h \
    $$PWD/../include/model/ch10packetreader.h \
    $$PWD/../include/model/packetqueue.h \
    $$PWD/../include/model/framesetup.h \
    $$PWD/../include/model/frameprocessor.h \
    $$PWD/../include/model/stepdetector.h \
    $$PWD/../include/model/calibrationextractor.h \
    $$PWD/../include/model/csvseriesparser.h \
    $$PWD/../include/model/seriescolumnschema.h \
    $$PWD/../include/model/streamconfigschema.h \
    $$PWD/../include/model/tomlconfighelper.h \
    $$PWD/../include/dto/streamconfig.h \
    $$PWD/../include/dto/framesyncparams.h \
    $$PWD/../include/dto/processingparams.h \
    $$PWD/../include/dto/processedstreamdata.h \
    $$PWD/../include/dto/plotseriesdata.h \
    $$PWD/../include/dto/calibrationprofile.h \
    $$PWD/../include/dto/timefields.h \
    $$PWD/../include/dto/source.h \
    $$PWD/../include/viewmodel/mainviewmodel.h \
    $$PWD/../include/viewmodel/processingcoordinator.h \
    $$PWD/../include/viewmodel/plotviewmodel.h \
    $$PWD/../include/view/mainview.h \
    $$PWD/../include/view/streamconfigdialog.h \
    $$PWD/../include/view/streamsubdialogs.h \
    $$PWD/../include/view/plotwidget.h \
    $$PWD/../include/view/plotcustomizationdialog.h \
    $$PWD/../include/view/exportdialog.h \
    $$PWD/../include/view/processingprogressdialog.h \
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
    tst_plotcustomizationdialog.cpp \
    tst_frameprocessor.cpp \
    tst_processingcoordinator.cpp \
    tst_streamconfigdialog.cpp \
    tst_exportdialog.cpp \
    tst_seriescolumnschema.cpp \
    tst_streamconfigschema.cpp \
    tst_stepdetector.cpp \
    tst_calibrationextractor.cpp

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
    tst_plotcustomizationdialog.h \
    tst_frameprocessor.h \
    tst_processingcoordinator.h \
    tst_streamconfigdialog.h \
    tst_exportdialog.h \
    tst_seriescolumnschema.h \
    tst_streamconfigschema.h \
    tst_stepdetector.h \
    tst_calibrationextractor.h
