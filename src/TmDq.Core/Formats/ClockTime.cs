namespace TmDq.Core;

/// <summary>The "DDD:HH:MM:SS" clock stamps the plot's axis and readouts show.</summary>
public static unsafe class ClockTime
{
    /// <summary>
    /// <paramref name="elapsed"/> seconds after the first sample as "DDD:HH:MM:SS" (with ".mmm" if
    /// <paramref name="includeMilliseconds"/>), the first sample being at <paramref name="baseDay"/>,
    /// <paramref name="baseTimeOffset"/> seconds after midnight.
    /// </summary>
    public static string Format(int baseDay, double baseTimeOffset, double elapsed, bool includeMilliseconds = false)
    {
        Library.RequireLoaded();
        return Text.Snprintf((buffer, capacity) =>
            Native.tmdq_clock_time_format(baseDay, baseTimeOffset, elapsed, includeMilliseconds ? 1 : 0, (byte*)buffer, capacity));
    }
}
