/**
 * @file tst_processingtemplateschema.cpp
 * @brief Tests for ProcessingTemplateSchema (Batch Apply).
 *
 * Pure model/serialization tests -- no .ch10 fixture, run in CI.
 */

#include "tst_processingtemplateschema.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "processingtemplate.h"
#include "processingtemplateschema.h"

namespace
{
    TemplateStreamEntry makeLockEntry(int pcmChannelId)
    {
        TemplateStreamEntry entry;
        entry.config.pcmChannelId      = pcmChannelId;
        entry.config.label             = QString("Ch %1").arg(pcmChannelId);
        entry.config.process           = true;
        entry.config.mode              = StreamMode::FrameSyncLockStats;
        entry.config.sync.pattern      = "FE6B2840";
        entry.config.sync.bitsInMinorFrame = 1024;
        return entry;
    }
}

void TestProcessingTemplateSchema::roundTripFrameSyncLockEntry()
{
    ProcessingTemplate in;
    in.appVersion       = "2.7.0";
    in.name             = "PRN11 lock template";
    in.timeChannelIndex = 3;
    in.entries.append(makeLockEntry(32));

    const QJsonDocument doc = ProcessingTemplateSchema::toJson(in);

    ProcessingTemplate out;
    QCOMPARE(int(ProcessingTemplateSchema::fromJson(doc, out)),
             int(ProcessingTemplateSchema::LoadStatus::Success));

    QCOMPARE(out.name, in.name);
    QCOMPARE(out.timeChannelIndex, in.timeChannelIndex);
    QCOMPARE(out.entries.size(), 1);
    QCOMPARE(out.entries[0].config.pcmChannelId, 32);
    QCOMPARE(int(out.entries[0].config.mode), int(StreamMode::FrameSyncLockStats));
    QCOMPARE(out.entries[0].config.sync.pattern, QString("FE6B2840"));
    QCOMPARE(out.entries[0].config.sync.bitsInMinorFrame, 1024);
}

void TestProcessingTemplateSchema::roundTripReceiverChannelInfoEntry()
{
    ProcessingTemplate in;
    TemplateStreamEntry entry;
    entry.config.pcmChannelId     = 40;
    entry.config.label            = "Ch 40";
    entry.config.mode             = StreamMode::ReceiverChannelInfo;
    entry.config.polarityIndex    = 1;
    entry.config.slopeIndex       = 2;
    entry.config.scaleDdBPerV     = 12.5;
    entry.config.numReceivers     = 4;
    entry.config.receiverChannels = 3;
    in.entries.append(entry);

    const QJsonDocument doc = ProcessingTemplateSchema::toJson(in);

    ProcessingTemplate out;
    QCOMPARE(int(ProcessingTemplateSchema::fromJson(doc, out)),
             int(ProcessingTemplateSchema::LoadStatus::Success));

    const StreamConfig& cfg = out.entries[0].config;
    QCOMPARE(int(cfg.mode), int(StreamMode::ReceiverChannelInfo));
    QCOMPARE(cfg.polarityIndex, 1);
    QCOMPARE(cfg.slopeIndex, 2);
    QCOMPARE(cfg.scaleDdBPerV, 12.5);
    QCOMPARE(cfg.numReceivers, 4);
    QCOMPARE(cfg.receiverChannels, 3);
}

