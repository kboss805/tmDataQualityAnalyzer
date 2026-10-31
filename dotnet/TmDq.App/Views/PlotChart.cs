using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Media.Immutable;
using TmDq.App.Plot;
using TmDq.Core;

namespace TmDq.App.Views
{
    /// <summary>
    /// The plot (plan step 4.3b), the Avalonia counterpart of the Qt <c>TmChart</c> with the gestures the Qt
    /// <c>PlotWidget</c> adds: one shared time axis and two Y axes (lock percentage or missed frames on the left, Receiver
    /// SNR on the right, each drawn only when something is on it); each series drawn from the core's geometry (its line,
    /// and above two samples a pixel column the band from each column's minimum to its maximum); ticks on round clock
    /// times; wheel zoom about the cursor, drag pan, a band dragged out with Ctrl or the middle button to zoom into it,
    /// double-click for the whole span; a crosshair and the readout of the nearest value.
    ///
    /// The data layer is painted once into a bitmap and reused until the data, a range, the size or the labels change,
    /// so moving the cursor costs the crosshair, not every series again. The state is the <see cref="PlotViewModel"/>'s;
    /// the chart only draws it and reports gestures to it.
    /// </summary>
    public sealed class PlotChart : Control
    {
        public static readonly StyledProperty<PlotViewModel?> PlotProperty =
            AvaloniaProperty.Register<PlotChart, PlotViewModel?>(nameof(Plot));

        // The Qt chart's layout, in pixels.
        public const double OuterMargin = 10, AxisLabelGap = 6, TickLength = 4, TickLabelGap = 12, TopInset = 12;
        public const double FontSize = 12, PenWidth = 1.5;
        public const int XTickCount = 10, YTickCount = 6;
        public const double WheelZoomStep = 0.85;
        public const byte BandAlpha = 90, BandEdgeAlpha = 190, CrosshairAlpha = 140, ZoomBandAlpha = 45;

        // The light theme's palette (the dark theme is plan step 4.8's).
        public static readonly Color Background = Colors.White;
        public static readonly Color Foreground = Color.FromRgb(30, 30, 30);
        public static readonly Color Grid = Color.FromRgb(200, 200, 200);
        public static readonly Color TitleColor = Color.FromRgb(0x00, 0x5F, 0xB8);

        // Keyed by the series itself: a new series reusing an old one's id must not be drawn from the old one's geometry.
        private readonly Dictionary<PlotSeries, (int Version, ChartView View, SeriesGeometry Geometry)> _geometry = new();
        private RenderTargetBitmap? _backing;
        private bool _backingValid;
        private PlotViewModel? _watched;

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
        }

        public PlotViewModel? Plot
        {
            get => GetValue(PlotProperty);
            set => SetValue(PlotProperty, value);
        }

        /// <summary>How many times the data layer has been painted: a cursor move must not add to it.</summary>
        public int DataLayerRenders { get; private set; }

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

        private double TextHeight => Math.Ceiling(Text("Xg", Foreground).Height);

        /// <summary>The rectangle the data is drawn in: the control less the axes' gutters (a hidden axis reserves none),
        /// the title and the headroom kept for the overlays at the top.</summary>
        public Rect PlotArea => AreaFor(Bounds.Size);

