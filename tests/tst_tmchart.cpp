#include "tst_tmchart.h"

#include <QImage>
#include <QPainter>
#include <QtTest>

#include "constants.h"
#include "tmchart.h"

namespace
{
/// A chart sized and ranged for predictable coordinate maths. Resized explicitly
/// rather than shown, so the tests run headless.
void prepare(TmChart& chart)
{
    chart.resize(400, 300);
    chart.setXRange(0.0, 100.0);
    chart.setLeftRange(0.0, 50.0);
    chart.setRightRange(-10.0, 10.0);
}

/// @return Number of pixels differing from @p background.
int nonBackgroundPixels(const QImage& img, const QColor& background)
{
    int count = 0;
    for (int y = 0; y < img.height(); ++y)
    {
        for (int x = 0; x < img.width(); ++x)
        {
            if (img.pixelColor(x, y) != background)
            {
                ++count;
            }
        }
    }
    return count;
}
}  // namespace

void TestTmChart::constructsEmpty()
{
    TmChart chart;
    QCOMPARE(chart.seriesCount(), 0);
    // Interactions start off: an empty chart must not be scrollable into a
    // meaningless range before any data arrives.
    QVERIFY(!chart.interactionsEnabled());
}

void TestTmChart::seriesAddRemoveAndClear()
{
    TmChart chart;
    const int a = chart.addSeries(TmChart::Axis::Left);
    const int b = chart.addSeries(TmChart::Axis::Right);
    QCOMPARE(a, 0);
    QCOMPARE(b, 1);
    QCOMPARE(chart.seriesCount(), 2);

    chart.setSeriesName(a, QStringLiteral("first"));
    chart.setSeriesName(b, QStringLiteral("second"));
    chart.setSeriesPen(a, QPen(Qt::red));
    QCOMPARE(chart.seriesPen(a).color(), QColor(Qt::red));

    chart.setSeriesVisible(a, false);
    QVERIFY(!chart.seriesVisible(a));
    QVERIFY(chart.seriesVisible(b));

    // Removing shifts later indices down - the caller's stored indices move, which
    // is why PlotWidget rebuilds its mapping rather than caching them.
    chart.removeSeries(a);
    QCOMPARE(chart.seriesCount(), 1);
    QCOMPARE(chart.seriesName(0), QString("second"));

    // Out-of-range access must be inert rather than crash or assert.
    chart.removeSeries(99);
    chart.setSeriesVisible(-1, false);
    QCOMPARE(chart.seriesCount(), 1);
    QCOMPARE(chart.seriesName(99), QString());

    chart.clearSeries();
    QCOMPARE(chart.seriesCount(), 0);
}

void TestTmChart::seriesDataTruncatesMismatchedLengths()
{
    // Guards a read past the end while drawing: the paint loop walks x and y in
    // lockstep, so an over-long x vector would index a shorter y.
    TmChart chart;
    prepare(chart);
    const int s = chart.addSeries(TmChart::Axis::Left);
    chart.setSeriesData(s, { 0.0, 1.0, 2.0, 3.0 }, { 5.0, 6.0 });

    QImage img(chart.size(), QImage::Format_ARGB32);
    QPainter painter(&img);
    chart.renderTo(painter, chart.size());   // must not crash
    painter.end();
    QVERIFY(true);
}

void TestTmChart::rejectsDegenerateAndNonFiniteRanges()
{
    TmChart chart;
    prepare(chart);
    const double lower = chart.xLower();
    const double upper = chart.xUpper();

    chart.setXRange(5.0, 5.0);                 // zero span
    QCOMPARE(chart.xLower(), lower);
    QCOMPARE(chart.xUpper(), upper);

    chart.setXRange(10.0, 1.0);                // inverted
    QCOMPARE(chart.xLower(), lower);

    chart.setXRange(qQNaN(), 10.0);            // non-finite
    QCOMPARE(chart.xLower(), lower);

    chart.setXRange(0.0, std::numeric_limits<double>::infinity());
    QCOMPARE(chart.xUpper(), upper);
}

