/**
 * @file tst_sessionschema.cpp
 * @brief Tests for SessionSchema (session save/load, Phase 6, build order §12.2).
 *
 * Pure model/serialization tests -- no .ch10 fixture, run in CI (design doc §9).
 */

#include "tst_sessionschema.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "session.h"
#include "sessionschema.h"

namespace
{
    Source makeSource(const QString& filepath, int timeChannelIndex, int pcmChannelId)
    {
        Source src;
        src.filepath         = filepath;
        src.timeChannelIndex = timeChannelIndex;

        StreamConfig sc;
        sc.pcmChannelId = pcmChannelId;
        sc.label        = QString("Ch %1").arg(pcmChannelId);
        sc.process      = true;
        src.streamConfigs.append(sc);
        return src;
    }
}

void TestSessionSchema::roundTripTwoSourceMixedModeSession()
{
    Session in;
    in.appVersion = "2.7.0";
    in.sources.append(makeSource("data/one.ch10", 1, 32));
    in.sources[0].streamConfigs[0].mode = StreamMode::FrameSyncLockStats;
    in.sources.append(makeSource("data/two.ch10", 2, 40));
    in.sources[1].streamConfigs[0].mode = StreamMode::ReceiverChannelInfo;
    in.sources[1].streamConfigs[0].receiverParamsToml = "settings/receiver_params/default.toml";

    const QJsonDocument doc = SessionSchema::toJson(in, QString());

    Session out;
    QCOMPARE(int(SessionSchema::fromJson(doc, out)), int(SessionSchema::LoadStatus::Success));

    QCOMPARE(out.appVersion, in.appVersion);
    QCOMPARE(out.sources.size(), 2);
    QCOMPARE(out.sources[0].filepath, in.sources[0].filepath);
    QCOMPARE(out.sources[0].timeChannelIndex, in.sources[0].timeChannelIndex);
    QCOMPARE(out.sources[0].streamConfigs.size(), 1);
    QCOMPARE(out.sources[0].streamConfigs[0].pcmChannelId, 32);
    QCOMPARE(int(out.sources[0].streamConfigs[0].mode), int(StreamMode::FrameSyncLockStats));
    QCOMPARE(out.sources[1].streamConfigs[0].pcmChannelId, 40);
    QCOMPARE(int(out.sources[1].streamConfigs[0].mode), int(StreamMode::ReceiverChannelInfo));
    QCOMPARE(out.sources[1].streamConfigs[0].receiverParamsToml,
             in.sources[1].streamConfigs[0].receiverParamsToml);
}

void TestSessionSchema::roundTripPreservesViewStateOverrides()
{
    Session in;
    in.viewState.plotTitle    = "Flight 42";
    in.viewState.lockAxisView = "MissedFrames";
    in.viewState.hasLeftYMaxOverride  = true;
    in.viewState.leftYMaxOverride     = 80.0;
    in.viewState.hasRightYMaxOverride = true;
    in.viewState.rightYMaxOverride    = 15.5;

    const QJsonDocument doc = SessionSchema::toJson(in, QString());

    Session out;
    QCOMPARE(int(SessionSchema::fromJson(doc, out)), int(SessionSchema::LoadStatus::Success));
    QCOMPARE(out.viewState.plotTitle, in.viewState.plotTitle);
    QCOMPARE(out.viewState.lockAxisView, in.viewState.lockAxisView);
    QVERIFY(out.viewState.hasLeftYMaxOverride);
    QCOMPARE(out.viewState.leftYMaxOverride, in.viewState.leftYMaxOverride);
    QVERIFY(out.viewState.hasRightYMaxOverride);
    QCOMPARE(out.viewState.rightYMaxOverride, in.viewState.rightYMaxOverride);
}

