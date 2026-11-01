using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using Avalonia.VisualTree;
using TmDq.App.Plot;

namespace TmDq.App.Views
{
    /// <summary>
    /// The legend that floats over the chart (plan step 4.4), the Qt <c>PlotLegendOverlay</c>: a translucent rounded box of
    /// rows, each a line swatch in the series' colour and its label, that scrolls once it reaches its cap (a fraction of the
    /// chart, so a dense plot cannot bury the data) and can be dragged anywhere inside the chart. It sits at the top right
    /// until it is dragged, and is kept inside the chart afterwards. What it lists is the plot's business
    /// (<see cref="PlotViewModel.LegendEntries"/>); this only lays it out.
    /// </summary>
    public sealed class PlotLegend : Border
    {
        public const double EdgeMargin = 8, ContentMargin = 6, RowSpacing = 3, SwatchLength = 22, SwatchThickness = 3;
        public const double CornerRadiusPx = 6, MaxWidthFraction = 0.45, MaxHeightFraction = 0.60, MinCapWidth = 80, MinCapHeight = 60;
        public const byte BackgroundAlpha = 185;

        // The Fluent theme's vertical scroll bar, which never auto-hides here, is 16 px wide. The rows always leave that
        // free, so the bar, shown once they overflow, never covers a label (a test holds the theme to it).
        public const double ScrollGutter = 16;

        public static readonly Color BackgroundColor = Color.FromArgb(BackgroundAlpha, 255, 255, 255);
        public static readonly Color BorderColor = Color.FromRgb(170, 170, 170);
        public static readonly Color TextColor = Color.FromRgb(30, 30, 30);

        private readonly StackPanel _rows = new() { Spacing = RowSpacing, Margin = new Thickness(0, 0, ScrollGutter, 0) };
        private readonly ScrollViewer _scroll;
        private Size _host;
        private bool _dragging, _userMoved;
        private Point _dragStart, _startPos;

        public PlotLegend()
        {
            Background = new ImmutableSolidColorBrush(BackgroundColor);
            BorderBrush = new ImmutableSolidColorBrush(BorderColor);
            BorderThickness = new Thickness(1);
            CornerRadius = new CornerRadius(CornerRadiusPx);
            Padding = new Thickness(ContentMargin);
            Cursor = new Cursor(StandardCursorType.Hand);
            IsVisible = false;
            _scroll = new ScrollViewer
            {
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                AllowAutoHide = false,
                Content = _rows,
            };
            Child = _scroll;
        }

        public int RowCount => _rows.Children.Count;

        public IReadOnlyList<Control> Rows => _rows.Children;

        public ScrollViewer Scroll => _scroll;

        /// <summary>Whether the user has dragged it (it no longer returns to the top right).</summary>
        public bool UserMoved => _userMoved;

        public Point Position => new(Canvas.GetLeft(this), Canvas.GetTop(this));

        /// <summary>Lists <paramref name="entries"/>, sized and placed in a chart of <paramref name="host"/>. Shown only when
        /// <paramref name="wanted"/> and there is a row: the user's choice outranks having rows, and an empty legend is
        /// never drawn.</summary>
        public void Show(IReadOnlyList<LegendEntry> entries, bool wanted, Size host)
        {
            _host = host;
            _rows.Children.Clear();
            foreach (LegendEntry e in entries) _rows.Children.Add(Row(e));
            if (entries.Count == 0)
            {
                IsVisible = false;
                return;
            }
            Fit();
            IsVisible = wanted;
        }

        /// <summary>The chart was resized: the cap follows it, and so does the place (the top right until dragged, inside
        /// the chart after).</summary>
        public void HostResized(Size host)
        {
            _host = host;
            if (RowCount > 0) Fit();
        }

        private static Control Row(LegendEntry e)
        {
            var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Background = Brushes.Transparent };
            row.Children.Add(new Border
            {
                Width = SwatchLength,
                Height = SwatchThickness,
                CornerRadius = new CornerRadius(1),
                Background = new ImmutableSolidColorBrush(e.Color),
                VerticalAlignment = VerticalAlignment.Center,
            });
            row.Children.Add(new TextBlock
            {
                Text = e.Label,
                Foreground = new ImmutableSolidColorBrush(TextColor),
                FontSize = PlotChart.FontSize,
                VerticalAlignment = VerticalAlignment.Center,
            });
            // A short SNR label still leaves the full name a hover away (US4.0).
            if (e.FullName != e.Label) ToolTip.SetTip(row, e.FullName);
            return row;
        }

        /// <summary>Sizes the box to its rows up to the cap, then places it.</summary>
        private void Fit()
        {
            _rows.Measure(Size.Infinity);
            Size content = _rows.DesiredSize;   // includes the scroll gutter, the rows' right margin
            double frame = 2 * (ContentMargin + 1);
            double maxW = Math.Max(MinCapWidth, _host.Width * MaxWidthFraction);
            double maxH = Math.Max(MinCapHeight, _host.Height * MaxHeightFraction);
            Width = Math.Min(Math.Ceiling(content.Width) + frame, maxW);
            Height = Math.Min(Math.Ceiling(content.Height) + frame, maxH);
            if (_userMoved) MoveTo(Position);
            else Canvas.SetLeft(this, Math.Max(EdgeMargin, _host.Width - Width - EdgeMargin));
            if (!_userMoved) Canvas.SetTop(this, EdgeMargin);
        }

        /// <summary>Moves the box, kept a margin inside the chart.</summary>
        private void MoveTo(Point p)
        {
            double maxX = Math.Max(EdgeMargin, _host.Width - Width - EdgeMargin);
            double maxY = Math.Max(EdgeMargin, _host.Height - Height - EdgeMargin);
            Canvas.SetLeft(this, Math.Clamp(p.X, EdgeMargin, maxX));
            Canvas.SetTop(this, Math.Clamp(p.Y, EdgeMargin, maxY));
        }

        // ---- the drag -----------------------------------------------------------------------------------------------

        protected override void OnPointerPressed(PointerPressedEventArgs e)
        {
            base.OnPointerPressed(e);
            if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed || Parent is not Visual host) return;
            // A press on the scroll bar scrolls; anywhere else in the box drags it.
            if (e.Source is Visual v && v.FindAncestorOfType<ScrollBar>(includeSelf: true) is not null) return;
            _dragging = true;
            _dragStart = e.GetPosition(host);
            _startPos = Position;
            Cursor = new Cursor(StandardCursorType.SizeAll);
            e.Pointer.Capture(this);
            e.Handled = true;
        }

        protected override void OnPointerMoved(PointerEventArgs e)
        {
            base.OnPointerMoved(e);
            if (!_dragging || Parent is not Visual host) return;
            MoveTo(_startPos + (e.GetPosition(host) - _dragStart));
            _userMoved = true;
            e.Handled = true;
        }

        protected override void OnPointerReleased(PointerReleasedEventArgs e)
        {
            base.OnPointerReleased(e);
            if (!_dragging) return;
            _dragging = false;
            Cursor = new Cursor(StandardCursorType.Hand);
            e.Pointer.Capture(null);
            e.Handled = true;
        }

        protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e)
        {
            base.OnPointerCaptureLost(e);
            _dragging = false;
        }
    }
}
