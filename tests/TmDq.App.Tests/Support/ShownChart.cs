using Avalonia;
using Avalonia.Controls;
using Avalonia.Platform;
using TmDq.App.Views.Plot;

namespace TmDq.App.Tests;

/// <summary>A chart shown in a window of its own, for the tests that drive it and read what it paints (<c>PlotTests</c>,
/// <c>PlotDraftTests</c>): where a point of it is in the window, its plot area there, and a frame of the window.</summary>
internal sealed record ShownChart(PlotChart Chart, Window Window)
{
    /// <summary>A point of the chart, in the window's coordinates (where the mouse is driven).</summary>
    public Point At(double x, double y) => Frames.InWindow(Chart, Window).TopLeft + new Point(x, y);

    /// <summary>The chart's plot area, in the window's coordinates.</summary>
    public Rect Area
    {
        get
        {
            Rect c = Frames.InWindow(Chart, Window), a = Chart.PlotArea;
            return new Rect(c.X + a.X, c.Y + a.Y, a.Width, a.Height);
        }
    }

    /// <summary>Renders the window (which is what makes the chart paint) and reads it.</summary>
    public ILockedFramebuffer Frame() => Frames.Capture(Window).Lock();
}
