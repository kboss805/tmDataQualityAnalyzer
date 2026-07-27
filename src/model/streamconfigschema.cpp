/**
 * @file streamconfigschema.cpp
 * @brief Implementation of the StreamConfig JSON schema (format + parse).
 */

#include "streamconfigschema.h"

#include <QJsonObject>
#include <QLatin1String>

#include "constants.h"
#include "streamconfig.h"

namespace
{
    constexpr QLatin1String kModeFrameSyncLock("FrameSyncLock");
    constexpr QLatin1String kModeReceiverChannelInfo("ReceiverChannelInfo");

    QString modeToString(StreamMode mode)
    {
        return (mode == StreamMode::FrameSyncLockStats) ? kModeFrameSyncLock
                                                         : kModeReceiverChannelInfo;
    }

    StreamMode modeFromString(const QString& s)
    {
        return (s == kModeFrameSyncLock) ? StreamMode::FrameSyncLockStats
                                          : StreamMode::ReceiverChannelInfo;
    }
}

QJsonObject StreamConfigSchema::toJson(const StreamConfig& config)
{
    QJsonObject sync;
    sync["pattern"]          = config.sync.pattern;
    sync["mask"]             = config.sync.mask;
    sync["bitsInMinorFrame"] = config.sync.bitsInMinorFrame;
    sync["randomized"]       = config.sync.randomized;
    sync["inverted"]         = config.sync.inverted;
    sync["dataRateMbps"]     = config.sync.dataRateMbps;

    QJsonObject obj;
    obj["pcmChannelId"]      = config.pcmChannelId;
    obj["label"]             = config.label;
    obj["process"]           = config.process;
    obj["mode"]              = modeToString(config.mode);
    obj["sync"]              = sync;
    obj["samplePeriodIndex"] = config.samplePeriodIndex;
    obj["tmatsDataRateMbps"] = config.tmatsDataRateMbps;
    obj["polarityIndex"]     = config.polarityIndex;
    obj["slopeIndex"]        = config.slopeIndex;
    obj["scaleDdBPerV"]      = config.scaleDdBPerV;
    obj["numReceivers"]      = config.numReceivers;
    obj["receiverChannels"]  = config.receiverChannels;
    obj["receiverParamsToml"] = config.receiverParamsToml;

    // Calibration input references only -- never the extracted profiles
    // (calibrationByWord is runtime-only; see StreamConfig's own doc comment).
    if (!config.calCh10Path.isEmpty())
    {
        QJsonObject calibration;
        calibration["calCh10Path"]  = config.calCh10Path;
        calibration["stepTomlPath"] = config.stepTomlPath;
        calibration["clipStartSec"] = config.clipStartSec;
        calibration["clipEndSec"]   = config.clipEndSec;
        obj["calibration"] = calibration;
    }

    return obj;
}

bool StreamConfigSchema::fromJson(const QJsonObject& json, StreamConfig& out)
{
    if (!json.contains("pcmChannelId") || !json.contains("label"))
        return false;

    out.pcmChannelId = json["pcmChannelId"].toInt(out.pcmChannelId);
    out.label        = json["label"].toString(out.label);
    out.process       = json["process"].toBool(out.process);
    out.mode          = modeFromString(json["mode"].toString(modeToString(out.mode)));

    if (json.contains("sync") && json["sync"].isObject())
    {
        const QJsonObject sync = json["sync"].toObject();
        out.sync.pattern          = sync["pattern"].toString(out.sync.pattern);
        out.sync.mask             = sync["mask"].toString(out.sync.mask);
        out.sync.bitsInMinorFrame = sync["bitsInMinorFrame"].toInt(out.sync.bitsInMinorFrame);
        out.sync.randomized       = sync["randomized"].toBool(out.sync.randomized);
        out.sync.inverted         = sync["inverted"].toBool(out.sync.inverted);
        out.sync.dataRateMbps     = sync["dataRateMbps"].toDouble(out.sync.dataRateMbps);
    }

    // Combo-box indices are clamped to their documented range on the way in. A
    // template written by a newer version, or hand-edited, could otherwise carry
    // an index this build doesn't know: samplePeriodIndex feeds a switch whose
    // default silently substitutes 100 ms, and slopeIndex indexes the voltage
    // bound tables. Clamping keeps a bad value from quietly changing the sample
    // rate or the volts-to-dB conversion.
    out.samplePeriodIndex  = qBound(0, json["samplePeriodIndex"].toInt(out.samplePeriodIndex),
                                    UIConstants::kMaxSamplePeriodIndex);
    out.tmatsDataRateMbps  = json["tmatsDataRateMbps"].toDouble(out.tmatsDataRateMbps);
    out.polarityIndex      = json["polarityIndex"].toInt(out.polarityIndex);
    out.slopeIndex         = qBound(0, json["slopeIndex"].toInt(out.slopeIndex),
                                    UIConstants::kMaxSlopeIndex);
    out.scaleDdBPerV       = json["scaleDdBPerV"].toDouble(out.scaleDdBPerV);
    out.numReceivers       = json["numReceivers"].toInt(out.numReceivers);
    out.receiverChannels   = json["receiverChannels"].toInt(out.receiverChannels);
    out.receiverParamsToml = json["receiverParamsToml"].toString(out.receiverParamsToml);

    if (json.contains("calibration") && json["calibration"].isObject())
    {
        const QJsonObject calibration = json["calibration"].toObject();
        out.calCh10Path  = calibration["calCh10Path"].toString(out.calCh10Path);
        out.stepTomlPath = calibration["stepTomlPath"].toString(out.stepTomlPath);
        out.clipStartSec = calibration["clipStartSec"].toDouble(out.clipStartSec);
        out.clipEndSec   = calibration["clipEndSec"].toDouble(out.clipEndSec);
    }

    return true;
}