void TestProcessingTemplateSchema::roundTripPreservesAppearance()
{
    ProcessingTemplate in;
    TemplateStreamEntry entry = makeLockEntry(32);

    SeriesAppearance lockAppearance;
    lockAppearance.metricType    = PlotSeriesData::MetricType::FrameSyncLock;
    lockAppearance.receiverIndex = 0;
    lockAppearance.channelIndex  = 0;
    lockAppearance.name          = "My Lock Curve";
    lockAppearance.color         = QColor("#ff8800");
    entry.appearance.append(lockAppearance);

    SeriesAppearance snrAppearance;
    snrAppearance.metricType    = PlotSeriesData::MetricType::SNR;
    snrAppearance.receiverIndex = 2;
    snrAppearance.channelIndex  = 1;
    snrAppearance.name          = "R2 C1";
    snrAppearance.color         = QColor("#3366cc");
    entry.appearance.append(snrAppearance);

    in.entries.append(entry);

    const QJsonDocument doc = ProcessingTemplateSchema::toJson(in);

    ProcessingTemplate out;
    QCOMPARE(int(ProcessingTemplateSchema::fromJson(doc, out)),
             int(ProcessingTemplateSchema::LoadStatus::Success));

    QCOMPARE(out.entries[0].appearance.size(), 2);

    const SeriesAppearance& a0 = out.entries[0].appearance[0];
    QCOMPARE(int(a0.metricType), int(PlotSeriesData::MetricType::FrameSyncLock));
    QCOMPARE(a0.receiverIndex, 0);
    QCOMPARE(a0.channelIndex, 0);
    QCOMPARE(a0.name, QString("My Lock Curve"));
    QCOMPARE(a0.color, QColor("#ff8800"));

    const SeriesAppearance& a1 = out.entries[0].appearance[1];
    QCOMPARE(int(a1.metricType), int(PlotSeriesData::MetricType::SNR));
    QCOMPARE(a1.receiverIndex, 2);
    QCOMPARE(a1.channelIndex, 1);
    QCOMPARE(a1.name, QString("R2 C1"));
    QCOMPARE(a1.color, QColor("#3366cc"));
}

void TestProcessingTemplateSchema::toJsonOmitsAppearanceWhenEmpty()
{
    ProcessingTemplate in;
    in.entries.append(makeLockEntry(32)); // no appearance captured

    const QJsonDocument doc = ProcessingTemplateSchema::toJson(in);
    const QJsonObject entry0 = doc.object()["entries"].toArray()[0].toObject();
    QVERIFY(!entry0.contains("appearance"));
}

void TestProcessingTemplateSchema::roundTripPreservesCalibrationInputReferences()
{
    ProcessingTemplate in;
    TemplateStreamEntry entry;
    entry.config.pcmChannelId = 40;
    entry.config.label        = "Ch 40";
    entry.config.calCh10Path  = "cal/rnrz-l_testfile.ch10";
    entry.config.stepTomlPath = "cal/steps.toml";
    entry.config.clipStartSec = 2.5;
    entry.config.clipEndSec   = 1.0;
    in.entries.append(entry);

    const QJsonDocument doc = ProcessingTemplateSchema::toJson(in);

    ProcessingTemplate out;
    QCOMPARE(int(ProcessingTemplateSchema::fromJson(doc, out)),
             int(ProcessingTemplateSchema::LoadStatus::Success));

    const StreamConfig& cfg = out.entries[0].config;
    QCOMPARE(cfg.calCh10Path, QString("cal/rnrz-l_testfile.ch10"));
    QCOMPARE(cfg.stepTomlPath, QString("cal/steps.toml"));
    QCOMPARE(cfg.clipStartSec, 2.5);
    QCOMPARE(cfg.clipEndSec, 1.0);
}

void TestProcessingTemplateSchema::toJsonNeverIncludesCalibrationByWord()
{
    ProcessingTemplate in;
    TemplateStreamEntry entry;
    entry.config.pcmChannelId = 40;
    entry.config.label        = "Ch 40";
    entry.config.calCh10Path  = "cal/rnrz-l_testfile.ch10";

    CalibrationProfile profile;
    profile.valid = true;
    profile.points.append({ 100.0, 0.0 });
    profile.points.append({ 200.0, 10.0 });
    entry.config.calibrationByWord.insert(6, profile);
    in.entries.append(entry);

    const QJsonDocument doc = ProcessingTemplateSchema::toJson(in);
    const QJsonObject config0 =
        doc.object()["entries"].toArray()[0].toObject()["config"].toObject();

    // The extracted profile is session-only and must never be serialized --
    // this guarantee is inherited from StreamConfigSchema.
    QVERIFY(!config0.contains("calibrationByWord"));
    QVERIFY(config0.contains("calibration"));
    QVERIFY(!config0["calibration"].toObject().contains("points"));
}

