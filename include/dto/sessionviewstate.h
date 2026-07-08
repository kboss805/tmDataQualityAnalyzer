/**
 * @file sessionviewstate.h
 * @brief Coarse, series-independent plot view state persisted in a session
 *        (docs/session-save-load-design.md §7 -- v1 restores this much only;
 *        per-series appearance is a documented fast-follow).
 */

#ifndef SESSIONVIEWSTATE_H
#define SESSIONVIEWSTATE_H

#include <QString>

/**
 * @brief The subset of PlotViewModel's view state a session saves/restores.
 *
 * Deliberately a plain DTO decoupled from PlotViewModel's own enum/private
 * override-flag representation, so the session model layer doesn't depend on
 * the viewmodel layer. Phase 6.3 (Save/Open Session UX) is responsible for
 * the two-way mapping to/from live PlotViewModel state.
 */
struct SessionViewState
{
    QString plotTitle;

    /// Mirrors PlotViewModel::LockAxisView as a string: "LockPercent" or
    /// "MissedFrames".
    QString lockAxisView = "LockPercent";

    bool   hasLeftYMaxOverride = false;
    double leftYMaxOverride    = 0.0;
    bool   hasRightYMaxOverride = false;
    double rightYMaxOverride    = 0.0;
};

#endif // SESSIONVIEWSTATE_H
