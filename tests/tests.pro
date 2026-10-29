QT += core gui widgets printsupport testlib concurrent svg

CONFIG += c++20 console
CONFIG -= app_bundle

TARGET = tmDataQualityAnalyzer_tests

INCLUDEPATH += \
    $$PWD/../include \
    $$PWD/../include/dto \
    $$PWD/../include/model \
    $$PWD/../include/viewmodel \
    $$PWD/../include/view \
    $$PWD/../lib/irig106/include

win32 {
    LIBS += -lws2_32
    # user32: TestMainView links mainview.cpp, whose nativeEvent() calls Win32
    # user32 APIs (see the app .pro).
    LIBS += -luser32
}

# Compile the vendored third-party lib (lib/irig106) without the
# app's -Wall -Wextra so their pre-existing warnings don't bury real app warnings.
include($$PWD/../thirdparty.pri)

# The same Qt resources the app compiles in. Widgets under test load their
# icons from :/resources/..., and without this every one is a null QIcon.
include($$PWD/../resources.pri)
include($$PWD/../codepage.pri)

# Application sources (exclude main.cpp to avoid duplicate main), grouped by layer.
SOURCES += \
    $$PWD/../src/model/channeldata.cpp \
    $$PWD/../src/model/chapter10reader.cpp \
    $$PWD/../src/model/ch10packetreader.cpp \
    $$PWD/../src/model/framesetup.cpp \
    $$PWD/../src/model/framesyncfile.cpp \
    $$PWD/../src/model/frameprocessor.cpp \
    $$PWD/../src/model/stepdetector.cpp \
    $$PWD/../src/model/streamjobbuilder.cpp \
    $$PWD/../src/model/calibrationextractor.cpp \
    $$PWD/../src/model/calibrationrequest.cpp \
    $$PWD/../src/model/pipelinerunner.cpp \
    $$PWD/../src/model/clocktime.cpp \
    $$PWD/../src/model/csvseriesparser.cpp \
    $$PWD/../src/model/csvserieswriter.cpp \
    $$PWD/../src/model/seriesbuilder.cpp \
    $$PWD/../src/model/seriescolumnschema.cpp \
    $$PWD/../src/model/streamconfigschema.cpp \
    $$PWD/../src/model/processingtemplateschema.cpp \
    $$PWD/../src/model/templatematcher.cpp \
    $$PWD/../src/model/textfile.cpp \
    $$PWD/../src/model/tomldocument.cpp \
    $$PWD/../src/viewmodel/mainviewmodel.cpp \
    $$PWD/../src/viewmodel/processingcoordinator.cpp \
    $$PWD/../src/viewmodel/batchcontroller.cpp \
    $$PWD/../src/viewmodel/plotviewmodel.cpp \
    $$PWD/../src/view/mainview.cpp \
    $$PWD/../src/view/mainviewchrome.cpp \
    $$PWD/../src/view/streamconfigdialog.cpp \
    $$PWD/../src/view/streamsubdialogs.cpp \
    $$PWD/../src/view/plotlegendoverlay.cpp \
    $$PWD/../src/view/plotcontextmenu.cpp \
    $$PWD/../src/view/plotexport.cpp \
    $$PWD/../src/view/plotwidget.cpp \
    $$PWD/../src/view/tmchart.cpp \
    $$PWD/../src/view/plotcustomizationdialog.cpp \
    $$PWD/../src/view/exportdialog.cpp \
    $$PWD/../src/view/batchapplydialog.cpp \
    $$PWD/../src/view/processingprogressdialog.cpp

