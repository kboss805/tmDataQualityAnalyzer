/**
 * @file sessionschema.cpp
 * @brief Implementation of the Session JSON schema (format + parse + path
 *        relativization).
 */

#include "sessionschema.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "session.h"
#include "streamconfigschema.h"

QString SessionSchema::sessionRelativePath(const QString& filePath, const QString& sessionDir)
{
    if (sessionDir.isEmpty() || QFileInfo(filePath).isRelative())
        return filePath;

    const QString rel = QDir(sessionDir).relativeFilePath(filePath);
    // QDir::relativeFilePath() returns the input unchanged (absolute) when the
    // two paths don't share a drive/root (Windows) -- detect that and keep the
    // absolute form rather than storing a bogus relative path.
    return QDir::isAbsolutePath(rel) ? filePath : rel;
}

QString SessionSchema::resolveSessionPath(const QString& storedPath, const QString& sessionDir)
{
    if (storedPath.isEmpty() || sessionDir.isEmpty() || QDir::isAbsolutePath(storedPath))
        return storedPath;
    return QDir(sessionDir).absoluteFilePath(storedPath);
}

QJsonDocument SessionSchema::toJson(const Session& session, const QString& sessionDir)
{
    QJsonArray sources_json;
    for (const Source& src : session.sources)
    {
        QJsonArray streams_json;
        for (const StreamConfig& sc : src.streamConfigs)
            streams_json.append(StreamConfigSchema::toJson(sc));

        QJsonObject source_obj;
        source_obj["filepath"]         = sessionRelativePath(src.filepath, sessionDir);
        source_obj["timeChannelIndex"] = src.timeChannelIndex;
        source_obj["streams"]          = streams_json;
        sources_json.append(source_obj);
    }

    QJsonObject view_state;
    view_state["plotTitle"]    = session.viewState.plotTitle;
    view_state["lockAxisView"] = session.viewState.lockAxisView;
    view_state["leftYMaxOverride"]  = session.viewState.hasLeftYMaxOverride
        ? QJsonValue(session.viewState.leftYMaxOverride) : QJsonValue();
    view_state["rightYMaxOverride"] = session.viewState.hasRightYMaxOverride
        ? QJsonValue(session.viewState.rightYMaxOverride) : QJsonValue();

    QJsonObject root;
    root["schemaVersion"] = kCurrentSchemaVersion;
    root["appVersion"]    = session.appVersion;
    root["sources"]       = sources_json;
    root["viewState"]     = view_state;

    return QJsonDocument(root);
}

SessionSchema::LoadStatus SessionSchema::fromJson(const QJsonDocument& doc, Session& out)
{
    if (!doc.isObject())
        return LoadStatus::InvalidFormat;

    const QJsonObject root = doc.object();

    if (!root.contains("schemaVersion") || !root["schemaVersion"].isDouble())
        return LoadStatus::UnsupportedSchemaVersion;
    const int version = root["schemaVersion"].toInt();
    if (version > kCurrentSchemaVersion)
        return LoadStatus::UnsupportedSchemaVersion;

    if (!root.contains("sources") || !root["sources"].isArray())
        return LoadStatus::InvalidFormat;

    out.schemaVersion = version;
    out.appVersion    = root["appVersion"].toString();
    out.sources.clear();

    for (const QJsonValue& source_val : root["sources"].toArray())
    {
        if (!source_val.isObject())
            continue;
        const QJsonObject source_obj = source_val.toObject();

        Source src;
        src.filepath         = source_obj["filepath"].toString();
        src.timeChannelIndex = source_obj["timeChannelIndex"].toInt();

        if (source_obj.contains("streams") && source_obj["streams"].isArray())
        {
            for (const QJsonValue& stream_val : source_obj["streams"].toArray())
            {
                if (!stream_val.isObject())
                    continue;
                StreamConfig sc;
                if (StreamConfigSchema::fromJson(stream_val.toObject(), sc))
                    src.streamConfigs.append(sc);
            }
        }
        out.sources.append(src);
    }

    if (root.contains("viewState") && root["viewState"].isObject())
    {
        const QJsonObject vs = root["viewState"].toObject();
        out.viewState.plotTitle    = vs["plotTitle"].toString();
        out.viewState.lockAxisView = vs["lockAxisView"].toString(out.viewState.lockAxisView);

        if (vs.contains("leftYMaxOverride") && vs["leftYMaxOverride"].isDouble())
        {
            out.viewState.hasLeftYMaxOverride = true;
            out.viewState.leftYMaxOverride    = vs["leftYMaxOverride"].toDouble();
        }
        if (vs.contains("rightYMaxOverride") && vs["rightYMaxOverride"].isDouble())
        {
            out.viewState.hasRightYMaxOverride = true;
            out.viewState.rightYMaxOverride    = vs["rightYMaxOverride"].toDouble();
        }
    }

    return LoadStatus::Success;
}
