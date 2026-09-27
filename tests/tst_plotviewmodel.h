/**
 * @file tst_plotviewmodel.h
 * @brief Unit tests for PlotViewModel — CSV parsing, series data, axis ranges.
 */

#ifndef TST_PLOTVIEWMODEL_H
#define TST_PLOTVIEWMODEL_H

#include <QObject>

class TestPlotViewModel : public QObject
{
    Q_OBJECT

private slots:
    void defaultState();
    void loadCsvFile();
    void csvTimeConversion();
    void seriesColorAssignment();
    void yAutoRange();
    void snrYMaxOverride();
    void yMinOverridesAndTheSpanGuard();
    void xTimeWindow();
    void seriesVisibility();
    void clearData();
    void plotTitleDefault();
    void plotTitleChange();
    void loadInvalidFile();
    void loadEmptyFile();
    void formatTimeZeroElapsed();
    void formatTimeDayBoundary();
    void formatTimeNegativeElapsed();
    void loadCsvHeaderOnly();
    void loadCsvMalformedRows();
    void lockSeriesMetricType();
    void lockSeriesColor();
    void importMultiStreamColorsAndVisibility();
    void importSnrStreamGrouping();
    void importExportRoundTrip();
    void lockAxisRange();
    void hasLockSeriesTrue();
    void hasLockSeriesFalse();
    void yAutoRangeIgnoresLockSeries();
    void loadCsvFileAsyncEmitsDataChanged();
    void loadCsvFileAsyncEmitsLoadFailed();

    // In-memory addStreamData tests
    void addStreamDataLockSeriesCreated();
    void addStreamDataSNRSeriesCreated();
    void addStreamDataMultipleStreamsAccumulate();
    void addStreamDataEmptyDataNoOp();

    // Frame sync error accumulation (US3.1) tests
    void addStreamDataErrorSeriesCreated();
    void errorSeriesSharesLockColor();
    void setLockAxisViewTogglesVisibility();
    void setLockAxisViewPreservesStreamSelection();
    void frameSyncErrorMaxReflectsData();

    // Stream identity: two streams sharing a TMATS-derived streamLabel must stay
    // distinct by streamOrder (pcmChannelId) — regression coverage for a
    // pre-v2.6.0 cross-contamination bug (reprocess-replace and rename/recolor
    // sibling-sync all matched on streamLabel alone).
    void reprocessOnlySameStreamOrderReplaced();
    void renameSeriesOnlySameStreamOrderSiblingRenamed();
    void recolorSeriesOnlySameStreamOrderSiblingRecolored();

    // Cross-source stream identity (Phase 1 multi-file input): two DIFFERENT
    // sources can legitimately reuse the same (streamLabel, streamOrder) — e.g.
    // two .ch10 files whose TMATS channel ids collide. sourceId must be part of
    // identity so reprocess-replace and rename/recolor sibling-sync never cross
    // a source boundary.
    void crossSourceReprocessDoesNotEraseOtherSource();
    void crossSourceRenameDoesNotAffectOtherSource();
    void crossSourceRecolorDoesNotAffectOtherSource();

    // Multi-source time-base re-basing (Phase 1 multi-file input, §3): the
    // shared elapsed-seconds base must track the EARLIEST absolute sample
    // across every source, re-basing (shifting existing series right) when a
    // later-added source's recording started earlier than everything loaded.
    void addStreamDataRebasesWhenLaterSourceStartsEarlier();
    void addStreamDataNoRebaseWhenLaterSourceStartsAfter();
    void addStreamDataRebasesTwiceForSuccessivelyEarlierSources();

    // Non-overlapping source detection (Phase 1 multi-file input, §3):
    // informational-only signal when a newly added source's absolute time
    // range doesn't overlap the data already loaded.
    void nonOverlappingSourceEmitsWarning();
    void overlappingSourceDoesNotEmitWarning();

    // Source removal (Phase 1 multi-file input, §5): drops only the target
    // source's series, and re-bases (mirroring the add-time re-base) only when
    // the removed source held the earliest sample.
    void removeSourceDropsOnlyThatSourcesSeries();
    void removeSourceRebasesLeftWhenRemovedSourceWasEarliest();
    void removeSourceNoRebaseWhenRemovedSourceWasNotEarliest();
    void removeLastSourceClearsAllData();
    void removeUnknownSourceIsNoOp();

    // Multi-file source view (US1.1 batch browsing)
    void setVisibleSourceIsolatesSource();
    void sourceListListsDistinctLabeledSources();
    void exportCsvSourceFilterWritesOnlyThatSource();

    // exportCsv tests
    void exportCsvCreatesFile();
    void exportCsvHeaderAndData();
    void exportCsvEmptyNoFile();
    void exportCsvIncludesErrorColumn();
    void exportCsvRoundTripsThroughLoad();
};

#endif // TST_PLOTVIEWMODEL_H
