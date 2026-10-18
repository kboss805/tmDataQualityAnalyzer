#include "tst_mainviewmodel_helpers.h"
#include <QFile>
#include <QSignalSpy>
#include <QtTest>
#include <QTemporaryDir>

#include "mainviewmodel.h"
#include "processingcoordinator.h"

void TestMainViewModelHelpers::classifyLogMessageSeverity()
{
    using LL = MainViewModel::LogLevel;
    // ERROR / WARNING are matched anywhere; the success cues anchor at the start.
    QCOMPARE(static_cast<int>(MainViewModel::classifyLogMessage("File read ERROR; aborting.")),
             static_cast<int>(LL::Error));
    QCOMPARE(static_cast<int>(MainViewModel::classifyLogMessage("WARNING: 2 time gap(s) detected.")),
             static_cast<int>(LL::Warning));
    QCOMPARE(static_cast<int>(MainViewModel::classifyLogMessage("Pre-scan result: 3 PCM channels.")),
             static_cast<int>(LL::Success));
    QCOMPARE(static_cast<int>(MainViewModel::classifyLogMessage("Processing complete.")),
             static_cast<int>(LL::Success));
    QCOMPARE(static_cast<int>(MainViewModel::classifyLogMessage("Opening: test.ch10")),
             static_cast<int>(LL::Info));
    // Error outranks the other cues when several are present.
    QCOMPARE(static_cast<int>(MainViewModel::classifyLogMessage("ERROR with a WARNING too")),
             static_cast<int>(LL::Error));
}

void TestMainViewModelHelpers::buildStreamJobAttachesCalibrationByWord()
{
    // The step-calibration profiles live in the stream's config keyed by word index, and
    // reach the word map in the job this builds. A key that came across wrong would attach
    // each profile to the wrong channel, or to none: the run would still finish and the
    // plot would look plausible, only calibrated against the wrong receiver.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString toml = dir.filePath("rcvr.toml");
    {
        QFile f(toml);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("[L_RCVR1]\nWord = 1\n[R_RCVR1]\nWord = 2\n[C_RCVR1]\nWord = 3\n");
    }

    CalibrationProfile good;
    good.valid = true;
    good.points = { {1000.0, 0.0}, {2000.0, 6.0} };
    CalibrationProfile unused = good;     // a valid profile for a word the map does not have
    CalibrationProfile bad;               // not valid: stays linear

    StreamConfig cfg;
    cfg.pcmChannelId        = 5;
    cfg.label               = "Rcvr";
    cfg.mode                = StreamMode::ReceiverChannelInfo;
    cfg.sync.pattern        = "FE6B2840";
    cfg.sync.mask           = "FFFFFFFF";
    cfg.sync.bitsInMinorFrame = 800;
    cfg.receiverParamsToml  = toml;
    cfg.calibrationByWord.insert(0, good);     // word 0 -> L_RCVR1
    cfg.calibrationByWord.insert(1, bad);      // word 1 -> R_RCVR1, invalid
    cfg.calibrationByWord.insert(7, unused);   // no parameter at word 7

    MainViewModel vm;
    QSignalSpy log(&vm, &MainViewModel::logMessageReceived);
    ProcessingParams base;
    StreamJob job;
    QString error;
    QVERIFY2(vm.buildStreamJob(cfg, base, job, error), qPrintable(error));

    QVERIFY(job.frameSetup);
    QCOMPARE(job.frameSetup->length(), 3);
    QVERIFY(job.frameSetup->getParameter(0)->profile.valid);       // word 0 attached
    QCOMPARE(job.frameSetup->getParameter(0)->profile.points.size(), size_t(2));
    QVERIFY(!job.frameSetup->getParameter(1)->profile.valid);      // invalid one is not
    QVERIFY(!job.frameSetup->getParameter(2)->profile.valid);      // nothing supplied

    QStringList lines;
    for (const QList<QVariant>& args : log)
        lines << args.at(0).toString();
    QVERIFY2(lines.join('\n').contains("provided 3 profile(s), attached 1 to word map."),
             qPrintable(lines.join('\n')));
}
