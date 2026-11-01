using System.Linq;
using TmDq.App.Plot;
using TmDq.Core;

namespace TmDq.App.Tests
{
    /// <summary>Series made by hand, for a plot to show without a run (<see cref="PlotViewModel.SetSeries"/>).</summary>
    internal static class PlotFixtures
    {
        public static PlotSeries Make(int id, string name, MetricType metric, int order, int sequence, double[] xs, double[] ys,
                                      int receiver = 0, int channel = 0, int source = 0) => new()
        {
            Id = id,
            Name = name,
            StreamLabel = name,
            Metric = metric,
            ReceiverIndex = receiver,
            ChannelIndex = channel,
            StreamOrder = order,
            StreamSequence = sequence,
            SourceId = source,
            X = xs,
            Y = ys,
            YMin = ys.Length == 0 ? double.MaxValue : ys.Min(),
            YMax = ys.Length == 0 ? double.MinValue : ys.Max(),
            SortedX = ChartGeometry.IsSortedX(xs),
        };

        public static PlotSeries Lock(int id, string name, int order, int sequence, double[] xs, double[] ys) =>
            Make(id, name, MetricType.FrameSyncLock, order, sequence, xs, ys);

        public static PlotSeries Missed(int id, string name, int order, int sequence, double[] xs, double[] ys) =>
            Make(id, name, MetricType.AccumulatedMissedFrames, order, sequence, xs, ys);

        public static PlotSeries Snr(int id, string name, int order, int sequence, int receiver, int channel, double[] xs, double[] ys) =>
            Make(id, name, MetricType.Snr, order, sequence, xs, ys, receiver, channel);
    }
}
