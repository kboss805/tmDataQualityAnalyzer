using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using Avalonia.VisualTree;
using TmDq.App.ViewModels.Plot;

namespace TmDq.App.Views.Plot;

/// <summary>
/// A legend that floats over the chart (plan step 4.4, US4.0): a translucent rounded box under a title, of rows, each a line
/// swatch in the series' colour and its label, that scrolls once it reaches its cap (a fraction of the chart, so a dense plot
/// cannot bury the data) and can be dragged anywhere inside the chart. The chart has two, "Framesync Series" at the top left
/// and "SNR Series" at the top right, so which entry is which metric is never a guess. Each sits in its corner of the plot area
/// until it is dragged (the right one stopping short of the right axis's labels, when that axis is shown), and is kept inside
/// the chart afterwards. What one lists is the plot's business (<see cref="PlotViewModel.LegendEntries"/>); this only lays it out.
/// </summary>
public sealed class PlotLegend : Border
{
    public const double EdgeMargin = 8, ContentMargin = 6, RowSpacing = 3, SwatchLength = 22, SwatchThickness = 3, StrongSwatchThickness = 4;
    public const double CornerRadiusPx = 8, MaxWidthFraction = 0.45, MaxHeightFraction = 0.60, MinCapWidth = 80, MinCapHeight = 60;
    public const byte BackgroundAlpha = 185;

    // The Fluent theme's vertical scroll bar, which never auto-hides here, is 16 px wide. The rows always leave that
    // free, so the bar, shown once they overflow, never covers a label (a test holds the theme to it).
    public const double ScrollGutter = 16;

    public const double TitleGap = 4;

    /// <summary>The corner a legend sits in until it is dragged.</summary>
    public enum Corner { Left, Right }

    private readonly StackPanel _rows = new() { Spacing = RowSpacing, Margin = new Thickness(0, 0, ScrollGutter, 0) };
    private readonly ScrollViewer _scroll;
    private readonly TextBlock _title = new() { FontWeight = FontWeight.SemiBold, FontSize = PlotChart.FontSize, Margin = new Thickness(0, 0, 0, TitleGap) };
    private Size _host;
    private double _edge;   // the side it sits against until it is dragged: the plot area's left edge, or its right edge, less the right axis's labels
    private double _top;    // the top of its place until it is dragged
    private bool _dragging, _userMoved;
    private Point _dragStart, _startPos;

    public PlotLegend()
    {
        ApplyPalette();
        ActualThemeVariantChanged += (_, _) => ApplyPalette();
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
        var body = new DockPanel { LastChildFill = true };
        DockPanel.SetDock(_title, Dock.Top);
        body.Children.Add(_title);
        body.Children.Add(_scroll);
        Child = body;
    }

    /// <summary>The corner it sits in until it is dragged: the left for the Framesync legend, the right for the SNR one.</summary>
    public Corner Side { get; set; } = Corner.Right;

    /// <summary>What the legend is called: its heading, over the rows.</summary>
    public string Title
    {
        get => _title.Text ?? "";
        set => _title.Text = value;
    }

    public int RowCount => _rows.Children.Count;

    public IReadOnlyList<Control> Rows => _rows.Children;

    public ScrollViewer Scroll => _scroll;

    /// <summary>Whether the user has dragged it (it no longer returns to the top right).</summary>
    public bool UserMoved => _userMoved;

    public Point Position => new(Canvas.GetLeft(this), Canvas.GetTop(this));