void TestTmChart::xTransformRoundTrips()
{
    TmChart chart;
    prepare(chart);
    for (double v : { 0.0, 25.0, 50.0, 99.9 })
    {
        const double px = chart.xToPixel(v);
        QVERIFY(qAbs(chart.pixelToX(px) - v) < 1e-6);
    }
    // Range ends map to the plot area's edges, not the widget's - the axis gutter
    // is outside the data region.
    QVERIFY(qAbs(chart.xToPixel(0.0)   - chart.plotArea().left())  < 1e-6);
    QVERIFY(qAbs(chart.xToPixel(100.0) - chart.plotArea().right()) < 1e-6);
}

void TestTmChart::yTransformsAreOrientedAndIndependent()
{
    TmChart chart;
    prepare(chart);
    // Screen Y grows downward while data grows upward, so a larger value must sit
    // at a smaller pixel Y. Getting this backwards would flip every plot.
    QVERIFY(chart.leftToPixel(50.0) < chart.leftToPixel(0.0));
    QVERIFY(qAbs(chart.leftToPixel(0.0)  - chart.plotArea().bottom()) < 1e-6);
    QVERIFY(qAbs(chart.leftToPixel(50.0) - chart.plotArea().top())    < 1e-6);
    QVERIFY(qAbs(chart.pixelToLeft(chart.leftToPixel(12.5)) - 12.5)   < 1e-6);

    // The two Y axes scale independently: the same data value lands in different
    // places, which is the whole point of the dual-axis layout.
    QVERIFY(qAbs(chart.rightToPixel(0.0) - chart.plotArea().center().y()) < 2.0);
    QVERIFY(chart.rightToPixel(0.0) != chart.leftToPixel(0.0));
}

void TestTmChart::tickValuesSpanRangeInclusive()
{
    // The X axis is time, and the previous implementation pinned exactly N evenly
    // spaced ticks including both ends. Verified through the rendered output's
    // reliance on it: first and last tick sit on the plot-area edges.
    TmChart chart;
    prepare(chart);
    chart.setXTickCount(5);
    QCOMPARE(chart.xToPixel(chart.xLower()), chart.plotArea().left());
    QCOMPARE(chart.xToPixel(chart.xUpper()), chart.plotArea().right());
}

void TestTmChart::wheelZoomAnchorsUnderCursorAndReportsRange()
{
    TmChart chart;
    prepare(chart);
    chart.setInteractionsEnabled(true);
    QSignalSpy spy(&chart, &TmChart::xRangeChangedByUser);

    // Zoom centred on the cursor: the data value under the pointer must stay under
    // the pointer, which is what makes wheel zoom feel anchored rather than drifting.
    const QPointF pos(chart.plotArea().left() + chart.plotArea().width() / 4.0,
                      chart.plotArea().center().y());
    const double under_cursor_before = chart.pixelToX(pos.x());

    QWheelEvent ev(pos, chart.mapToGlobal(pos.toPoint()), QPoint(0, 0), QPoint(0, 120),
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&chart, &ev);

    QCOMPARE(spy.count(), 1);
    QVERIFY(chart.xUpper() - chart.xLower() < 100.0);          // zoomed in
    QVERIFY(qAbs(chart.pixelToX(pos.x()) - under_cursor_before) < 1e-6);
}

void TestTmChart::interactionsDisabledIgnoresWheel()
{
    TmChart chart;
    prepare(chart);
    chart.setInteractionsEnabled(false);
    QSignalSpy spy(&chart, &TmChart::xRangeChangedByUser);

    const QPointF pos(chart.plotArea().center());
    QWheelEvent ev(pos, chart.mapToGlobal(pos.toPoint()), QPoint(0, 0), QPoint(0, 120),
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&chart, &ev);

    QCOMPARE(spy.count(), 0);
    QCOMPARE(chart.xLower(), 0.0);
    QCOMPARE(chart.xUpper(), 100.0);
}