void TestProcessingTemplateSchema::fromJsonAcceptsCurrentSchemaVersion()
{
    ProcessingTemplate in;
    in.entries.append(makeLockEntry(32));

    const QJsonDocument doc = ProcessingTemplateSchema::toJson(in);
    QCOMPARE(doc.object()["schemaVersion"].toInt(),
             ProcessingTemplateSchema::kCurrentSchemaVersion);

    ProcessingTemplate out;
    QCOMPARE(int(ProcessingTemplateSchema::fromJson(doc, out)),
             int(ProcessingTemplateSchema::LoadStatus::Success));
}

void TestProcessingTemplateSchema::fromJsonRejectsNewerSchemaVersion()
{
    QJsonObject root;
    root["schemaVersion"] = ProcessingTemplateSchema::kCurrentSchemaVersion + 1;
    root["entries"] = QJsonArray();

    ProcessingTemplate out;
    QCOMPARE(int(ProcessingTemplateSchema::fromJson(QJsonDocument(root), out)),
             int(ProcessingTemplateSchema::LoadStatus::UnsupportedSchemaVersion));
}

void TestProcessingTemplateSchema::fromJsonRejectsMissingSchemaVersion()
{
    QJsonObject root;
    root["entries"] = QJsonArray();

    ProcessingTemplate out;
    QCOMPARE(int(ProcessingTemplateSchema::fromJson(QJsonDocument(root), out)),
             int(ProcessingTemplateSchema::LoadStatus::UnsupportedSchemaVersion));
}

void TestProcessingTemplateSchema::fromJsonRejectsMissingEntriesArray()
{
    QJsonObject root;
    root["schemaVersion"] = ProcessingTemplateSchema::kCurrentSchemaVersion;

    ProcessingTemplate out;
    QCOMPARE(int(ProcessingTemplateSchema::fromJson(QJsonDocument(root), out)),
             int(ProcessingTemplateSchema::LoadStatus::InvalidFormat));
}

void TestProcessingTemplateSchema::fromJsonRejectsNonObjectDocument()
{
    const QJsonDocument doc = QJsonDocument::fromVariant(QVariantList{ 1, 2, 3 });

    ProcessingTemplate out;
    QCOMPARE(int(ProcessingTemplateSchema::fromJson(doc, out)),
             int(ProcessingTemplateSchema::LoadStatus::InvalidFormat));
}

void TestProcessingTemplateSchema::swapBytesRoundTripsAndDefaultsTrue()
{
    // A batch is one vendor's recordings, so byte order travels with the template
    // exactly as timeChannelIndex does.
    ProcessingTemplate in;
    in.name             = "safran";
    in.timeChannelIndex = 1;
    in.swapBytes        = false;

    ProcessingTemplate out;
    QCOMPARE(int(ProcessingTemplateSchema::fromJson(ProcessingTemplateSchema::toJson(in), out)),
             int(ProcessingTemplateSchema::LoadStatus::Success));
    QCOMPARE(out.swapBytes, false);

    // A template written before this field existed carries no "swapBytes" key. Those
    // came from a build that always swapped, so absent must read as true - anything
    // else would silently change how an existing template processes its files.
    QJsonObject legacy = ProcessingTemplateSchema::toJson(in).object();
    legacy.remove("swapBytes");
    ProcessingTemplate from_legacy;
    QCOMPARE(int(ProcessingTemplateSchema::fromJson(QJsonDocument(legacy), from_legacy)),
             int(ProcessingTemplateSchema::LoadStatus::Success));
    QCOMPARE(from_legacy.swapBytes, true);
}
