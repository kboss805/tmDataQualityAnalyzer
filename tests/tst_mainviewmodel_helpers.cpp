#include "tst_mainviewmodel_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QtTest>
#include <QTemporaryDir>

#include "chapter10reader.h"
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

namespace {

QString fixturePath(const QString& name)
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    return dir.filePath("data/test files/" + name);
}

} // namespace

void TestMainViewModelHelpers::openFileListsItsChannelsAndOffersThemAsStreams()
{
    // Opening a recording is where the metadata reader's channel lists reach the dialogs:
    // the combo-box strings, the default stream rows (id, bare label, TMATS data rate) and the
    // reader's selection when the combo boxes move.
    const QString path = fixturePath("prn_testfile, sample.ch10");
    if (!QFile::exists(path))
        QSKIP("prn sample fixture not available");

    MainViewModel vm;
    QSignalSpy errors(&vm, &MainViewModel::errorOccurred);
    QSignalSpy changed(&vm, &MainViewModel::channelListsChanged);
    vm.openFile(path);

    QCOMPARE(errors.count(), 0);
    QVERIFY(changed.count() >= 1);
    QVERIFY(vm.fileLoaded());
    QCOMPARE(vm.timeChannelList(), QStringList{"1 - IRIG-B"});
    QCOMPARE(vm.pcmChannelList(),
             (QStringList{"13 - CH-01 PRN 15 5M", "14 - CH-02 PRN 15 20M",
                          "15 - CH-03 PRN 11 1M", "16 - CH-04 PRN 11 5M"}));

    const QVector<StreamConfig> configs = vm.buildDefaultStreamConfigs();
    QCOMPARE(configs.size(), 4);
    const int    ids[]    = {13, 14, 15, 16};
    const char*  labels[] = {"CH-01 PRN 15 5M", "CH-02 PRN 15 20M", "CH-03 PRN 11 1M", "CH-04 PRN 11 5M"};
    const double mbps[]   = {5.0, 20.0, 1.0, 5.0};
    for (int i = 0; i < 4; i++)
    {
        QCOMPARE(configs[i].pcmChannelId, ids[i]);
        QCOMPARE(configs[i].label, QString(labels[i]));
        QCOMPARE(configs[i].tmatsDataRateMbps, mbps[i]);
        QVERIFY(!configs[i].process);
    }

    // The combo-box indices move the reader's selection (index 0 is the placeholder).
    QCOMPARE(vm.reader()->getCurrentTimeChannelID(), 1);
    vm.setTimeChannelIndex(2);                     // past the one time channel
    QCOMPARE(vm.reader()->getCurrentTimeChannelID(), -1);
    vm.setTimeChannelIndex(1);
    QCOMPARE(vm.reader()->getCurrentTimeChannelID(), 1);
}

void TestMainViewModelHelpers::openFileOnAFileThatCannotBeReadReportsOneErrorAndKeepsNoChannels()
{
    // A failed open is reported exactly once, to the same error signal the View shows, and it
    // leaves no channels: Open starts a fresh session, so the previous file's are gone too.
    const QString good = fixturePath("prn_testfile, sample.ch10");
    if (!QFile::exists(good))
        QSKIP("prn sample fixture not available");

    MainViewModel vm;
    QSignalSpy errors(&vm, &MainViewModel::errorOccurred);
    vm.openFile(good);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(vm.pcmChannelList().size(), 4);

    const QString bad = QDir::tempPath() + "/tst_openfile_missing.ch10";
    QVERIFY(!QFile::exists(bad));
    vm.openFile(bad);

    QCOMPARE(errors.count(), 1);
    QCOMPARE(errors.at(0).at(0).toString(), "Error opening file: " + bad);
    QVERIFY(!vm.fileLoaded());
    QVERIFY(vm.timeChannelList().isEmpty());
    QVERIFY(vm.pcmChannelList().isEmpty());
    QVERIFY(vm.buildDefaultStreamConfigs().isEmpty());
    QVERIFY(vm.inputFilename().isEmpty());
}

void TestMainViewModelHelpers::openFileShowsANonAsciiChannelNameIntact()
{
    // The reader holds a TMATS name as UTF-8; the combo box and the default stream row show it
    // as text. A copy of step_cal gets an accented letter in its PCM channel's name (two bytes,
    // traded for "H-" so no length or checksum changes - the recording has no data checksum).
    const QString source_path = fixturePath("step_cal, example.ch10");
    if (!QFile::exists(source_path))
        QSKIP("step_cal fixture not available");

    QFile source(source_path);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QByteArray all = source.readAll();
    const QByteArray needle = "R-1\\DSI-27:CH-16 AGC";
    const QByteArray patched = QByteArray("R-1\\DSI-27:C") + "\xC3\xA9" + "-1 AGC";
    QCOMPARE(needle.size(), patched.size());
    const int at = all.indexOf(needle);
    QVERIFY(at >= 0 && at < 20000);
    all.replace(at, patched.size(), patched);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("accent.ch10");
    {
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        QVERIFY(out.write(all) == all.size());
    }

    MainViewModel vm;
    QSignalSpy errors(&vm, &MainViewModel::errorOccurred);
    vm.openFile(path);
    QCOMPARE(errors.count(), 0);

    const QString name = QStringLiteral("C") + QChar(0x00E9) + QStringLiteral("-1 AGC STEP CALS 800K");
    QCOMPARE(vm.pcmChannelList(), QStringList{"26 - " + name});
    const QVector<StreamConfig> configs = vm.buildDefaultStreamConfigs();
    QCOMPARE(configs.size(), 1);
    QCOMPARE(configs[0].label, name);
}
