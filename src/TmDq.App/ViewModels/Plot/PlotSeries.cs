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

    /// <summary>Seconds from the shared base; one array per stream, shared by its series, and moved in place when
    /// the base moves.</summary>
    public required double[] X { get; init; }

    public required double[] Y { get; init; }
    public double YMin { get; init; }
    public double YMax { get; init; }
    public Color Color { get; set; }
    public bool Visible { get; set; } = true;

    /// <summary>Whether the times are ordered, so the chart can walk only those in view (computed once).</summary>
    public bool SortedX { get; init; }

    /// <summary>Bumped when the samples move, so a drawing made from them is not reused.</summary>
    public int Version { get; internal set; }

    public bool IsLeftAxis => Metric is MetricType.FrameSyncLock or MetricType.AccumulatedMissedFrames;
}
