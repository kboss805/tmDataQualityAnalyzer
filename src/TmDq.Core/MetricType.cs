namespace TmDq.Core;

/// <summary>Which kind of measurement a series carries (tmdq_metric_type).</summary>
public enum MetricType
{
    /// <summary>A receiver channel's calibrated SNR, in dB.</summary>
    Snr = 0,

    /// <summary>Frame-sync lock percentage.</summary>
    FrameSyncLock = 1,

    /// <summary>Cumulative missed frames.</summary>
    AccumulatedMissedFrames = 2,
}
