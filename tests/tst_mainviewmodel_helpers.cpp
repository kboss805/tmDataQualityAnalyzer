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

namespace {

/// A lock-only stream configuration with a valid sync, for tests to change one field of.
StreamConfig validLockConfig()
{
    StreamConfig cfg;
    cfg.pcmChannelId          = 5;
    cfg.label                 = "Stream";
    cfg.mode                  = StreamMode::FrameSyncLockStats;
    cfg.sync.pattern          = "FE6B2840";
    cfg.sync.mask             = "";
    cfg.sync.bitsInMinorFrame = 800;
    return cfg;
}

} // namespace

void TestMainViewModelHelpers::buildStreamJobDecodesTheSyncPatternAndMask()
{
    // The hex fields are decoded the way QString::toULongLong(16) does: surrounding whitespace is
    // ignored, a leading '+' and a "0x" prefix are accepted, any '-' is refused (even "-0"), and
    // more than 64 bits is refused. The pattern's length in bits is its CHARACTER count times four,
    // whatever those characters are, so " FE6B " is 24 bits; and an empty mask means "every bit of
    // the pattern". Both the main run and the calibration extraction decode this way.
    QFETCH(QString, pattern);
    QFETCH(QString, mask);
    QFETCH(bool, ok);
    QFETCH(qulonglong, sync);
    QFETCH(qulonglong, mask_value);
    QFETCH(int, length);

    StreamConfig cfg = validLockConfig();
    cfg.sync.pattern = pattern;
    cfg.sync.mask    = mask;

    MainViewModel vm;
    ProcessingParams base;
    StreamJob job;
    QString error;
    QCOMPARE(vm.buildStreamJob(cfg, base, job, error), ok);
    if (!ok)
    {
        return;
    }
    QCOMPARE(qulonglong(job.params.frameSync), sync);
    QCOMPARE(qulonglong(job.params.frameSyncMask), mask_value);
    QCOMPARE(job.params.syncPatternLength, length);
}

void TestMainViewModelHelpers::buildStreamJobDecodesTheSyncPatternAndMask_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("mask");
    QTest::addColumn<bool>("ok");
    QTest::addColumn<qulonglong>("sync");
    QTest::addColumn<qulonglong>("mask_value");
    QTest::addColumn<int>("length");

    constexpr qulonglong kAll = 0xFFFFFFFFFFFFFFFFull;
    auto good = [](const char* name, const QString& pattern, const QString& mask, qulonglong sync, qulonglong mask_value, int length) {
        QTest::newRow(name) << pattern << mask << true << sync << mask_value << length;
    };
    auto bad = [](const char* name, const QString& pattern, const QString& mask) {
        QTest::newRow(name) << pattern << mask << false << qulonglong(0) << qulonglong(0) << 0;
    };

    good("plain",            "FE6B2840", "", 0xFE6B2840, 0xFFFFFFFF, 32);
    good("lower-case",       "fe6b2840", "", 0xFE6B2840, 0xFFFFFFFF, 32);
    good("spaces-around",    " FE6B ", "", 0xFE6B, 0xFFFFFF, 24);
    good("space-after",      "FE6B ", "", 0xFE6B, 0xFFFFF, 20);
    good("space-before",     " FE6B", "", 0xFE6B, 0xFFFFF, 20);
    good("0x-prefix",        "0xFE6B", "", 0xFE6B, 0xFFFFFF, 24);
    good("0X-prefix",        "0XFE6B", "", 0xFE6B, 0xFFFFFF, 24);
    good("plus-sign",        "+FE6B", "", 0xFE6B, 0xFFFFF, 20);
    good("zero",             "0", "", 0, 0xF, 4);
    good("leading-zeros",    "00FE", "", 0xFE, 0xFFFF, 16);
    good("tab-newline",      "\t7\n", "", 7, 0xFFF, 12);
    good("looks-like-binary","0b1", "", 0xB1, 0xFFF, 12);
    good("exponent-looking", "1e5", "", 0x1E5, 0xFFF, 12);
    good("sixty-four-bits",  "FFFFFFFFFFFFFFFF", "", kAll, kAll, 64);
    good("top-bit-only",     "8000000000000000", "", 0x8000000000000000ull, kAll, 64);
    bad("negative",          "-FE6B", "");
    bad("negative-zero",     "-0", "");
    bad("inner-space",       "FE 6B", "");
    bad("underscore",        "FE_6B", "");
    bad("comma",             "FE,6B", "");
    bad("sixty-five-bits",   "10000000000000000", "");
    bad("not-hex",           "G", "");
    bad("blank",             " ", "");
    bad("bare-0x",           "0x", "");
    bad("x-first",           "x1", "");
    bad("trailing-dot",      "FE6B.", "");
    bad("arabic-indic-digit", QString(QChar(0x0661)), "");
    bad("bare-plus",         "+", "");
    bad("bare-minus",        "-", "");
    bad("double-plus",       "++1", "");

    good("mask-plain",       "FE6B2840", "FE6B2840", 0xFE6B2840, 0xFE6B2840, 32);
    good("mask-short",       "FE6B2840", "00FE", 0xFE6B2840, 0xFE, 32);
    good("mask-zero",        "FE6B2840", "0", 0xFE6B2840, 0, 32);
    good("mask-spaces",      "FE6B2840", " FE6B ", 0xFE6B2840, 0xFE6B, 32);
    good("mask-0x",          "FE6B2840", "0xFE6B", 0xFE6B2840, 0xFE6B, 32);
    good("mask-plus",        "FE6B2840", "+FE6B", 0xFE6B2840, 0xFE6B, 32);
    good("mask-wide",        "FE6B2840", "FFFFFFFFFFFFFFFF", 0xFE6B2840, kAll, 32);
    bad("mask-negative",     "FE6B2840", "-FE6B");
    bad("mask-negative-zero","FE6B2840", "-0");
    bad("mask-inner-space",  "FE6B2840", "FE 6B");
    bad("mask-overflow",     "FE6B2840", "10000000000000000");
    bad("mask-blank",        "FE6B2840", " ");
    bad("mask-not-hex",      "FE6B2840", "G");
}

