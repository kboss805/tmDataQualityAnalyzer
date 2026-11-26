using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Media.Immutable;
using Avalonia.Threading;
using TmDq.App.ViewModels.Plot;
using TmDq.Core;

namespace TmDq.App.Views.Plot;

/// <summary>
/// The plot (plan step 4.3b, US4.0), drawn and driven as v2.x's was: one shared time axis and two Y axes
/// (lock percentage or missed frames on the left, Receiver SNR on the right, each drawn only when something is
/// on it); each series drawn from the core's geometry (its line, and above two samples a pixel column the band
/// from each column's minimum to its maximum); ticks on round clock times; wheel zoom about the cursor, drag pan,
/// a band dragged out with Ctrl or the middle button to zoom into it, double-click for the whole span; a crosshair
/// and the readout of the nearest value.
///
/// The data layer is painted once into a bitmap and reused until the data, a range, the size or the labels change,
/// so moving the cursor costs the crosshair, not every series again. The state is the <see cref="PlotViewModel"/>'s;
/// the chart only draws it and reports gestures to it.
///
/// Two things keep a large plot responsive while it is panned or zoomed. Each long series has an index in the core
/// (<see cref="SeriesIndex"/>), so its envelope is looked up rather than walked: what a frame costs no longer grows
/// with the samples in view. And while a drag, the wheel or a pan or zoom key is moving a plot whose last full paint took longer than
/// <see cref="DraftThresholdMs"/>, the data layer is painted as a draft: each band's outline without its fill and
/// every line one device pixel wide, which Skia draws several times faster. The draft is still each column's minimum
/// to maximum, so a one-sample dropout shows in it; the full picture is painted when the drag or the key is released or
/// <see cref="RefineDelay"/> after the last step. An export never draws a draft.
/// </summary>
[SuppressMessage("Design", "CA1001:Types that own disposable fields should be disposable",
    Justification = "A control is not disposed by its owner: the bitmap is released when it leaves the visual tree (OnDetachedFromVisualTree).")]
public sealed class PlotChart : Control
{
    public static readonly StyledProperty<PlotViewModel?> PlotProperty =
        AvaloniaProperty.Register<PlotChart, PlotViewModel?>(nameof(Plot));

    // The chart's layout, in pixels, as v2.x laid it out.
    public const double OuterMargin = 10, AxisLabelGap = 6, TickLength = 4, TickLabelGap = 12, TopInset = 12;
    public const double FontSize = 12, PenWidth = 1.5, StrongPenWidth = 2.25;
    public const int XTickCount = 10, YTickCount = 6;
    // The dotted grid's and the crosshair's dash patterns (a pen reads them and never changes them).
    private static readonly double[] GridDashes = { 1.0, 2.0 };

    /// <summary>A dotted series' dashes, in multiples of its line's width: a dot, then twice as long a gap.</summary>
    public static readonly IReadOnlyList<double> SeriesDots = new[] { 1.0, 2.0 };
    private static readonly double[] CrosshairDashes = { 4.0, 2.0 };

    public const byte BandAlpha = 90, BandEdgeAlpha = 190, CrosshairAlpha = 140, ZoomBandAlpha = 45;

    /// <summary>A series this long or longer gets an index: shorter ones cost next to nothing to walk.</summary>
    public const int IndexMinSamples = 4096;

    /// <summary>A gesture draws drafts only on a plot whose last full paint took longer than this: about 30 frames a
    /// second. Measured: 52 one-hour series at 10 ms took about 90 ms to paint in full with their indexes, and a frame
    /// of their draft about 40 ms; four such series take 8 ms in full, and are never drafted.</summary>
    public const double DefaultDraftThresholdMs = 30;

    /// <summary>How long after the last step of a gesture (a wheel notch, a drag or a held pan or zoom key) the full picture is painted.</summary>
    public static readonly TimeSpan DefaultRefineDelay = TimeSpan.FromMilliseconds(150);

    // Keyed by the series itself: a new series reusing an old one's id must not be drawn from the old one's geometry.
    private readonly Dictionary<PlotSeries, (int Version, ChartView View, SeriesGeometry Geometry)> _geometry = new();
    // Depends on the values alone, which a series never changes: kept until the series goes, through any shift of its times.
    private readonly Dictionary<PlotSeries, SeriesIndex> _indexes = new();
    private RenderTargetBitmap? _backing;
    private bool _backingValid;
    private (FontFamily Family, double Scale, double TextHeight, double LabelWidth)? _metrics;
    private PlotViewModel? _watched;