# Application headers, grouped by layer.
HEADERS += \
    $$PWD/../include/constants.h \
    $$PWD/../include/plotconstants.h \
    $$PWD/../include/model/channeldata.h \
    $$PWD/../include/model/chapter10reader.h \
    $$PWD/../include/model/ch10packetreader.h \
    $$PWD/../include/model/intervalgate.h \
    $$PWD/../include/model/irig106lock.h \
    $$PWD/../include/model/packetqueue.h \
    $$PWD/../include/model/framesetup.h \
    $$PWD/../include/model/framesyncfile.h \
    $$PWD/../include/model/framesync.h \
    $$PWD/../include/model/frameprocessor.h \
    $$PWD/../include/model/stepdetector.h \
    $$PWD/../include/model/streamjobbuilder.h \
    $$PWD/../include/model/calibrationextractor.h \
    $$PWD/../include/model/calibrationrequest.h \
    $$PWD/../include/model/pipelinerunner.h \
    $$PWD/../include/model/clocktime.h \
    $$PWD/../include/model/csvseriesparser.h \
    $$PWD/../include/model/csvserieswriter.h \
    $$PWD/../include/model/seriesbuilder.h \
    $$PWD/../include/model/seriescolumnschema.h \
    $$PWD/../include/model/streamconfigschema.h \
    $$PWD/../include/model/processingtemplateschema.h \
    $$PWD/../include/model/templatematcher.h \
    $$PWD/../include/model/textfile.h \
    $$PWD/../include/model/tomldocument.h \
    $$PWD/../include/dto/streamconfig.h \
    $$PWD/../include/dto/framesyncparams.h \
    $$PWD/../include/dto/processingparams.h \
    $$PWD/../include/dto/processedstreamdata.h \
    $$PWD/../include/dto/plotseriesdata.h \
    $$PWD/../include/dto/calibrationprofile.h \
    $$PWD/../include/dto/source.h \
    $$PWD/../include/dto/seriesappearance.h \
    $$PWD/../include/dto/processingtemplate.h \
    $$PWD/../include/viewmodel/mainviewmodel.h \
    $$PWD/../include/viewmodel/processingcoordinator.h \
    $$PWD/../include/viewmodel/batchcontroller.h \
    $$PWD/../include/viewmodel/plotviewmodel.h \
    $$PWD/../include/view/mainview.h \
    $$PWD/../include/view/streamconfigdialog.h \
    $$PWD/../include/view/streamsubdialogs.h \
    $$PWD/../include/view/plotlegendoverlay.h \
    $$PWD/../include/view/plotwidget.h \
    $$PWD/../include/view/tmchart.h \
    $$PWD/../include/view/plotcustomizationdialog.h \
    $$PWD/../include/view/exportdialog.h \
    $$PWD/../include/view/batchapplydialog.h \
    $$PWD/../include/view/processingprogressdialog.h

# The C ABI behind tmdq_core.dll (native/tmdq_core), compiled straight into the test executable so TestAbi
# calls it as a host would, without loading a DLL. TMDQ_ABI_STATIC makes TMDQ_API empty (no dllimport).
# The list is the one native/tmdq_core/tmdq_core.vcxproj compiles; TestCoreQtFree checks they agree.
DEFINES += TMDQ_ABI_STATIC
INCLUDEPATH += $$PWD/../native/tmdq_core
SOURCES += \
    $$PWD/../native/tmdq_core/tmdq_core.cpp \
    $$PWD/../native/tmdq_core/abi_file.cpp \
    $$PWD/../native/tmdq_core/abi_run.cpp \
    $$PWD/../native/tmdq_core/abi_calibration.cpp \
    $$PWD/../native/tmdq_core/abi_formats.cpp \
    $$PWD/../native/tmdq_core/abi_series.cpp
HEADERS += \
    $$PWD/../native/tmdq_core/tmdq_abi.h \
    $$PWD/../native/tmdq_core/abi_internal.h \
    $$PWD/../native/tmdq_core/abi_events.h

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
    tst_constants.cpp \
    tst_batchcontroller.cpp \
    tst_mainview.cpp \
    tst_mainviewmodel_helpers.cpp \
    tst_plotviewmodel.cpp \
    tst_plotwidget.cpp \
    tst_tmchart.cpp \
    tst_plotcustomizationdialog.cpp \
    tst_processingcoordinator.cpp \
    tst_streamconfigdialog.cpp \
    tst_exportdialog.cpp \
    tst_seriescolumnschema.cpp \
    tst_csvseries.cpp \
    tst_framesyncfile.cpp \
    tst_streamjob.cpp \
    tst_seriesbuild.cpp \
    tst_streamconfigschema.cpp \
    tst_processingtemplateschema.cpp \
    tst_templatematcher.cpp \
    tst_coreqtfree.cpp \
    tst_abi.cpp \
    tst_qtprocessingevents.cpp \
    tst_calibrationrequest.cpp

# Test headers (needed for MOC processing)
HEADERS += \
    tst_constants.h \
    tst_batchcontroller.h \
    tst_mainview.h \
    tst_mainviewmodel_helpers.h \
    tst_plotviewmodel.h \
    tst_plotwidget.h \
    tst_tmchart.h \
    tst_plotcustomizationdialog.h \
    tst_processingcoordinator.h \
    tst_streamconfigdialog.h \
    tst_exportdialog.h \
    tst_seriescolumnschema.h \
    tst_csvseries.h \
    tst_framesyncfile.h \
    tst_streamjob.h \
    tst_seriesbuild.h \
    tst_streamconfigschema.h \
    tst_processingtemplateschema.h \
    tst_templatematcher.h \
    tst_coreqtfree.h \
    tst_abi.h \
    tst_qtprocessingevents.h \
    tst_calibrationrequest.h
