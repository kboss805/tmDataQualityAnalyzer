/**
 * @file processingtemplateschema.cpp
 * @brief Implementation of the ProcessingTemplate JSON schema (format + parse).
 */

#include "processingtemplateschema.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "processingtemplate.h"
#include "streamconfigschema.h"

namespace
{
    QString metricToString(PlotSeriesData::MetricType m)
    {
        switch (m)
        {
        case PlotSeriesData::MetricType::FrameSyncLock:           return "FrameSyncLock";
        case PlotSeriesData::MetricType::AccumulatedMissedFrames: return "AccumulatedMissedFrames";
        case PlotSeriesData::MetricType::SNR:                     return "SNR";
        }
        return "SNR";
    }

    PlotSeriesData::MetricType metricFromString(const QString& s)
    {
        if (s == "FrameSyncLock")           return PlotSeriesData::MetricType::FrameSyncLock;
        if (s == "AccumulatedMissedFrames") return PlotSeriesData::MetricType::AccumulatedMissedFrames;
        return PlotSeriesData::MetricType::SNR;
    }

    QJsonObject appearanceToJson(const SeriesAppearance& a)
    {
        QJsonObject obj;
        obj["metricType"]    = metricToString(a.metricType);
        obj["receiverIndex"] = a.receiverIndex;
        obj["channelIndex"]  = a.channelIndex;
        obj["name"]          = a.name;
        obj["color"]         = a.color.name(QColor::HexArgb);
        return obj;
    }

    SeriesAppearance appearanceFromJson(const QJsonObject& obj)
    {
        SeriesAppearance a;
        a.metricType    = metricFromString(obj["metricType"].toString());
        a.receiverIndex = obj["receiverIndex"].toInt(a.receiverIndex);
        a.channelIndex  = obj["channelIndex"].toInt(a.channelIndex);
        a.name          = obj["name"].toString(a.name);
        a.color         = QColor(obj["color"].toString());
        return a;
    }
}

QJsonDocument ProcessingTemplateSchema::toJson(const ProcessingTemplate& tmpl)
{
    QJsonArray entries_json;
    for (const TemplateStreamEntry& entry : tmpl.entries)
    {
        QJsonObject entry_obj;
        entry_obj["config"] = StreamConfigSchema::toJson(entry.config);

        // Appearance is optional -- omit the key entirely when nothing was captured.
        if (!entry.appearance.isEmpty())
        {
            QJsonArray appearance_json;
            for (const SeriesAppearance& a : entry.appearance)
                appearance_json.append(appearanceToJson(a));
            entry_obj["appearance"] = appearance_json;
        }
        entries_json.append(entry_obj);
    }

    QJsonObject root;
    root["schemaVersion"]    = kCurrentSchemaVersion;
    root["appVersion"]       = tmpl.appVersion;
    root["name"]             = tmpl.name;
    root["timeChannelIndex"] = tmpl.timeChannelIndex;
    root["entries"]          = entries_json;

    return QJsonDocument(root);
}

ProcessingTemplateSchema::LoadStatus ProcessingTemplateSchema::fromJson(const QJsonDocument& doc,
                                                                        ProcessingTemplate& out)
{
    if (!doc.isObject())
        return LoadStatus::InvalidFormat;

    const QJsonObject root = doc.object();

    if (!root.contains("schemaVersion") || !root["schemaVersion"].isDouble())
        return LoadStatus::UnsupportedSchemaVersion;
    const int version = root["schemaVersion"].toInt();
    if (version > kCurrentSchemaVersion)
        return LoadStatus::UnsupportedSchemaVersion;

    if (!root.contains("entries") || !root["entries"].isArray())
        return LoadStatus::InvalidFormat;

    out.schemaVersion    = version;
    out.appVersion       = root["appVersion"].toString();
    out.name             = root["name"].toString();
    out.timeChannelIndex = root["timeChannelIndex"].toInt(out.timeChannelIndex);
    out.entries.clear();

    for (const QJsonValue& entry_val : root["entries"].toArray())
    {
        if (!entry_val.isObject())
            continue;
        const QJsonObject entry_obj = entry_val.toObject();

        if (!entry_obj.contains("config") || !entry_obj["config"].isObject())
            continue;

        TemplateStreamEntry entry;
        if (!StreamConfigSchema::fromJson(entry_obj["config"].toObject(), entry.config))
            continue;

        if (entry_obj.contains("appearance") && entry_obj["appearance"].isArray())
        {
            for (const QJsonValue& a_val : entry_obj["appearance"].toArray())
            {
                if (a_val.isObject())
                    entry.appearance.append(appearanceFromJson(a_val.toObject()));
            }
        }
        out.entries.append(entry);
    }

    return LoadStatus::Success;
}