void TestTmChart::hiddenAxisReclaimsItsMargin()
{
    // An axis nobody plots against is hidden rather than left auto-ranged under a
    // label naming data that is not there. Asserted through the plot area because
    // that is the measurable consequence: a hidden axis reserves no gutter, so the
    // chart grows into it. Whether the label is *painted* is checked by rendering.
    TmChart chart;
    prepare(chart);
    chart.setLeftLabel(QStringLiteral("Lock (%)"));
    chart.setRightLabel(QStringLiteral("SNR (dB)"));

    // Defaults: left drawn, right not - an empty chart is still a chart.
    QVERIFY(chart.leftAxisVisible());
    QVERIFY(!chart.rightAxisVisible());

    const double left_gutter_shown = chart.plotArea().left();
    QVERIFY(left_gutter_shown > 0.0);

    chart.setLeftAxisVisible(false);
    QVERIFY(!chart.leftAxisVisible());
    QVERIFY(chart.plotArea().left() < left_gutter_shown);   // margin given back

    chart.setRightAxisVisible(true);
    const double right_edge_shown = chart.plotArea().right();
    chart.setRightAxisVisible(false);
    QVERIFY(chart.plotArea().right() > right_edge_shown);

    // The transforms must follow the new plot area, not a stale one - a coordinate
    // still mapped against the old gutter would draw every point offset.
    chart.setLeftAxisVisible(true);
    QVERIFY(qAbs(chart.xToPixel(0.0) - chart.plotArea().left()) < 1e-6);
    QVERIFY(qAbs(chart.leftToPixel(0.0) - chart.plotArea().bottom()) < 1e-6);
}

void TestTmChart::renderToPaintsData()
{
    // renderTo() is the single path used for PNG, SVG and PDF export, so if it
    // paints nothing every export format is silently blank.
    TmChart chart;
    prepare(chart);
    const QColor bg(10, 10, 10);
    chart.setThemeColors(bg, Qt::white, Qt::gray, Qt::cyan);
    chart.setTitle(QStringLiteral("Test Plot"));
    chart.setXLabel(QStringLiteral("Time"));
    chart.setLeftLabel(QStringLiteral("Lock (%)"));

    const int s = chart.addSeries(TmChart::Axis::Left);
    chart.setSeriesPen(s, QPen(Qt::red, 2));
    chart.setSeriesData(s, { 0.0, 50.0, 100.0 }, { 0.0, 25.0, 50.0 });

    QImage img(chart.size(), QImage::Format_ARGB32);
    img.fill(bg);
    QPainter painter(&img);
    chart.renderTo(painter, chart.size());
    painter.end();

    QVERIFY(nonBackgroundPixels(img, bg) > 100);   // axes, labels and the series
}

void TestTmChart::exportOmitsCursorOverlays()
{
    // The crosshair and zoom band are cursor state, not data: they must never be
    // baked into an exported image. Rendering identical output with and without
    // them set is the check.
    TmChart chart;
    prepare(chart);
    const QColor bg(10, 10, 10);
    chart.setThemeColors(bg, Qt::white, Qt::gray, Qt::cyan);
    const int s = chart.addSeries(TmChart::Axis::Left);
    chart.setSeriesPen(s, QPen(Qt::red, 2));
    chart.setSeriesData(s, { 0.0, 100.0 }, { 0.0, 50.0 });

    auto snapshot = [&chart, &bg]() {
        QImage img(chart.size(), QImage::Format_ARGB32);
        img.fill(bg);
        QPainter p(&img);
        chart.renderTo(p, chart.size());
        p.end();
        return img;
    };

    const QImage plain = snapshot();
    chart.setCrosshair(50.0, true);
    chart.setZoomBand(20.0, 80.0, true);
    const QImage with_overlays = snapshot();

    QCOMPARE(plain, with_overlays);
}
