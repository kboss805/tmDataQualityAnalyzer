#ifndef TST_TMCHART_H
#define TST_TMCHART_H

#include <QObject>

/// @brief Unit tests for TmChart, the first-party chart that replaces QCustomPlot.
///
/// Focuses on the parts a rendering bug would hide: coordinate transforms, the
/// guards that keep a degenerate range from poisoning them, series bookkeeping
/// across removals, and that the export path paints the same content as the
/// screen. Pixel-exact appearance is deliberately not asserted - it would pin
/// styling rather than behaviour.
class TestTmChart : public QObject
{
    Q_OBJECT

private slots:
    void constructsEmpty();
    void seriesAddRemoveAndClear();
    void seriesDataTruncatesMismatchedLengths();
    void rejectsDegenerateAndNonFiniteRanges();
    void xTransformRoundTrips();
    void yTransformsAreOrientedAndIndependent();
    void tickValuesSpanRangeInclusive();
    void wheelZoomAnchorsUnderCursorAndReportsRange();
    void interactionsDisabledIgnoresWheel();
    void hiddenAxisReclaimsItsMargin();
    void renderToPaintsData();
    void exportOmitsCursorOverlays();
};

#endif // TST_TMCHART_H
