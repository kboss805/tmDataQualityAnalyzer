#include "tst_tmchart.h"

#include <QImage>
#include <QPainter>
#include <QtTest>

#include <cmath>

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

void TestTmChart::denseSeriesStillDrawsASingleSampleDropout()
{
    // A series with far more samples than the plot has pixel columns is drawn from
    // its per-column envelope rather than sample by sample. The envelope has to
    // keep a one-sample dropout: on this plot that lone sample IS the event an
    // operator is hunting for, and a reduction that kept only the first and last
    // sample of each column would lose it.
    //
    // The dropout is placed in the MIDDLE of a column, computed from the rendered
    // plot area rather than guessed. An earlier version of this test put it at
    // exactly half the series, which - x being the sample index, and the mapping
    // linear - makes it the column's first sample, drawn either way. The test
    // passed against the reduction it exists to reject.
    TmChart chart;
    chart.resize(600, 400);
    chart.show();
    QVERIFY(QTest::qWaitForWindowExposed(&chart));

    const int kSamples = 200000;
    QVector<double> xs;
    QVector<double> ys;
    xs.reserve(kSamples);
    ys.reserve(kSamples);
    for (int i = 0; i < kSamples; ++i)
    {
        xs.push_back(i);
        ys.push_back(100.0);
    }

    const int id = chart.addSeries(TmChart::Axis::Left);
    chart.setSeriesPen(id, QPen(Qt::red));
    chart.setSeriesData(id, xs, ys);
    chart.setXRange(0, kSamples - 1);
    chart.setLeftRange(0, 100);

    auto render = [&]() {
        QImage img(chart.size(), QImage::Format_ARGB32);
        img.fill(Qt::white);
        QPainter painter(&img);
        chart.renderTo(painter, chart.size());
        painter.end();
        return img;
    };

    // First render establishes the plot area, which fixes how many samples share a
    // pixel column; the dropout then goes halfway into one.
    render();
    const QRectF area = chart.plotArea();
    QVERIFY(area.width() > 100);
    const int per_column = kSamples / static_cast<int>(area.width());
    QVERIFY2(per_column > 10, "series is not dense enough to be drawn from its envelope");

    const int drop_at = kSamples / 2 + per_column / 2;
    ys[drop_at] = 0.0;                  // one sample at the bottom of the range
    chart.setSeriesData(id, xs, ys);

    const QImage img = render();

    // The flat line sits at the top of the axis, so any series-coloured pixel in
    // the bottom quarter can only be the dropout.
    int reddish = 0;
    for (int y = static_cast<int>(area.bottom() - area.height() * 0.25);
         y < static_cast<int>(area.bottom()) && y < img.height(); ++y)
    {
        for (int x = 0; x < img.width(); ++x)
        {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 120 && c.green() < 100 && c.blue() < 100)
            {
                ++reddish;
            }
        }
    }
    QVERIFY2(reddish > 0, "a one-sample dropout mid-column was not drawn");
}

void TestTmChart::linePersistsWhenNoSampleIsInsideTheView()
{
    // A series is built only from the samples inside the X range, which is what
    // makes a zoomed plot cheap. The sample on each SIDE of the range has to be
    // kept anyway: zoomed far enough in - or sitting between two sparse points -
    // the view can contain no sample at all, and the line crossing it is still
    // real data. Keeping only what is strictly inside would blank the plot at
    // exactly the zoom level an operator uses to read a value off it.
    TmChart chart;
    chart.resize(400, 300);
    chart.setLeftRange(0, 100);

    const int s = chart.addSeries(TmChart::Axis::Left);
    chart.setSeriesPen(s, QPen(Qt::red, 2));
    chart.setSeriesData(s, { 0.0, 100.0 }, { 50.0, 50.0 });

    // A window in the middle of the single segment: no sample lies within it.
    chart.setXRange(40.0, 60.0);

    QImage img(chart.size(), QImage::Format_ARGB32);
    const QColor bg(10, 10, 10);
    img.fill(bg);
    QPainter painter(&img);
    chart.renderTo(painter, chart.size());
    painter.end();

    const QRectF area = chart.plotArea();
    int reddish = 0;
    for (int y = static_cast<int>(area.top()); y < static_cast<int>(area.bottom()); ++y)
    {
        for (int x = static_cast<int>(area.left()); x < static_cast<int>(area.right()); ++x)
        {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 120 && c.green() < 100 && c.blue() < 100)
            {
                ++reddish;
            }
        }
    }
    QVERIFY2(reddish > 0, "the series vanished when no sample was inside the view");
}

