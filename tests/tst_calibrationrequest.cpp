#include "tst_calibrationrequest.h"

#include <random>
#include <string>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QtTest>

#include "calibrationrequest.h"
#include "constants.h"
#include "framesync.h"
#include "qtbridge.h"

namespace {

// ---------------------------------------------------------------------------
// The code as it was, in the Extract Calibration dialog (src/view/streamsubdialogs.cpp), with its inputs
// taken from a CalibrationInputs instead of the dialog's members. Kept as written (QString and all) so it is
// the oracle for the move; it is not used by the application any more.
// ---------------------------------------------------------------------------

struct OldResult
{
    bool                          ok = false;
    std::string                   error;
    CalibrationExtractor::Request req;
};

OldResult oldInlineCode(const CalibrationInputs& setup, const QString& appRoot)
{
    OldResult result;
    CalibrationExtractor::Request req;
    req.calFilename      = setup.calFilename;
    req.timeChannelId    = setup.timeChannelId;
    req.pcmChannelId     = setup.pcmChannelId;
    req.numReceivers     = setup.numReceivers;
    req.receiverChannels = setup.receiverChannels;
    req.steps            = setup.steps;
    req.clipStartSec     = setup.clipStartSec;
    req.clipEndSec       = setup.clipEndSec;

    Acquisition& acquisition = req.acquisition;
    switch (resolveFrameSync(QString::fromStdString(setup.syncPattern).toStdString(),
                             QString::fromStdString(setup.syncMask).toStdString(), acquisition.sync))
    {
        case FrameSyncError::None:
            break;
        case FrameSyncError::PatternEmpty:
        case FrameSyncError::PatternInvalid:
            result.error = QString("Invalid frame sync pattern '%1'.").arg(QString::fromStdString(setup.syncPattern)).toStdString();
            return result;
        case FrameSyncError::MaskInvalid:
            result.error = QString("Invalid frame sync mask '%1'.").arg(QString::fromStdString(setup.syncMask)).toStdString();
            return result;
    }
    acquisition.bitsInMinorFrame = setup.bitsInMinorFrame;
    acquisition.randomized       = setup.randomized;
    acquisition.inverted         = setup.inverted;
    acquisition.dataRateMbps     = setup.dataRateMbps;
    acquisition.swapBytes        = setup.swapBytes;

    QString receiver_params = QString::fromStdString(setup.receiverParamsToml.string());
    if (receiver_params.isEmpty())
    {
        const QString defaultRcvrParams = appRoot + "/" + UIConstants::kSettingsDirName +
            "/" + UIConstants::kReceiverParamsDirName + "/" + UIConstants::kDefaultTomlFilename;
        if (QFileInfo::exists(defaultRcvrParams))
        {
            receiver_params = defaultRcvrParams;
        }
    }
    req.receiverParamsToml = toFsPath(receiver_params);

    result.ok  = true;
    result.req = req;
    return result;
}

/// Every field of a request, as text, so two requests compare in one line.
QString describe(const CalibrationExtractor::Request& r)
{
    QStringList parts;
    parts << "cal=" + QString::fromStdString(r.calFilename.generic_string());
    parts << "time=" + QString::number(r.timeChannelId) << "pcm=" + QString::number(r.pcmChannelId);
    const Acquisition& a = r.acquisition;
    parts << "pattern=" + QString::number(a.sync.pattern, 16) << "mask=" + QString::number(a.sync.mask, 16)
          << "len=" + QString::number(a.sync.patternLength) << "bits=" + QString::number(a.bitsInMinorFrame)
          << QString("rnd=%1").arg(a.randomized) << QString("inv=%1").arg(a.inverted)
          << "rate=" + QString::number(a.dataRateMbps, 'g', 17) << QString("swap=%1").arg(a.swapBytes);
    parts << "params=" + QString::fromStdString(r.receiverParamsToml.generic_string());
    parts << "rcv=" + QString::number(r.numReceivers) << "ch=" + QString::number(r.receiverChannels);
    QStringList steps;
    for (const StepDefinition& s : r.steps)
        steps << QString::number(s.db, 'g', 17);
    parts << "steps=" + steps.join(",");
    parts << "clip=" + QString::number(r.clipStartSec, 'g', 17) + "," + QString::number(r.clipEndSec, 'g', 17);
    return parts.join(" ");
}

CalibrationInputs typicalSetup()
{
    CalibrationInputs s;
    s.calFilename      = "cal.ch10";
    s.timeChannelId    = 1;
    s.pcmChannelId     = 26;
    s.syncPattern      = "FE6B2840";
    s.bitsInMinorFrame = 800;
    s.dataRateMbps     = 0.8;
    s.numReceivers     = 16;
    s.receiverChannels = 3;
    s.steps            = { {0.0}, {10.0}, {20.0} };
    s.clipStartSec     = 1.5;
    s.clipEndSec       = 2.5;
    return s;
}

/// An application root, with or without the shipped default word map.
struct AppRoot
{
    QTemporaryDir dir;
    explicit AppRoot(bool withDefault)
    {
        if (withDefault)
        {
            QDir().mkpath(dir.filePath("settings/receiver_params"));
            QFile f(dir.filePath("settings/receiver_params/default.toml"));
            if (f.open(QIODevice::WriteOnly))
                f.write("[L_RCVR1]\nWord = 1\n");
        }
    }
    std::string path() const { return dir.path().toStdString(); }
};

} // namespace