    private bool _moving;          // a gesture is moving the plot, and the plot is heavy enough to draft
    private bool _draftShown;      // the data layer on screen is a draft
    private double _lastFullPaintMs;
    private int _gestureSteps;     // bumped by each step, so that only the last step's delay settles the gesture

    private bool _panning;
    private double _panAnchorX;
    private bool _bandZooming;
    private double _bandFrom, _bandTo;
    private bool _crosshairVisible;
    private double _crosshairX;

    public PlotChart()
    {
        Focusable = true;
        ClipToBounds = true;
        ToolTip.SetPlacement(this, PlacementMode.Pointer);
        // The theme changed (US9.0): the cached layer was painted in the other palette.
        ActualThemeVariantChanged += (_, _) =>
        {
            _backingValid = false;
            _metrics = null;   // a theme may bring another font
            InvalidateVisual();
        };
    }

    /// <summary>The colours of the theme in use.</summary>
    public ChartPalette Palette => ChartPalette.For(ActualThemeVariant);

    public PlotViewModel? Plot
    {
        get => GetValue(PlotProperty);
        set => SetValue(PlotProperty, value);
    }

    /// <summary>How many times the data layer has been painted, drafts included: a cursor move must not add to it.</summary>
    public int DataLayerRenders { get; private set; }

    /// <summary>How many of those paints were drafts.</summary>
    public int DraftLayerRenders { get; private set; }

    /// <summary>How many bitmaps the data layer has been painted into: a new one only when the chart's size in pixels changes.</summary>
    public int BackingBitmapsMade { get; private set; }

    /// <summary>Whether the data layer on screen is a draft.</summary>
    public bool DraftShown => _draftShown;

    /// <summary>How long the last full (not draft) paint of the data layer took, in milliseconds.</summary>
    public double LastFullPaintMs => _lastFullPaintMs;

    /// <summary>A gesture draws drafts only when the last full paint took longer than this (<see cref="DefaultDraftThresholdMs"/>).</summary>
    public double DraftThresholdMs { get; set; } = DefaultDraftThresholdMs;

    /// <summary>How long after the last step of a gesture the full picture is painted (<see cref="DefaultRefineDelay"/>).</summary>
    public TimeSpan RefineDelay { get; set; } = DefaultRefineDelay;

    /// <summary>How many series have an index.</summary>
    public int IndexCount => _indexes.Count;

    public bool CrosshairVisible => _crosshairVisible;
    public double CrosshairX => _crosshairX;
    public bool ZoomBandVisible => _bandZooming;
    public (double From, double To) ZoomBand => (_bandFrom, _bandTo);

    // ---- layout and transforms --------------------------------------------------------------------------------

    private Typeface Typeface => new(TextElement.GetFontFamily(this));

    private FormattedText Text(string text, Color color, bool bold = false) =>
        new(text, CultureInfo.CurrentCulture, FlowDirection.LeftToRight,
            bold ? new Typeface(Typeface.FontFamily, FontStyle.Normal, FontWeight.Bold) : Typeface,
            FontSize, new ImmutableSolidColorBrush(color));

    private double TextHeight => Metrics.TextHeight;

    /// <summary>How many times the text the layout is measured with has been measured: once until the font, the theme or the
    /// scaling changes, however often the layout is asked for (each mouse move asks several times).</summary>
    public int TextMeasurements { get; private set; }

    /// <summary>The height of a line of the chart's text and the width of the widest axis label, which the gutters are made
    /// of: measured once, and again only when the font family or the scaling changes, or the theme (<see cref="PlotChart()"/>).</summary>
    private (double TextHeight, double LabelWidth) Metrics
    {
        get
        {
            FontFamily family = TextElement.GetFontFamily(this);
            double scale = TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0;
            if (_metrics is { } m && m.Family.Equals(family) && m.Scale.Equals(scale)) return (m.TextHeight, m.LabelWidth);
            double height = Math.Ceiling(Text("Xg", Palette.Foreground).Height);
            double width = Math.Ceiling(Text("-000.0", Palette.Foreground).Width);
            _metrics = (family, scale, height, width);
            TextMeasurements++;
            return (height, width);
        }
    }

    /// <summary>The rectangle the data is drawn in: the control less the axes' gutters (a hidden axis reserves none),
    /// the title and the headroom kept for the overlays at the top.</summary>
    public Rect PlotArea => AreaFor(Bounds.Size);

