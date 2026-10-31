using System;
using System.Collections.Generic;

namespace TmDq.Core;

/// <summary>
/// What a series is drawn against: the X range shown, the range of the Y axis the series is measured on, and the plot
/// area in pixels (screen Y grows downward; the area's bottom is <see cref="Top"/> + <see cref="Height"/>).
/// </summary>
public readonly record struct ChartView(double XLower, double XUpper, double YLower, double YUpper,
                                        double Left, double Top, double Width, double Height);

/// <summary>A series' line and band, in pixels.</summary>
/// <param name="Segments">The line, four values a segment (x0, y0, x1, y1); a gap has no segment across it.</param>
/// <param name="Bands">Closed outlines, one per unbroken run of pixel columns, as (x, y) pairs: the columns' minima
/// left to right, then their maxima right to left. Empty unless <paramref name="Decimated"/>.</param>
/// <param name="Decimated">True when the series was drawn from its per-column envelope: above two samples a pixel
/// column, each column's mean is the line and its minimum to maximum the band.</param>
public sealed record SeriesGeometry(double[] Segments, IReadOnlyList<double[]> Bands, bool Decimated)
{
    public int SegmentCount => Segments.Length / 4;
}

/// <summary>
/// The plot's geometry, built by the core (plan decision D1) so this application and the Qt one draw the same pixels:
/// the transforms between data and pixels, a series' line and band, and the X ticks.
/// </summary>
public static unsafe class ChartGeometry
{
    /// <summary>The line and band of the series (<paramref name="xs"/>, <paramref name="ys"/>; only their paired prefix
    /// counts) drawn in <paramref name="view"/>. With <paramref name="sortedX"/> (see <see cref="IsSortedX"/>) only the
    /// samples in view, and one each side, are walked.</summary>
    public static SeriesGeometry Build(ReadOnlySpan<double> xs, ReadOnlySpan<double> ys, bool sortedX, ChartView view)
    {
        Library.RequireLoaded();
        int count = Math.Min(xs.Length, ys.Length);
        ChartViewNative v = ToNative(view);
        IntPtr geometry;
        fixed (double* px = xs)
        fixed (double* py = ys)
        {
            Library.Check(Native.tmdq_chart_geometry(px, py, count, sortedX ? 1 : 0, &v, out geometry));
        }
        try
        {
            GeometryInfoNative info = default;
            info.StructSize = (uint)sizeof(GeometryInfoNative);
            Library.Check(Native.tmdq_geometry_info_get(geometry, &info));
            double[] segments = new ReadOnlySpan<double>(info.Segments, info.SegmentCount * 4).ToArray();
            var bands = new double[info.BandCount][];
            for (int b = 0; b < bands.Length; b++)
            {
                Library.Check(Native.tmdq_geometry_band(geometry, b, out double* points, out int pointCount));
                bands[b] = new ReadOnlySpan<double>(points, pointCount * 2).ToArray();
            }
            return new SeriesGeometry(segments, bands, info.Decimated != 0);
        }
        finally
        {
            Native.tmdq_geometry_free(geometry);
        }
    }

    /// <summary>True when every time after the first is finite and none is smaller than the one before.</summary>
    public static bool IsSortedX(ReadOnlySpan<double> xs)
    {
        Library.RequireLoaded();
        fixed (double* px = xs)
        {
            return Native.tmdq_chart_sorted_x(px, xs.Length) == 1;
        }
    }

    /// <summary>True when [<paramref name="lower"/>, <paramref name="upper"/>] is a finite span of positive width.</summary>
    public static bool UsableRange(double lower, double upper)
    {
        Library.RequireLoaded();
        return Native.tmdq_chart_usable_range(lower, upper) == 1;
    }

    /// <summary>An X value as a pixel column; the area's left edge for an X range that is not usable.</summary>
    public static double XToPixel(ChartView view, double x) => Transform(view, 0, x);

    /// <summary>A pixel column as an X value; the X range's lower end for an area with no width.</summary>
    public static double PixelToX(ChartView view, double px) => Transform(view, 1, px);

    /// <summary>A Y value as a pixel row, larger values higher; the area's bottom for a Y range that is not usable.</summary>
    public static double YToPixel(ChartView view, double y) => Transform(view, 2, y);

    /// <summary>A pixel row as a Y value; the Y range's lower end for an area with no height.</summary>
    public static double PixelToY(ChartView view, double py) => Transform(view, 3, py);

    /// <summary><paramref name="count"/> (at least 2) evenly spaced values from <paramref name="lower"/> to
    /// <paramref name="upper"/>, both ends included; none for a range that is not usable.</summary>
    public static double[] Ticks(double lower, double upper, int count) =>
        ReadTicks((buffer, capacity) => Native.tmdq_chart_ticks(lower, upper, count, buffer, capacity));

    /// <summary>Ticks for a time axis read as DDD:HH:MM:SS: a round step on the round clock times inside the range,
    /// <paramref name="origin"/> being the clock time of x = 0.</summary>
    public static double[] TimeTicks(double lower, double upper, int count, double origin = 0.0) =>
        ReadTicks((buffer, capacity) => Native.tmdq_chart_time_ticks(lower, upper, count, origin, buffer, capacity));

    private delegate int TickWriter(double* buffer, int capacity);

    private static double[] ReadTicks(TickWriter write)
    {
        Library.RequireLoaded();
        int needed = write(null, 0);
        if (needed < 0) throw new TmdqException(Status.BadArgument, Library.LastError());
        var ticks = new double[needed];
        fixed (double* p = ticks)
        {
            write(p, ticks.Length);
        }
        return ticks;
    }

    private static double Transform(ChartView view, int kind, double value)
    {
        Library.RequireLoaded();
        ChartViewNative v = ToNative(view);
        Library.Check(Native.tmdq_chart_transform(&v, kind, value, out double result));
        return result;
    }

    private static ChartViewNative ToNative(ChartView view) => new()
    {
        StructSize = (uint)sizeof(ChartViewNative),
        XLower = view.XLower,
        XUpper = view.XUpper,
        YLower = view.YLower,
        YUpper = view.YUpper,
        Left = view.Left,
        Top = view.Top,
        Width = view.Width,
        Height = view.Height,
    };
}