void TestCalibrationRequest::theSyncTextIsDecodedLikeTheMainRun()
{
    const AppRoot root(false);
    CalibrationExtractor::Request req;
    std::string error;

    CalibrationInputs s = typicalSetup();
    QVERIFY2(buildCalibrationRequest(s, root.path(), req, error), error.c_str());
    QCOMPARE(req.acquisition.sync.pattern, std::uint64_t{0xFE6B2840});
    QCOMPARE(req.acquisition.sync.patternLength, 32);
    QCOMPARE(req.acquisition.sync.mask, std::uint64_t{0xFFFFFFFF});   // no mask: every bit of the pattern

    // The length is the CHARACTER count times four, whatever the characters are.
    s.syncPattern = " FE6B ";
    QVERIFY2(buildCalibrationRequest(s, root.path(), req, error), error.c_str());
    QCOMPARE(req.acquisition.sync.patternLength, 24);
    QCOMPARE(req.acquisition.sync.pattern, std::uint64_t{0xFE6B});

    s.syncPattern = "FE6B2840";
    s.syncMask    = "FFFF0000";
    QVERIFY2(buildCalibrationRequest(s, root.path(), req, error), error.c_str());
    QCOMPARE(req.acquisition.sync.mask, std::uint64_t{0xFFFF0000});
}

void TestCalibrationRequest::aSyncPatternOrMaskThatCannotBeReadIsRefusedWithTheDialogsMessage()
{
    const AppRoot root(false);
    CalibrationExtractor::Request req;
    std::string error;

    CalibrationInputs s = typicalSetup();
    s.syncPattern = "";
    QVERIFY(!buildCalibrationRequest(s, root.path(), req, error));
    QCOMPARE(error, std::string("Invalid frame sync pattern ''."));

    s.syncPattern = "not hex";
    QVERIFY(!buildCalibrationRequest(s, root.path(), req, error));
    QCOMPARE(error, std::string("Invalid frame sync pattern 'not hex'."));

    s.syncPattern = "FFFFFFFFFFFFFFFFF";   // wider than 64 bits
    QVERIFY(!buildCalibrationRequest(s, root.path(), req, error));
    QCOMPARE(error, std::string("Invalid frame sync pattern 'FFFFFFFFFFFFFFFFF'."));

    s.syncPattern = "FE6B2840";
    s.syncMask    = "xyz";
    QVERIFY(!buildCalibrationRequest(s, root.path(), req, error));
    QCOMPARE(error, std::string("Invalid frame sync mask 'xyz'."));

    // The pattern is checked first.
    s.syncPattern = "zz";
    QVERIFY(!buildCalibrationRequest(s, root.path(), req, error));
    QCOMPARE(error, std::string("Invalid frame sync pattern 'zz'."));
}