    /// <summary>Lists <paramref name="entries"/>, sized and placed in a chart of <paramref name="host"/>: against
    /// <paramref name="edge"/> (the plot area's left edge for the left corner, its right edge for the right one) and
    /// <paramref name="top"/>. Shown only when <paramref name="wanted"/> and there is a row: the user's choice outranks
    /// having rows, and an empty legend is never drawn.</summary>
    public void Show(IReadOnlyList<LegendEntry> entries, bool wanted, Size host, double edge, double top)
    {
        _host = host;
        _edge = edge;
        _top = top;
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
    public void HostResized(Size host, double edge, double top)
    {
        _host = host;
        _edge = edge;
        _top = top;
        if (RowCount > 0) Fit();
    }

    /// <summary>The colours of the theme in use: translucent over the chart, in either theme (US9.0).</summary>
    public ChartPalette Palette => ChartPalette.For(ActualThemeVariant);

    private void ApplyPalette()
    {
        ChartPalette p = Palette;
        Background = new ImmutableSolidColorBrush(p.LegendBackground);
        BorderBrush = new ImmutableSolidColorBrush(p.LegendBorder);
        _title.Foreground = new ImmutableSolidColorBrush(p.LegendText);
        foreach (Control row in _rows.Children)
        {
            foreach (TextBlock label in ((Panel)row).Children.OfType<TextBlock>())
                label.Foreground = new ImmutableSolidColorBrush(p.LegendText);
            foreach (Border swatch in ((Panel)row).Children.OfType<Border>())
            {
                IBrush brush = new ImmutableSolidColorBrush(p.SeriesColor((Color)swatch.Tag!));
                if (swatch.Child is Avalonia.Controls.Shapes.Line dots) dots.Stroke = brush;   // a dotted series' swatch is its dots
                else swatch.Background = brush;
            }
        }
    }

    private StackPanel Row(LegendEntry e)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Background = Brushes.Transparent };
        // As thick as the line it stands for: the combined polarization is a little thicker.
        double thickness = e.Strong ? StrongSwatchThickness : SwatchThickness;
        var swatch = new Border
        {
            Width = SwatchLength,
            Height = thickness,
            CornerRadius = new CornerRadius(1),
            Tag = e.Color,   // the series' own colour; the swatch is drawn in the theme's version of it
            VerticalAlignment = VerticalAlignment.Center,
        };
        IBrush colour = new ImmutableSolidColorBrush(Palette.SeriesColor(e.Color));
        if (e.Dotted)
        {
            // A dotted series' swatch is dotted, with the same dots as its line (PlotChart.SeriesDots).
            swatch.Child = new Avalonia.Controls.Shapes.Line
            {
                StartPoint = new Point(0, thickness / 2),
                EndPoint = new Point(SwatchLength, thickness / 2),
                Stroke = colour,
                StrokeThickness = thickness,
                StrokeDashArray = new Avalonia.Collections.AvaloniaList<double>(PlotChart.SeriesDots),
            };
        }
        else
        {
            swatch.Background = colour;
        }
        row.Children.Add(swatch);
        row.Children.Add(new TextBlock
        {
            Text = e.Label,
            Foreground = new ImmutableSolidColorBrush(Palette.LegendText),
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
        _title.Measure(Size.Infinity);
        Size content = _rows.DesiredSize;   // includes the scroll gutter, the rows' right margin
        Size title = _title.DesiredSize;    // includes its gap to the rows; the title stays put while the rows scroll
        double frame = 2 * (ContentMargin + 1);
        double maxW = Math.Max(MinCapWidth, _host.Width * MaxWidthFraction);
        double maxH = Math.Max(MinCapHeight, _host.Height * MaxHeightFraction);
        Width = Math.Min(Math.Ceiling(Math.Max(content.Width, title.Width)) + frame, maxW);
        Height = Math.Min(Math.Ceiling(title.Height + content.Height) + frame, maxH);
        if (_userMoved)
        {
            MoveTo(Position);
            return;
        }
        Canvas.SetLeft(this, Side == Corner.Left ? Math.Max(EdgeMargin, _edge + EdgeMargin) : Math.Max(EdgeMargin, _edge - Width - EdgeMargin));
        Canvas.SetTop(this, Math.Max(EdgeMargin, _top));
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
        Cursor = new Cursor(StandardCursorType.Hand);   // the drag's SizeAll goes with it, as on a release
    }
}
