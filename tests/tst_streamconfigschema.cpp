/**
 * @file tst_streamconfigschema.cpp
 * @brief Tests for StreamConfigSchema (session save/load, Phase 6, build order §12.1).
 */

#include "tst_streamconfigschema.h"

#include <QJsonObject>
#include <QtTest>

#include "streamconfig.h"
#include "streamconfigschema.h"

void TestStreamConfigSchema::roundTripFrameSyncLockConfig()
{
    StreamConfig in;
    in.pcmChannelId      = 32;
    in.label             = "Ch 32";
    in.process           = true;
    in.mode              = StreamMode::FrameSyncLockStats;
    in.sync.pattern      = "FE6B2840";
    in.sync.mask         = "FFFFFFFF";
    in.sync.bitsInMinorFrame = 1024;
    in.sync.randomized   = true;
    in.sync.inverted     = false;
    in.sync.dataRateMbps = 1.5;
    in.samplePeriodIndex = 2;
    in.tmatsDataRateMbps = 10.0;

    const QJsonObject json = StreamConfigSchema::toJson(in);

    StreamConfig out;
    QVERIFY(StreamConfigSchema::fromJson(json, out));

    QCOMPARE(out.pcmChannelId, in.pcmChannelId);
    QCOMPARE(out.label, in.label);
    QCOMPARE(out.process, in.process);
    QCOMPARE(int(out.mode), int(in.mode));
    QCOMPARE(out.sync.pattern, in.sync.pattern);
    QCOMPARE(out.sync.mask, in.sync.mask);
    QCOMPARE(out.sync.bitsInMinorFrame, in.sync.bitsInMinorFrame);
    QCOMPARE(out.sync.randomized, in.sync.randomized);
    QCOMPARE(out.sync.inverted, in.sync.inverted);
    QCOMPARE(out.sync.dataRateMbps, in.sync.dataRateMbps);
    QCOMPARE(out.samplePeriodIndex, in.samplePeriodIndex);
    QCOMPARE(out.tmatsDataRateMbps, in.tmatsDataRateMbps);
}

void TestStreamConfigSchema::roundTripReceiverChannelInfoConfig()
{
    StreamConfig in;
    in.pcmChannelId      = 40;
    in.label             = "Ch 40";
    in.process           = true;
    in.mode              = StreamMode::ReceiverChannelInfo;
    in.sync.pattern      = "FE6B2840";
    in.sync.mask         = "FFFFFFFF";
    in.sync.bitsInMinorFrame = 800;
    in.sync.inverted     = true;
    in.polarityIndex     = 1;
    in.slopeIndex        = 2;
    in.scaleDdBPerV      = 12.5;
    in.numReceivers      = 4;
    in.receiverChannels  = 3;
    in.receiverParamsToml = "settings/receiver_params/default.toml";

    const QJsonObject json = StreamConfigSchema::toJson(in);

    StreamConfig out;
    QVERIFY(StreamConfigSchema::fromJson(json, out));

    QCOMPARE(int(out.mode), int(in.mode));
    QCOMPARE(out.polarityIndex, in.polarityIndex);
    QCOMPARE(out.slopeIndex, in.slopeIndex);
    QCOMPARE(out.scaleDdBPerV, in.scaleDdBPerV);
    QCOMPARE(out.numReceivers, in.numReceivers);
    QCOMPARE(out.receiverChannels, in.receiverChannels);
    QCOMPARE(out.receiverParamsToml, in.receiverParamsToml);
}

void TestStreamConfigSchema::roundTripPreservesCalibrationInputReferences()
{
    StreamConfig in;
    in.pcmChannelId = 40;
    in.label        = "Ch 40";
    in.calCh10Path  = "cal/rnrz-l_testfile.ch10";
    in.stepTomlPath = "cal/steps.toml";
    in.clipStartSec = 2.5;
    in.clipEndSec   = 1.0;

    const QJsonObject json = StreamConfigSchema::toJson(in);

    StreamConfig out;
    QVERIFY(StreamConfigSchema::fromJson(json, out));

    QCOMPARE(out.calCh10Path, in.calCh10Path);
    QCOMPARE(out.stepTomlPath, in.stepTomlPath);
    QCOMPARE(out.clipStartSec, in.clipStartSec);
    QCOMPARE(out.clipEndSec, in.clipEndSec);
}

void TestStreamConfigSchema::toJsonNeverIncludesCalibrationByWord()
{
    StreamConfig in;
    in.pcmChannelId = 40;
    in.label        = "Ch 40";
    in.calCh10Path  = "cal/rnrz-l_testfile.ch10";

    CalibrationProfile profile;
    profile.valid = true;
    profile.points.append({ 100.0, 0.0 });
    profile.points.append({ 200.0, 10.0 });
    in.calibrationByWord.insert(6, profile);

    const QJsonObject json = StreamConfigSchema::toJson(in);

    // The extracted profile itself must never appear in the serialized form --
    // only the calibration *input references* (calCh10Path etc.) do.
    QVERIFY(!json.contains("calibrationByWord"));
    QVERIFY(json.contains("calibration"));
    QVERIFY(!json["calibration"].toObject().contains("points"));
}

void TestStreamConfigSchema::toJsonOmitsCalibrationBlockWhenNoCalCh10Path()
{
    StreamConfig in;
    in.pcmChannelId = 40;
    in.label        = "Ch 40";
    // calCh10Path left empty: no non-linear calibration was ever extracted.

    const QJsonObject json = StreamConfigSchema::toJson(in);
    QVERIFY(!json.contains("calibration"));
}

void TestStreamConfigSchema::fromJsonRejectsObjectMissingRequiredFields()
{
    QJsonObject json;
    json["label"] = "Ch 40"; // pcmChannelId missing

    StreamConfig out;
    QVERIFY(!StreamConfigSchema::fromJson(json, out));
}

void TestStreamConfigSchema::fromJsonLeavesDefaultsWhenOptionalFieldsMissing()
{
    QJsonObject json;
    json["pcmChannelId"] = 40;
    json["label"]        = "Ch 40";

    StreamConfig out;
    const double default_scale = out.scaleDdBPerV;
    QVERIFY(StreamConfigSchema::fromJson(json, out));

    QCOMPARE(out.pcmChannelId, 40);
    QCOMPARE(out.label, QString("Ch 40"));
    QCOMPARE(out.scaleDdBPerV, default_scale);
    QVERIFY(out.calCh10Path.isEmpty());
}