void TestCalibrationRequest::aFailedBuildLeavesTheRequestAlone()
{
    const AppRoot root(false);
    CalibrationExtractor::Request req;
    std::string error;
    QVERIFY(buildCalibrationRequest(typicalSetup(), root.path(), req, error));
    const QString before = describe(req);

    CalibrationInputs bad = typicalSetup();
    bad.pcmChannelId = 99;
    bad.syncPattern  = "nope";
    QVERIFY(!buildCalibrationRequest(bad, root.path(), req, error));
    QCOMPARE(describe(req), before);
}

void TestCalibrationRequest::everyOtherFieldIsCarriedAcross()
{
    const AppRoot root(false);
    CalibrationInputs s = typicalSetup();
    s.timeChannelId     = 7;
    s.pcmChannelId      = 9;
    s.randomized        = true;
    s.inverted          = true;
    s.swapBytes         = false;
    s.dataRateMbps      = 1.25;
    s.bitsInMinorFrame  = 8000;
    s.numReceivers      = 4;
    s.receiverChannels  = 2;
    s.clipStartSec      = 0.25;
    s.clipEndSec        = 0.75;
    s.steps             = { {-5.0}, {5.0} };
    s.calFilename       = "dir/some cal.ch10";
    s.receiverParamsToml = "words.toml";

    CalibrationExtractor::Request req;
    std::string error;
    QVERIFY2(buildCalibrationRequest(s, root.path(), req, error), error.c_str());
    QCOMPARE(req.calFilename, std::filesystem::path("dir/some cal.ch10"));
    QCOMPARE(req.timeChannelId, 7);
    QCOMPARE(req.pcmChannelId, 9);
    QVERIFY(req.acquisition.randomized);
    QVERIFY(req.acquisition.inverted);
    QVERIFY(!req.acquisition.swapBytes);
    QCOMPARE(req.acquisition.dataRateMbps, 1.25);
    QCOMPARE(req.acquisition.bitsInMinorFrame, 8000);
    QCOMPARE(req.numReceivers, 4);
    QCOMPARE(req.receiverChannels, 2);
    QCOMPARE(req.clipStartSec, 0.25);
    QCOMPARE(req.clipEndSec, 0.75);
    QCOMPARE(req.steps.size(), std::size_t{2});
    QCOMPARE(req.steps[0].db, -5.0);
    QCOMPARE(req.steps[1].db, 5.0);
    QCOMPARE(req.receiverParamsToml, std::filesystem::path("words.toml"));
}

void TestCalibrationRequest::theDefaultWordMapIsUsedOnlyWhenNoneIsNamedAndTheAppRootHasOne()
{
    const AppRoot withDefault(true);
    const AppRoot without(false);
    CalibrationExtractor::Request req;
    std::string error;
    CalibrationInputs s = typicalSetup();

    // None named, the root has one: it is used.
    QVERIFY(buildCalibrationRequest(s, withDefault.path(), req, error));
    QVERIFY(!req.receiverParamsToml.empty());
    QCOMPARE(req.receiverParamsToml.filename().string(), std::string("default.toml"));
    QVERIFY(std::filesystem::exists(req.receiverParamsToml));

    // None named, the root has none: the request names no file (the extractor builds its grid).
    QVERIFY(buildCalibrationRequest(s, without.path(), req, error));
    QVERIFY(req.receiverParamsToml.empty());

    // A named file wins, whether or not the root has a default - and is not checked for existence here.
    s.receiverParamsToml = "named.toml";
    QVERIFY(buildCalibrationRequest(s, withDefault.path(), req, error));
    QCOMPARE(req.receiverParamsToml, std::filesystem::path("named.toml"));
    QVERIFY(buildCalibrationRequest(s, without.path(), req, error));
    QCOMPARE(req.receiverParamsToml, std::filesystem::path("named.toml"));

    // An empty or unusable app root is no root: nothing is found.
    s.receiverParamsToml.clear();
    QVERIFY(buildCalibrationRequest(s, std::string(), req, error));
    QVERIFY(req.receiverParamsToml.empty());
    QVERIFY(buildCalibrationRequest(s, "Z:/no/such/place/at/all", req, error));
    QVERIFY(req.receiverParamsToml.empty());
}