void TestMainViewModelHelpers::buildStreamJobBuildsTheAcquisitionParameters()
{
    // The remaining fields a stream's configuration contributes to its worker's parameters.
    MainViewModel vm;
    vm.setSwapBytes(false);   // file-level, so it comes from the view model and not from the stream

    StreamConfig cfg = validLockConfig();
    cfg.pcmChannelId          = 17;
    cfg.label                 = "Rcvr";
    cfg.sync.bitsInMinorFrame = 8000;
    cfg.sync.randomized       = true;
    cfg.sync.inverted         = true;
    cfg.sync.dataRateMbps     = 0.8;
    cfg.samplePeriodIndex     = 2;

    ProcessingParams base;
    base.timeChannelId = 3;                       // set by the view model, carried through
    StreamJob job;
    QString error;
    QVERIFY2(vm.buildStreamJob(cfg, base, job, error), qPrintable(error));

    const ProcessingParams& p = job.params;
    QCOMPARE(p.timeChannelId, 3);
    QCOMPARE(p.pcmChannelId, 17);
    QCOMPARE(p.bitsInMinorFrame, 8000);
    QCOMPARE(p.wordsInMinorFrame, 500);           // 8000 bits / 16-bit words
    QVERIFY(p.isRandomized);
    QVERIFY(p.isInverted);
    QVERIFY(!p.swapBytes);
    QCOMPARE(p.dataRateBps, 800000.0);
    QCOMPARE(p.mode, StreamMode::FrameSyncLockStats);
    QCOMPARE(p.streamLabel, std::string("Rcvr"));
    QCOMPARE(p.samplePeriodSec, UIConstants::kSamplePeriod10ms);

    // A data rate of zero means "use the TMATS rate": passed on as zero, not as a rate.
    cfg.sync.dataRateMbps = 0.0;
    QVERIFY(vm.buildStreamJob(cfg, base, job, error));
    QCOMPARE(job.params.dataRateBps, 0.0);

    // The sample-period index picks one of three periods; anything else falls back to 100 ms.
    const struct { int index; double period; } periods[] = {
        {0, UIConstants::kSamplePeriod1s}, {1, UIConstants::kSamplePeriod100ms},
        {2, UIConstants::kSamplePeriod10ms}, {3, UIConstants::kSamplePeriod100ms}, {-1, UIConstants::kSamplePeriod100ms}};
    for (const auto& c : periods)
    {
        cfg.samplePeriodIndex = c.index;
        QVERIFY(vm.buildStreamJob(cfg, base, job, error));
        QCOMPARE(job.params.samplePeriodSec, c.period);
    }

    // The file-level byte order follows the view model.
    vm.setSwapBytes(true);
    QVERIFY(vm.buildStreamJob(cfg, base, job, error));
    QVERIFY(job.params.swapBytes);

    // A stream with no label is described by its channel number.
    cfg.label.clear();
    QVERIFY(vm.buildStreamJob(cfg, base, job, error));
    QCOMPARE(job.params.streamLabel, std::string("Ch 17"));
}

void TestMainViewModelHelpers::buildStreamJobReportsEachWayAConfigIsRejected()
{
    QFETCH(QString, pattern);
    QFETCH(QString, mask);
    QFETCH(int, bits);
    QFETCH(QString, label);
    QFETCH(QString, expected);

    StreamConfig cfg = validLockConfig();
    cfg.label                 = label;
    cfg.sync.pattern          = pattern;
    cfg.sync.mask             = mask;
    cfg.sync.bitsInMinorFrame = bits;

    MainViewModel vm;
    ProcessingParams base;
    StreamJob job;
    QString error;
    QVERIFY(!vm.buildStreamJob(cfg, base, job, error));
    QCOMPARE(error, expected);
}

void TestMainViewModelHelpers::buildStreamJobReportsEachWayAConfigIsRejected_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("mask");
    QTest::addColumn<int>("bits");
    QTest::addColumn<QString>("label");
    QTest::addColumn<QString>("expected");

    // Each message leads with the stream's label, or "Ch <id>" when it has none.
    QTest::newRow("empty-pattern")   << "" << "" << 800 << "Rcvr" << "Rcvr: A frame sync pattern is required.";
    QTest::newRow("bad-pattern")     << "ZZ" << "" << 800 << "Rcvr" << "Rcvr: Invalid frame sync pattern 'ZZ'.";
    QTest::newRow("bad-mask")        << "FE6B2840" << "XYZ" << 800 << "Rcvr" << "Rcvr: Invalid frame sync mask 'XYZ'.";
    QTest::newRow("unlabelled")      << "ZZ" << "" << 800 << "" << "Ch 5: Invalid frame sync pattern 'ZZ'.";
    QTest::newRow("frame-too-short") << "FE6B2840" << "" << PCMConstants::kMinFrameLengthBits - 1 << "Rcvr"
                                     << QString("Rcvr: Bits/Frame must be >= %1.").arg(PCMConstants::kMinFrameLengthBits);
    // The pattern is checked before the mask, and both before the frame length.
    QTest::newRow("pattern-first")   << "ZZ" << "XYZ" << 1 << "Rcvr" << "Rcvr: Invalid frame sync pattern 'ZZ'.";
    QTest::newRow("mask-before-frame") << "FE6B2840" << "XYZ" << 1 << "Rcvr" << "Rcvr: Invalid frame sync mask 'XYZ'.";
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