        private Rect AreaFor(Size size)
        {
            PlotViewModel? vm = Plot;
            bool leftVisible = vm?.LeftAxisVisible ?? true, rightVisible = vm?.RightAxisVisible ?? false;
            double textH = TextHeight, labelW = Math.Ceiling(Text("-000.0", Foreground).Width);
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

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == PlotProperty)
            {
                if (_watched is not null)
                {
                    _watched.SeriesChanged -= OnDataChanged;
                    _watched.RangesChanged -= OnDataChanged;
                    _watched.PropertyChanged -= OnModelPropertyChanged;
                }
                _watched = Plot;
                if (_watched is not null)
                {
                    _watched.SeriesChanged += OnDataChanged;
                    _watched.RangesChanged += OnDataChanged;
                    _watched.PropertyChanged += OnModelPropertyChanged;
                }
                OnDataChanged(this, EventArgs.Empty);
            }
            else if (change.Property == BoundsProperty)
            {
                _backingValid = false;
            }
        }

        private void OnModelPropertyChanged(object? sender, PropertyChangedEventArgs e) => OnDataChanged(sender, e);

        private void OnDataChanged(object? sender, EventArgs e)
        {
            // Forget the geometry of series no longer shown.
            var current = new HashSet<PlotSeries>(Plot?.Series ?? Array.Empty<PlotSeries>());
            foreach (PlotSeries gone in new List<PlotSeries>(_geometry.Keys))
                if (!current.Contains(gone)) _geometry.Remove(gone);
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
            if (!_backingValid || _backing is null || _backing.PixelSize != pixels)
            {
                _backing?.Dispose();
                _backing = new RenderTargetBitmap(pixels, new Vector(96 * scale, 96 * scale));
                using (DrawingContext into = _backing.CreateDrawingContext())
                {
                    RenderData(into, size);
                }
                _backingValid = true;
                DataLayerRenders++;
            }
            context.DrawImage(_backing, new Rect(size));
            RenderOverlays(context, size);
        }

        /// <summary>The chart at <paramref name="size"/>, without the cursor's crosshair and zoom band: what an export draws.</summary>
        public void RenderTo(DrawingContext context, Size size) => RenderData(context, size);

        private void RenderData(DrawingContext dc, Size size)
        {
            dc.FillRectangle(new ImmutableSolidColorBrush(Background), new Rect(size));
            PlotViewModel? vm = Plot;
            Rect area = AreaFor(size);
            bool hasData = vm?.HasData == true;
            bool leftVisible = vm?.LeftAxisVisible ?? true, rightVisible = vm?.RightAxisVisible ?? false;

            // The title, centred above the plot.
            FormattedText title = Text(vm?.Title ?? PlotViewModel.DefaultTitle, TitleColor, bold: true);
            dc.DrawText(title, new Point((size.Width - title.Width) / 2, OuterMargin / 2));

            // Ticks need the core, which is loaded once there is data; an empty chart is a frame with its labels.
            double[] xTicks = hasData ? ChartGeometry.TimeTicks(vm!.XViewMin, vm.XViewMax, XTickCount, vm.TimeOrigin) : Array.Empty<double>();
            double[] leftTicks = hasData && leftVisible ? ChartGeometry.Ticks(vm!.LeftMin, vm.LeftMax, YTickCount) : Array.Empty<double>();
            double[] rightTicks = hasData && rightVisible ? ChartGeometry.Ticks(vm!.RightMin, vm.RightMax, YTickCount) : Array.Empty<double>();
            ChartView left = ViewFor(true, area), right = ViewFor(false, area);

            // The grid: dotted, at the X ticks and at the Y ticks of the axis drawn (the left when both are).
            var gridPen = new ImmutablePen(new ImmutableSolidColorBrush(Grid), 1, new ImmutableDashStyle(new[] { 1.0, 2.0 }, 0));
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
                        DrawSeries(dc, s, s.IsLeftAxis ? left : right);
                    }
                }
            }

            DrawAxes(dc, size, area, vm, xTicks, leftTicks, rightTicks, left, right, leftVisible, rightVisible);
        }

        private void DrawSeries(DrawingContext dc, PlotSeries s, ChartView view)
        {
            if (!_geometry.TryGetValue(s, out var cached) || cached.Version != s.Version || cached.View != view)
            {
                cached = (s.Version, view, ChartGeometry.Build(s.X, s.Y, s.SortedX, view));
                _geometry[s] = cached;
            }
            SeriesGeometry g = cached.Geometry;

            // The band first, so the mean line reads on top of it. Outlined as well as filled: its edges are the columns'
            // extremes, and stroking them keeps a one-sample excursion sharp.
            if (g.Bands.Count > 0)
            {
                var fill = new ImmutableSolidColorBrush(Color.FromArgb(BandAlpha, s.Color.R, s.Color.G, s.Color.B));
                var edge = new ImmutablePen(new ImmutableSolidColorBrush(Color.FromArgb(BandEdgeAlpha, s.Color.R, s.Color.G, s.Color.B)), 1);
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
            dc.DrawGeometry(null, new ImmutablePen(new ImmutableSolidColorBrush(s.Color), PenWidth, lineJoin: PenLineJoin.Bevel), line);
        }

        private void DrawAxes(DrawingContext dc, Size size, Rect area, PlotViewModel? vm, double[] xTicks, double[] leftTicks,
                              double[] rightTicks, ChartView left, ChartView right, bool leftVisible, bool rightVisible)
        {
            var pen = new ImmutablePen(new ImmutableSolidColorBrush(Foreground), 1);
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
                FormattedText label = Text(vm!.FormatTime(xTicks[i]), Foreground);
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
                FormattedText label = Text(t.ToString("F1", CultureInfo.InvariantCulture), Foreground);
                dc.DrawText(label, new Point(area.Left - TickLength - 2 - label.Width, py - label.Height / 2));
            }
            foreach (double t in rightTicks)
            {
                double py = ChartGeometry.YToPixel(right, t);
                dc.DrawLine(pen, new Point(area.Right, py), new Point(area.Right + TickLength, py));
                FormattedText label = Text(t.ToString("F1", CultureInfo.InvariantCulture), Foreground);
                dc.DrawText(label, new Point(area.Right + TickLength + 2, py - label.Height / 2));
            }

            // The axis titles; the Y titles read bottom to top beside their axis.
            FormattedText xTitle = Text(PlotViewModel.XAxisLabel, Foreground);
            dc.DrawText(xTitle, new Point(area.Left + (area.Width - xTitle.Width) / 2, size.Height - textH - OuterMargin / 2));
            if (leftVisible) DrawVertical(dc, Text(vm?.LeftAxisLabel ?? PlotViewModel.LockAxisLabel, Foreground), OuterMargin, area);
            if (rightVisible) DrawVertical(dc, Text(PlotViewModel.SnrAxisLabel, Foreground), size.Width - OuterMargin, area);
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
                dc.FillRectangle(new ImmutableSolidColorBrush(Color.FromArgb(ZoomBandAlpha, Foreground.R, Foreground.G, Foreground.B)),
                                 new Rect(p0, area.Top, Math.Max(0, p1 - p0), area.Height));
            }
            if (_crosshairVisible)
            {
                var pen = new ImmutablePen(new ImmutableSolidColorBrush(Color.FromArgb(CrosshairAlpha, Foreground.R, Foreground.G, Foreground.B)),
                                           1, new ImmutableDashStyle(new[] { 4.0, 2.0 }, 0));
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
            double factor = e.Delta.Y > 0 ? WheelZoomStep : 1.0 / WheelZoomStep;
            double lower = anchor - (anchor - vm.XViewMin) * factor;
            double upper = anchor + (vm.XViewMax - anchor) * factor;
            if (upper - lower < PlotViewModel.MinZoomSpanSeconds) return;
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
                if (shift != 0.0) vm.SetXViewByUser(vm.XViewMin + shift, vm.XViewMax + shift);
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
            _panning = false;
            e.Pointer.Capture(null);
            Cursor = vm?.HasData == true ? new Cursor(StandardCursorType.Hand) : Cursor.Default;
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
}
