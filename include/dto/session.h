/**
 * @file session.h
 * @brief A whole multi-source analysis configuration, as saved/loaded by
 *        Phase 6 (docs/session-save-load-design.md).
 *
 * A Session stores the *configuration to reproduce* the plot, not processed
 * data (§1) -- loading one replays the Phase 1 add-source pipeline over
 * @c sources in order. @c Source::sourceId is NOT part of the persisted
 * format: ids are assigned fresh at load time by MainViewModel, exactly as
 * they are for a freshly opened/added file.
 */

#ifndef SESSION_H
#define SESSION_H

#include <QString>
#include <QVector>

#include "sessionviewstate.h"
#include "source.h"

struct Session
{
    int    schemaVersion = 1;      ///< See SessionSchema::kCurrentSchemaVersion.
    QString appVersion;            ///< Informational only; not a load gate.
    QVector<Source> sources;
    SessionViewState viewState;
};

#endif // SESSION_H