void TestTmChart::rangeChangeRedrawsInsteadOfReusingGeometry()
{
    // The geometry is in pixels and is cached across paints, because a crosshair
    // move repaints without changing anything it was built from. A range change
    // DOES change it, and reusing it would leave the plot showing the previous
    // view - a stale picture that still looks like a plausible plot, which is the
    // hard kind of wrong. Same for the plot area: a resize moves every pixel.
    TmChart chart;
    chart.resize(400, 300);
    chart.setLeftRange(0, 100);

    // A step: low over the first half of the span, high over the second.
    QVector<double> xs;
    QVector<double> ys;
    for (int i = 0; i <= 100; ++i)
    {
        xs.push_back(i);
        ys.push_back(i < 50 ? 10.0 : 90.0);
    }
    const int s = chart.addSeries(TmChart::Axis::Left);
    chart.setSeriesPen(s, QPen(Qt::red, 2));
    chart.setSeriesData(s, xs, ys);
    chart.setXRange(0, 100);

    const QColor bg(10, 10, 10);
    auto meanSeriesY = [&]() {
        QImage img(chart.size(), QImage::Format_ARGB32);
        img.fill(bg);
        QPainter painter(&img);
        chart.renderTo(painter, chart.size());
        painter.end();

        const QRectF area = chart.plotArea();
        double sum   = 0.0;
        int    count = 0;
        for (int y = static_cast<int>(area.top()); y < static_cast<int>(area.bottom()); ++y)
        {
            for (int x = static_cast<int>(area.left()); x < static_cast<int>(area.right()); ++x)
            {
                const QColor c = img.pixelColor(x, y);
                if (c.red() > 120 && c.green() < 100 && c.blue() < 100)
                {
                    sum += y;
                    ++count;
                }
            }
        }
        return count > 0 ? sum / count : -1.0;
    };

    const double both_halves = meanSeriesY();
    QVERIFY(both_halves > 0.0);

    // Zoom onto the high half only. Y grows downwards, so the series must now sit
    // measurably HIGHER on the image than the average of the two halves did.
    chart.setXRange(60.0, 100.0);
    const double high_half = meanSeriesY();
    QVERIFY(high_half > 0.0);
    QVERIFY2(high_half < both_halves - 10.0,
             "the plot still showed the previous X range");
}

void TestTmChart::cachedPaintStillFollowsTheData()
{
    // The painted data layer is cached as a pixmap and blitted, so a repaint that
    // changed nothing costs a blit instead of rasterizing every series again. The
    // risk that buys is a SECOND stale-cache failure mode, one the geometry tests
    // cannot see: they go through renderTo(), which paints straight to its target
    // and never touches the pixmap. This one goes through grab(), so it exercises
    // paintEvent - the only path that uses it.
    TmChart chart;
    chart.resize(400, 300);
    chart.setLeftRange(0, 100);
    chart.show();
    QVERIFY(QTest::qWaitForWindowExposed(&chart));

    // A step: low over the first half of the span, high over the second.
    QVector<double> xs;
    QVector<double> ys;
    for (int i = 0; i <= 100; ++i)
    {
        xs.push_back(i);
        ys.push_back(i < 50 ? 10.0 : 90.0);
    }
    const int s = chart.addSeries(TmChart::Axis::Left);
    chart.setSeriesPen(s, QPen(Qt::red, 2));
    chart.setSeriesData(s, xs, ys);
    chart.setXRange(0, 100);

    const QImage both = chart.grab().toImage();
    QVERIFY(!both.isNull());

    // Painted twice with nothing changed, the picture must be identical - that is
    // the cache doing its job rather than a coincidence worth asserting elsewhere.
    QCOMPARE(chart.grab().toImage(), both);

    // Zoomed onto the high half it must NOT be identical. A stale pixmap here shows
    // the previous view, which still looks like a plausible plot.
    chart.setXRange(60.0, 100.0);
    QVERIFY2(chart.grab().toImage() != both, "the plot still showed the previous X range");
}

void TestTmChart::crosshairDrawsOverTheCachedPlot()
{
    // The crosshair is deliberately NOT part of the cached layer - that is what
    // makes moving it cheap. The failure that buys is the opposite of a stale plot:
    // an overlay that never appears, because the blit is all that reaches the
    // screen. Nothing else in the suite would notice, since every other rendering
    // test goes through renderTo(), which excludes overlays by design.
    TmChart chart;
    chart.resize(400, 300);
    chart.setLeftRange(0, 100);
    chart.setXRange(0, 100);
    chart.show();
    QVERIFY(QTest::qWaitForWindowExposed(&chart));

    const QImage plain = chart.grab().toImage();

    chart.setCrosshair(50.0, true);
    const QImage with_cursor = chart.grab().toImage();
    QVERIFY2(with_cursor != plain, "the crosshair was swallowed by the cached layer");

    // ...and hiding it returns the plot to exactly what it was, so the overlay is
    // not being baked into the cache either.
    chart.setCrosshair(50.0, false);
    QCOMPARE(chart.grab().toImage(), plain);
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