    /// <summary>The values the left axis is ticked at (round ones: 0, 200, 400... for a range of 0 to 1155); none without data or
    /// while the axis is hidden. The ticks need the core, which is loaded once there is data.</summary>
    public double[] LeftTicks =>
        Plot is { HasData: true, LeftAxisVisible: true } vm ? ChartGeometry.RoundTicks(vm.LeftMin, vm.LeftMax, YTickCount) : Array.Empty<double>();

    /// <summary>The values the right axis is ticked at, as <see cref="LeftTicks"/>.</summary>
    public double[] RightTicks =>
        Plot is { HasData: true, RightAxisVisible: true } vm ? ChartGeometry.RoundTicks(vm.RightMin, vm.RightMax, YTickCount) : Array.Empty<double>();

    /// <summary>
    /// Where the legend's right edge may reach in a chart of <paramref name="size"/>: the chart's own right edge, or, while
    /// the right axis is shown, the plot area's, so that the legend does not cover that axis's labels and title. A hidden
    /// axis reserves no gutter, so there is nothing to keep clear.
    /// </summary>
    public double LegendRightEdge(Size size) => Plot?.RightAxisVisible == true ? AreaFor(size).Right : size.Width;

    private Rect AreaFor(Size size)
    {
        PlotViewModel? vm = Plot;
        bool leftVisible = vm?.LeftAxisVisible ?? true, rightVisible = vm?.RightAxisVisible ?? false;
        (double textH, double labelW) = Metrics;
        double left = OuterMargin;
        if (leftVisible) left += labelW + TickLength + textH + AxisLabelGap;
        double right = OuterMargin;
        if (rightVisible) right += labelW + TickLength + textH + AxisLabelGap;
        double top = OuterMargin + TopInset + textH + AxisLabelGap;   // the title is always set
        double bottom = OuterMargin + textH + TickLength + textH + AxisLabelGap;
        return new Rect(left, top, Math.Max(1, size.Width - left - right), Math.Max(1, size.Height - top - bottom));
    }

    private ChartView ViewFor(bool leftAxis, Rect area)
    {
        PlotViewModel? vm = Plot;
        double xLower = vm?.XViewMin ?? 0, xUpper = vm?.XViewMax ?? 1;
        double yLower = leftAxis ? vm?.LeftMin ?? 0 : vm?.RightMin ?? 0;
        double yUpper = leftAxis ? vm?.LeftMax ?? 100 : vm?.RightMax ?? 1;
        return new ChartView(xLower, xUpper, yLower, yUpper, area.X, area.Y, area.Width, area.Height);
    }

    public double XToPixel(double x) => ChartGeometry.XToPixel(ViewFor(true, PlotArea), x);
    public double PixelToX(double px) => ChartGeometry.PixelToX(ViewFor(true, PlotArea), px);
    public double LeftToPixel(double y) => ChartGeometry.YToPixel(ViewFor(true, PlotArea), y);
    public double RightToPixel(double y) => ChartGeometry.YToPixel(ViewFor(false, PlotArea), y);

    // ---- the model --------------------------------------------------------------------------------------------