void TestSessionSchema::roundTripOmittedOverridesStayUnset()
{
    Session in; // no axis overrides set

    const QJsonDocument doc = SessionSchema::toJson(in, QString());

    Session out;
    QCOMPARE(int(SessionSchema::fromJson(doc, out)), int(SessionSchema::LoadStatus::Success));
    QVERIFY(!out.viewState.hasLeftYMaxOverride);
    QVERIFY(!out.viewState.hasRightYMaxOverride);
}

void TestSessionSchema::fromJsonWritesCurrentSchemaVersion()
{
    const QJsonDocument doc = SessionSchema::toJson(Session(), QString());
    QCOMPARE(doc.object()["schemaVersion"].toInt(), SessionSchema::kCurrentSchemaVersion);
}

void TestSessionSchema::fromJsonRejectsNewerSchemaVersion()
{
    QJsonObject root;
    root["schemaVersion"] = SessionSchema::kCurrentSchemaVersion + 1;
    root["sources"] = QJsonArray();

    Session out;
    QCOMPARE(int(SessionSchema::fromJson(QJsonDocument(root), out)),
             int(SessionSchema::LoadStatus::UnsupportedSchemaVersion));
}

void TestSessionSchema::fromJsonRejectsMissingSchemaVersion()
{
    QJsonObject root;
    root["sources"] = QJsonArray();

    Session out;
    QCOMPARE(int(SessionSchema::fromJson(QJsonDocument(root), out)),
             int(SessionSchema::LoadStatus::UnsupportedSchemaVersion));
}

void TestSessionSchema::fromJsonRejectsMissingSourcesArray()
{
    QJsonObject root;
    root["schemaVersion"] = SessionSchema::kCurrentSchemaVersion;

    Session out;
    QCOMPARE(int(SessionSchema::fromJson(QJsonDocument(root), out)),
             int(SessionSchema::LoadStatus::InvalidFormat));
}

void TestSessionSchema::fromJsonRejectsNonObjectDocument()
{
    const QJsonDocument doc = QJsonDocument::fromVariant(QVariantList{ 1, 2, 3 });

    Session out;
    QCOMPARE(int(SessionSchema::fromJson(doc, out)), int(SessionSchema::LoadStatus::InvalidFormat));
}

void TestSessionSchema::sessionRelativePathRelativizesUnderSameDrive()
{
    const QString rel = SessionSchema::sessionRelativePath(
        "C:/analysis/2026/data/file1.ch10", "C:/analysis/2026");
    QCOMPARE(rel, QString("data/file1.ch10"));
}

void TestSessionSchema::sessionRelativePathKeepsAbsoluteWhenOutsideSessionDir()
{
    // Cross-drive on Windows can't be expressed as a relative path -- Qt keeps
    // it absolute in that case, and sessionRelativePath must not corrupt it.
    const QString rel = SessionSchema::sessionRelativePath(
        "D:/data/file1.ch10", "C:/analysis/2026");
    QCOMPARE(rel, QString("D:/data/file1.ch10"));
}

void TestSessionSchema::resolveSessionPathResolvesRelativeAgainstSessionDir()
{
    const QString resolved = SessionSchema::resolveSessionPath(
        "data/file1.ch10", "C:/analysis/2026");
    QCOMPARE(resolved, QString("C:/analysis/2026/data/file1.ch10"));
}

void TestSessionSchema::resolveSessionPathLeavesAbsoluteUnchanged()
{
    const QString resolved = SessionSchema::resolveSessionPath(
        "D:/data/file1.ch10", "C:/analysis/2026");
    QCOMPARE(resolved, QString("D:/data/file1.ch10"));
}

void TestSessionSchema::relativizeThenResolveRoundTrips()
{
    const QString session_dir = "C:/analysis/2026";
    const QString original    = "C:/analysis/2026/data/sub/file1.ch10";

    const QString rel      = SessionSchema::sessionRelativePath(original, session_dir);
    const QString resolved = SessionSchema::resolveSessionPath(rel, session_dir);
    QCOMPARE(resolved, original);
}
