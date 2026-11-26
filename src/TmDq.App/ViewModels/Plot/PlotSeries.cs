using System;
using Avalonia.Media;
using TmDq.Core;

namespace TmDq.App.ViewModels.Plot;

/// <summary>One plotted series: its identity, its samples on the shared elapsed axis, and how it is shown.</summary>
public sealed class PlotSeries
{
    public required int Id { get; init; }
    public required string Name { get; set; }
    public required string StreamLabel { get; init; }
    public required MetricType Metric { get; init; }
    public int ReceiverIndex { get; init; }
    public int ChannelIndex { get; init; }
    public int StreamOrder { get; init; }
    public int StreamSequence { get; init; }
    public int SourceId { get; init; }

    /// <summary>Whether the series is a phased array antenna's (US10.0): its channels are the antenna's, its three polarizations are
    /// H, V and C, and it is never exported (the third party's system writes that log).</summary>
    public bool IsPhasedArray { get; init; }

    private double[] _x = Array.Empty<double>();

    /// <summary>Seconds from the shared base; one array per stream, shared by its series, moved in place when a run or an
    /// import moves the base earlier, and replaced (<see cref="MoveTimes"/>) when Remove File moves it later.</summary>
    public required double[] X { get => _x; init => _x = value; }

    /// <summary>Gives the series <paramref name="x"/> as its times, the same samples on a moved base, and says they moved.</summary>
    internal void MoveTimes(double[] x)
    {
        _x = x;
        Version++;
    }

    public required double[] Y { get; init; }
    public double YMin { get; init; }
    public double YMax { get; init; }
    public Color Color { get; set; }

    /// <summary>Whether the line is drawn dotted: the second 15 of a run of streams (see <see cref="PlotColors.Assign(System.Collections.Generic.IEnumerable{PlotSeries})"/>).</summary>
    public bool Dotted { get; set; }

    /// <summary>Whether the line is drawn a little thicker, the strongest of a stream's polarizations: the combined one.</summary>
    public bool Strong { get; set; }

    public bool Visible { get; set; } = true;

    /// <summary>Whether the times are ordered, so the chart can walk only those in view (computed once).</summary>
    public bool SortedX { get; init; }

    /// <summary>Bumped when the samples move, so a drawing made from them is not reused.</summary>
    public int Version { get; internal set; }

    public bool IsLeftAxis => Metric is MetricType.FrameSyncLock or MetricType.AccumulatedMissedFrames;
}