    /// <summary>
    /// The cached layer is a bitmap and each index is native memory: both go when the chart leaves the window, and
    /// are made again, from the series, if it returns.
    /// </summary>
    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        _backing?.Dispose();
        _backing = null;
        _backingValid = false;
        foreach (SeriesIndex index in _indexes.Values) index.Dispose();
        _indexes.Clear();
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == PlotProperty)
        {
            if (_watched is not null)
            {
                _watched.SeriesChanged -= OnSeriesChanged;
                _watched.RangesChanged -= OnRangesChanged;
                _watched.PropertyChanged -= OnModelPropertyChanged;
            }
            _watched = Plot;
            if (_watched is not null)
            {
                _watched.SeriesChanged += OnSeriesChanged;
                _watched.RangesChanged += OnRangesChanged;
                _watched.PropertyChanged += OnModelPropertyChanged;
            }
            OnSeriesChanged(this, EventArgs.Empty);
        }
        else if (change.Property == BoundsProperty)
        {
            _backingValid = false;
        }

    }

    // The data layer is reused until the data, a range, the size or the labels change, so each notification does what it
    // means: the series changing drops what was built for series no longer shown; a range paints the layer again; and of
    // the plot's properties only the title is drawn (the axis label changes with the series, the zoom history, the
    // legend's switch and the rest are not drawn).
    private void OnModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(PlotViewModel.Title)) OnRangesChanged(sender, e);
    }

    private void OnRangesChanged(object? sender, EventArgs e)
    {
        _backingValid = false;
        InvalidateVisual();
    }

    private void OnSeriesChanged(object? sender, EventArgs e)
    {
        // Forget the geometry and the index of series no longer shown.
        var current = new HashSet<PlotSeries>(Plot?.Series ?? Array.Empty<PlotSeries>());
        foreach (PlotSeries gone in new List<PlotSeries>(_geometry.Keys))
            if (!current.Contains(gone)) _geometry.Remove(gone);
        foreach (PlotSeries gone in new List<PlotSeries>(_indexes.Keys))
        {
            if (current.Contains(gone)) continue;
            _indexes[gone].Dispose();
            _indexes.Remove(gone);
        }
        _backingValid = false;
        Cursor = Plot?.HasData == true ? new Cursor(StandardCursorType.Hand) : Cursor.Default;
        InvalidateVisual();
    }

    // ---- painting ---------------------------------------------------------------------------------------------

    public override void Render(DrawingContext context)
    {
        Size size = Bounds.Size;
        if (size.Width < 1 || size.Height < 1) return;
        double scale = TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0;
        var pixels = new PixelSize(Math.Max(1, (int)Math.Ceiling(size.Width * scale)),
                                   Math.Max(1, (int)Math.Ceiling(size.Height * scale)));
        var dpi = new Vector(96 * scale, 96 * scale);
        if (!_backingValid || _backing is null || _backing.PixelSize != pixels || _backing.Dpi != dpi)
        {
            // A bitmap of the same size is painted again, not made again: a full-window bitmap a repaint was costly to
            // allocate and free. Its drawing context clears it to transparent first, so nothing of the last paint shows.
            if (_backing is null || _backing.PixelSize != pixels || _backing.Dpi != dpi)
            {
                _backing?.Dispose();
                _backing = new RenderTargetBitmap(pixels, dpi);
                BackingBitmapsMade++;
            }
            bool draft = _moving;
            long started = Stopwatch.GetTimestamp();
            using (DrawingContext into = _backing.CreateDrawingContext())
            {
                // A draft's lines are one device pixel wide, whatever the scaling.
                RenderData(into, size, draft ? 1.0 / scale : 0.0);
            }
            if (draft) DraftLayerRenders++;
            else _lastFullPaintMs = Stopwatch.GetElapsedTime(started).TotalMilliseconds;
            _draftShown = draft;
            _backingValid = true;
            DataLayerRenders++;
        }
        // The source rectangle is in the bitmap's pixels, not units: given as the units' size, above a scaling of 1 only
        // its top-left part was drawn, magnified across the chart.
        context.DrawImage(_backing, new Rect(0, 0, pixels.Width, pixels.Height), new Rect(size));
        RenderOverlays(context, size);
    }

    /// <summary>The chart at <paramref name="size"/>, without the cursor's crosshair and zoom band: what an export draws.
    /// Always the full picture, never a draft.</summary>
    public void RenderTo(DrawingContext context, Size size) => RenderData(context, size, 0.0);

    /// <summary>A step of a gesture is about to move the plot (a wheel notch, a drag's move, or a pan or zoom key, which
    /// repeats while it is held: <see cref="PlotView"/> reports those): on a heavy plot, the frames from here are
    /// drafts until the gesture settles (<see cref="Settle"/>, which a released drag or key calls and
    /// <see cref="RefineDelay"/> of quiet calls too).</summary>
    internal void Moving()
    {
        if (_lastFullPaintMs <= DraftThresholdMs) return;
        _moving = true;
        // Each step restarts the wait: only the last step's delay, run out, settles the gesture. (A delay posted back
        // to the UI thread rather than a DispatcherTimer, whose ticks only the dispatcher's main loop delivers.)
        int step = ++_gestureSteps;
        _ = Task.Delay(RefineDelay).ContinueWith(_ => Dispatcher.UIThread.Post(() =>
        {
            if (step == _gestureSteps) Settle();
        }), TaskScheduler.Default);
    }

    /// <summary>The gesture has stopped: the full picture replaces a draft on screen.</summary>
    public void Settle()
    {
        if (!_moving) return;
        _moving = false;
        if (!_draftShown) return;
        _backingValid = false;
        InvalidateVisual();
    }

    /// <param name="draftLine">0 for the full picture; otherwise a draft, its lines this wide.</param>
    private void RenderData(DrawingContext dc, Size size, double draftLine)
    {
        dc.FillRectangle(new ImmutableSolidColorBrush(Palette.Background), new Rect(size));
        PlotViewModel? vm = Plot;
        Rect area = AreaFor(size);
        bool hasData = vm?.HasData == true;
        bool leftVisible = vm?.LeftAxisVisible ?? true, rightVisible = vm?.RightAxisVisible ?? false;

        // The title, centred above the plot.
        FormattedText title = Text(vm?.Title ?? PlotViewModel.DefaultTitle, Palette.Title, bold: true);
        dc.DrawText(title, new Point((size.Width - title.Width) / 2, OuterMargin / 2));

        // Ticks need the core, which is loaded once there is data; an empty chart draws its own (DrawEmptyTicks).
        double[] xTicks = hasData ? ChartGeometry.TimeTicks(vm!.XViewMin, vm.XViewMax, XTickCount, vm.TimeOrigin) : Array.Empty<double>();
        double[] leftTicks = LeftTicks, rightTicks = RightTicks;
        ChartView left = ViewFor(true, area), right = ViewFor(false, area);

        // The grid: dotted, at the X ticks and at the Y ticks of the axis drawn (the left when both are).
        var gridPen = new ImmutablePen(new ImmutableSolidColorBrush(Palette.Grid), 1, new ImmutableDashStyle(GridDashes, 0));
        if (!hasData) DrawEmptyTicks(dc, area, vm, leftVisible, gridPen);
        foreach (double t in xTicks)
        {
            double px = ChartGeometry.XToPixel(left, t);
            dc.DrawLine(gridPen, new Point(px, area.Top), new Point(px, area.Bottom));
        }
        foreach (double t in leftTicks.Length > 0 ? leftTicks : rightTicks)
        {
            double py = ChartGeometry.YToPixel(leftTicks.Length > 0 ? left : right, t);
            dc.DrawLine(gridPen, new Point(area.Left, py), new Point(area.Right, py));
        }

        if (hasData)
        {
            using (dc.PushClip(area))
            {
                foreach (PlotSeries s in vm!.Series)
                {
                    if (!vm.EffectiveVisible(s) || s.X.Length < 2) continue;
                    DrawSeries(dc, s, s.IsLeftAxis ? left : right, draftLine);
                }
            }
        }

        DrawAxes(dc, size, area, vm, xTicks, leftTicks, rightTicks, left, right, leftVisible, rightVisible);
    }

    /// <summary>
    /// The empty chart's grid and ticks, as v2.x drew them before a recording was opened: <see cref="XTickCount"/>
    /// and <see cref="YTickCount"/> evenly spaced across the plot area, with the left axis's values (0 to 100 for
    /// lock percentage) beside its ticks. There is no time yet, so the time axis has its marks but no labels (v2.x's
    /// read "000:00:00:00" nine times over). Drawn here rather than by the core, which is loaded only once a
    /// recording opens.
    /// </summary>
    private void DrawEmptyTicks(DrawingContext dc, Rect area, PlotViewModel? vm, bool leftVisible, IPen gridPen)
    {
        var pen = new ImmutablePen(new ImmutableSolidColorBrush(Palette.Foreground), 1);
        for (int i = 0; i < XTickCount; i++)
        {
            double px = area.Left + area.Width * i / (XTickCount - 1);
            dc.DrawLine(gridPen, new Point(px, area.Top), new Point(px, area.Bottom));
            dc.DrawLine(pen, new Point(px, area.Bottom), new Point(px, area.Bottom + TickLength));
        }
        if (!leftVisible) return;
        double lower = vm?.LeftMin ?? PlotViewModel.LockAxisMin, upper = vm?.LeftMax ?? PlotViewModel.LockAxisMax;
        for (int i = 0; i < YTickCount; i++)
        {
            double py = area.Bottom - area.Height * i / (YTickCount - 1);
            dc.DrawLine(gridPen, new Point(area.Left, py), new Point(area.Right, py));
            dc.DrawLine(pen, new Point(area.Left - TickLength, py), new Point(area.Left, py));
            double value = lower + (upper - lower) * i / (YTickCount - 1);
            FormattedText label = Text(value.ToString("F1", CultureInfo.InvariantCulture), Palette.Foreground);
            dc.DrawText(label, new Point(area.Left - TickLength - 2 - label.Width, py - label.Height / 2));
        }
    }

    private void DrawSeries(DrawingContext dc, PlotSeries s, ChartView view, double draftLine)
    {
        if (!_geometry.TryGetValue(s, out var cached) || cached.Version != s.Version || cached.View != view)
        {
            cached = (s.Version, view, ChartGeometry.Build(s.X, s.Y, s.SortedX, view, IndexFor(s)));
            _geometry[s] = cached;
        }
        SeriesGeometry g = cached.Geometry;
        bool draft = draftLine > 0.0;
        Color color = Palette.SeriesColor(s.Color);   // the light theme draws a pale colour darker

        // The band first, so the mean line reads on top of it. Outlined as well as filled: its edges are the columns'
        // extremes, and stroking them keeps a one-sample excursion sharp. A draft keeps the outline and drops the fill,
        // which costs the most to paint.
        if (g.Bands.Count > 0)
        {
            IBrush? fill = draft ? null : new ImmutableSolidColorBrush(Color.FromArgb(BandAlpha, color.R, color.G, color.B));
            var edge = new ImmutablePen(new ImmutableSolidColorBrush(Color.FromArgb(BandEdgeAlpha, color.R, color.G, color.B)),
                                        draft ? draftLine : 1);
            foreach (double[] band in g.Bands)
            {
                var outline = new StreamGeometry();
                using (StreamGeometryContext c = outline.Open())
                {
                    c.BeginFigure(new Point(band[0], band[1]), true);
                    for (int i = 2; i < band.Length; i += 2) c.LineTo(new Point(band[i], band[i + 1]));
                    c.EndFigure(true);
                }
                dc.DrawGeometry(fill, edge, outline);
            }
        }

        if (g.SegmentCount == 0) return;
        var line = new StreamGeometry();
        using (StreamGeometryContext c = line.Open())
        {
            double[] seg = g.Segments;
            bool open = false;
            double lastX = double.NaN, lastY = double.NaN;
            for (int i = 0; i < seg.Length; i += 4)
            {
                // A run of joined segments is one figure; a gap starts the next.
                if (!open || seg[i] != lastX || seg[i + 1] != lastY)
                {
                    if (open) c.EndFigure(false);
                    c.BeginFigure(new Point(seg[i], seg[i + 1]), false);
                    open = true;
                }
                c.LineTo(new Point(seg[i + 2], seg[i + 3]));
                lastX = seg[i + 2];
                lastY = seg[i + 3];
            }
            if (open) c.EndFigure(false);
        }
        // A draft's line is one device pixel, which Skia draws as a hairline without its stroker: measured, 52 dense
        // series' lines went from 32 ms to 4 ms.
        // A dotted series is dotted at rest; a draft keeps its hairline solid, for the same reason as above. The combined
        // polarization is a little thicker.
        double width = draft ? draftLine : s.Strong ? StrongPenWidth : PenWidth;
        ImmutableDashStyle? dots = s.Dotted && !draft ? new ImmutableDashStyle(SeriesDots, 0) : null;
        dc.DrawGeometry(null, new ImmutablePen(new ImmutableSolidColorBrush(color), width, dots, lineJoin: PenLineJoin.Bevel), line);
    }

    /// <summary>The index of a long series with ordered times, built the first time it is drawn; null for the rest.</summary>
    private SeriesIndex? IndexFor(PlotSeries s)
    {
        if (!s.SortedX || s.Y.Length < IndexMinSamples) return null;
        if (!_indexes.TryGetValue(s, out SeriesIndex? index))
        {
            index = new SeriesIndex(s.Y);
            _indexes[s] = index;
        }
        return index;
    }

    private void DrawAxes(DrawingContext dc, Size size, Rect area, PlotViewModel? vm, double[] xTicks, double[] leftTicks,
                          double[] rightTicks, ChartView left, ChartView right, bool leftVisible, bool rightVisible)
    {
        var pen = new ImmutablePen(new ImmutableSolidColorBrush(Palette.Foreground), 1);
        double textH = TextHeight;

        // Axis lines: the bottom always; left and right only when that axis is in use.
        if (leftVisible) dc.DrawLine(pen, area.BottomLeft, area.TopLeft);
        dc.DrawLine(pen, area.BottomLeft, area.BottomRight);
        if (rightVisible) dc.DrawLine(pen, area.BottomRight, area.TopRight);

        // X ticks: every mark, but a label only where it clears the one before, and always the last, pulled inside
        // the control at either end.
        double lastLabelRight = -1e9;
        for (int i = 0; i < xTicks.Length; i++)
        {
            double px = ChartGeometry.XToPixel(left, xTicks[i]);
            dc.DrawLine(pen, new Point(px, area.Bottom), new Point(px, area.Bottom + TickLength));
            FormattedText label = Text(vm!.FormatTime(xTicks[i]), Palette.Foreground);
            double w = label.Width + TickLabelGap;
            double labelLeft = px - w / 2;
            bool isLast = i == xTicks.Length - 1;
            if (labelLeft < lastLabelRight && !isLast) continue;
            double drawLeft = labelLeft;
            if (isLast) drawLeft = Math.Min(drawLeft, area.Right - w);
            drawLeft = Math.Max(drawLeft, 0);
            dc.DrawText(label, new Point(drawLeft + (w - label.Width) / 2, area.Bottom + TickLength));
            lastLabelRight = drawLeft + w;
        }

        foreach (double t in leftTicks)
        {
            double py = ChartGeometry.YToPixel(left, t);
            dc.DrawLine(pen, new Point(area.Left - TickLength, py), new Point(area.Left, py));
            FormattedText label = Text(t.ToString("F1", CultureInfo.InvariantCulture), Palette.Foreground);
            dc.DrawText(label, new Point(area.Left - TickLength - 2 - label.Width, py - label.Height / 2));
        }
        foreach (double t in rightTicks)
        {
            double py = ChartGeometry.YToPixel(right, t);
            dc.DrawLine(pen, new Point(area.Right, py), new Point(area.Right + TickLength, py));
            FormattedText label = Text(t.ToString("F1", CultureInfo.InvariantCulture), Palette.Foreground);
            dc.DrawText(label, new Point(area.Right + TickLength + 2, py - label.Height / 2));
        }

        // The axis titles; the Y titles read bottom to top beside their axis.
        FormattedText xTitle = Text(PlotViewModel.XAxisLabel, Palette.Foreground);
        dc.DrawText(xTitle, new Point(area.Left + (area.Width - xTitle.Width) / 2, size.Height - textH - OuterMargin / 2));
        if (leftVisible) DrawVertical(dc, Text(vm?.LeftAxisLabel ?? PlotViewModel.LockAxisLabel, Palette.Foreground), OuterMargin, area);
        if (rightVisible) DrawVertical(dc, Text(PlotViewModel.SnrAxisLabel, Palette.Foreground), size.Width - OuterMargin, area);
    }

    private static void DrawVertical(DrawingContext dc, FormattedText text, double centreX, Rect area)
    {
        Matrix m = Matrix.CreateRotation(-Math.PI / 2) * Matrix.CreateTranslation(centreX, area.Center.Y);
        using (dc.PushTransform(m))
        {
            dc.DrawText(text, new Point(-text.Width / 2, -text.Height / 2));
        }
    }

    private void RenderOverlays(DrawingContext dc, Size size)
    {
        if (Plot?.HasData != true) return;
        Rect area = AreaFor(size);
        ChartView view = ViewFor(true, area);
        if (_bandZooming)
        {
            double p0 = ChartGeometry.XToPixel(view, Math.Min(_bandFrom, _bandTo));
            double p1 = ChartGeometry.XToPixel(view, Math.Max(_bandFrom, _bandTo));
            dc.FillRectangle(new ImmutableSolidColorBrush(Color.FromArgb(ZoomBandAlpha, Palette.Foreground.R, Palette.Foreground.G, Palette.Foreground.B)),
                             new Rect(p0, area.Top, Math.Max(0, p1 - p0), area.Height));
        }
        if (_crosshairVisible)
        {
            var pen = new ImmutablePen(new ImmutableSolidColorBrush(Color.FromArgb(CrosshairAlpha, Palette.Foreground.R, Palette.Foreground.G, Palette.Foreground.B)),
                                       1, new ImmutableDashStyle(CrosshairDashes, 0));
            double px = ChartGeometry.XToPixel(view, _crosshairX);
            dc.DrawLine(pen, new Point(px, area.Top), new Point(px, area.Bottom));
        }
    }

    // ---- gestures ---------------------------------------------------------------------------------------------

    protected override void OnPointerWheelChanged(PointerWheelEventArgs e)
    {
        base.OnPointerWheelChanged(e);
        PlotViewModel? vm = Plot;
        if (vm?.HasData != true || e.Delta.Y == 0 || !ChartGeometry.UsableRange(vm.XViewMin, vm.XViewMax)) return;
        // About the cursor, not the centre: the time under the pointer stays put.
        double anchor = PixelToX(e.GetPosition(this).X);
        double factor = e.Delta.Y > 0 ? PlotViewModel.WheelZoomFactor : 1.0 / PlotViewModel.WheelZoomFactor;
        if (vm.WindowZoomedAbout(anchor, factor) is not (double lower, double upper)) return;
        Moving();
        vm.SetXViewByUser(lower, upper);
        e.Handled = true;
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        base.OnPointerPressed(e);
        Focus();
        PlotViewModel? vm = Plot;
        if (vm?.HasData != true) return;
        PointerPointProperties p = e.GetCurrentPoint(this).Properties;
        double x = PixelToX(e.GetPosition(this).X);
        if (e.ClickCount == 2 && p.IsLeftButtonPressed)
        {
            // A double-click restores the whole span.
            vm.ResetXRange();
            e.Handled = true;
            return;
        }
        // A band is dragged out with the middle button, or the left with Ctrl; a plain left drag pans.
        if (p.IsMiddleButtonPressed || (p.IsLeftButtonPressed && e.KeyModifiers.HasFlag(KeyModifiers.Control)))
        {
            _bandZooming = true;
            _bandFrom = _bandTo = x;
            Cursor = new Cursor(StandardCursorType.Cross);
        }
        else if (p.IsLeftButtonPressed)
        {
            _panning = true;
            _panAnchorX = x;
            Cursor = new Cursor(StandardCursorType.SizeWestEast);
        }
        else
        {
            return;
        }
        e.Pointer.Capture(this);
        e.Handled = true;
    }

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        base.OnPointerMoved(e);
        PlotViewModel? vm = Plot;
        if (vm?.HasData != true) return;
        Point pos = e.GetPosition(this);
        if (_bandZooming)
        {
            _bandTo = PixelToX(pos.X);
            ShowCrosshair(false, 0);
            ShowReadout(null);
            return;
        }
        if (_panning)
        {
            // Keep the time grabbed under the cursor.
            double shift = _panAnchorX - PixelToX(pos.X);
            if (shift != 0.0)
            {
                Moving();
                vm.SetXViewByUser(vm.XViewMin + shift, vm.XViewMax + shift);
            }
        }
        double x = PixelToX(pos.X);
        ShowCrosshair(true, x);
        ShowReadout(vm.Readout(x, pos.X, XToPixel));
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e)
    {
        base.OnPointerReleased(e);
        PlotViewModel? vm = Plot;
        if (_bandZooming)
        {
            _bandZooming = false;
            InvalidateVisual();
            vm?.ApplyBandZoom(_bandFrom, PixelToX(e.GetPosition(this).X));
        }
        if (_panning) Settle();   // a released drag is the full picture at once, not after the delay
        _panning = false;
        e.Pointer.Capture(null);
        Cursor = vm?.HasData == true ? new Cursor(StandardCursorType.Hand) : Cursor.Default;
    }

    /// <summary>The capture went elsewhere mid-drag (a window took the mouse, the plot was replaced): the drag ends where
    /// it is. A band is dropped without zooming, a pan settles where it got to, and the cursor is the plot's again;
    /// otherwise the plot would pan with no button down, or keep its band drawn, until the next release (audit B5).
    /// A release reaches here too, after it has ended the drag itself, and finds nothing to end.</summary>
    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e)
    {
        base.OnPointerCaptureLost(e);
        if (!_bandZooming && !_panning) return;
        if (_panning) Settle();
        _bandZooming = false;
        _panning = false;
        Cursor = Plot?.HasData == true ? new Cursor(StandardCursorType.Hand) : Cursor.Default;
        InvalidateVisual();
    }

    protected override void OnPointerExited(PointerEventArgs e)
    {
        base.OnPointerExited(e);
        ShowCrosshair(false, 0);
        ShowReadout(null);
    }

    private void ShowCrosshair(bool visible, double x)
    {
        _crosshairVisible = visible;
        _crosshairX = x;
        InvalidateVisual();   // the overlays only: the data layer is kept
    }

    private void ShowReadout(string? text)
    {
        ToolTip.SetTip(this, text);
        ToolTip.SetIsOpen(this, text is not null);
    }
}