void TestCalibrationRequest::theNewCodeAgreesWithTheOldInlineCodeOverGeneratedInputs()
{
    const AppRoot withDefault(true);
    const AppRoot without(false);
    const std::vector<std::string> patterns = { "FE6B2840", "", "zz", " FE6B ", "FFFFFFFFFFFFFFFFF", "-1", "0x1F", "+AB", "a345ca5c", "334AABBF" };
    const std::vector<std::string> masks = { "", "FFFF0000", "xyz", "0xFF", "FFFFFFFFFFFFFFFFF", "00000000", " F0 " };
    const std::vector<std::string> params = { "", "words.toml", "dir/other words.toml" };
    const std::vector<std::string> roots = { withDefault.path(), without.path(), std::string(), "Z:/nowhere" };

    std::mt19937 rng(20261022);
    auto pick = [&](const auto& v) -> const auto& { return v[rng() % v.size()]; };

    int compared = 0, accepted = 0, refused = 0, usedDefault = 0;
    for (int i = 0; i < 3000; ++i)
    {
        CalibrationInputs s;
        s.calFilename       = "cal" + std::to_string(rng() % 5) + ".ch10";
        s.timeChannelId     = int(rng() % 4) - 1;
        s.pcmChannelId      = int(rng() % 40);
        s.syncPattern       = pick(patterns);
        s.syncMask          = pick(masks);
        s.bitsInMinorFrame  = int(rng() % 9000);
        s.randomized        = rng() % 2;
        s.inverted          = rng() % 2;
        s.dataRateMbps      = double(rng() % 40) / 8.0;
        s.swapBytes         = rng() % 2;
        s.receiverParamsToml = pick(params);
        s.numReceivers      = int(rng() % 20);
        s.receiverChannels  = int(rng() % 5);
        const int steps = int(rng() % 4);
        for (int k = 0; k < steps; ++k)
            s.steps.push_back({double(int(rng() % 80) - 20) / 2.0});
        s.clipStartSec = double(rng() % 100) / 10.0;
        s.clipEndSec   = double(rng() % 100) / 10.0;
        const std::string& root = pick(roots);

        const OldResult old = oldInlineCode(s, QString::fromStdString(root));
        CalibrationExtractor::Request req;
        std::string error;
        const bool ok = buildCalibrationRequest(s, root, req, error);

        QVERIFY2(ok == old.ok, qPrintable(QString("input %1: ok %2 vs old %3").arg(i).arg(ok).arg(old.ok)));
        if (ok)
        {
            QCOMPARE(describe(req), describe(old.req));
            ++accepted;
            if (s.receiverParamsToml.empty() && !req.receiverParamsToml.empty())
                ++usedDefault;
        }
        else
        {
            QCOMPARE(error, old.error);
            ++refused;
        }
        ++compared;
    }
    // The generator really reached each branch.
    QVERIFY2(accepted > 300 && refused > 300 && usedDefault > 30,
             qPrintable(QString("%1 accepted, %2 refused, %3 used the default").arg(accepted).arg(refused).arg(usedDefault)));
    QCOMPARE(compared, 3000);
}
