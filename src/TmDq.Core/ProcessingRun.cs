using System;

namespace TmDq.Core;

/// <summary>
/// A run in flight: N streams of one recording processed concurrently on the library's own threads. Created
/// by <see cref="Start"/>; reports to the <see cref="IRunObserver"/> it was given. Disposing cancels a run still
/// going, waits for its threads, and releases it - so an observer can be torn down straight after.
/// </summary>
public sealed unsafe class ProcessingRun : IDisposable
{
    private readonly RunHandle _handle;
    private readonly ObserverBridge _bridge;

    private ProcessingRun(ObserverBridge bridge, RunHandle handle)
    {
        _bridge = bridge;
        _handle = handle;
    }

    /// <summary>
    /// Builds a job for every stream (checking each exactly as the application does), opens the recording, and
    /// starts processing; returns at once.
    /// </summary>
    /// <exception cref="TmdqException">
    /// <see cref="Status.BadArgument"/> for a request that is incomplete; <see cref="Status.Job"/> when a stream's
    /// settings are rejected (the message names the stream and the setting); <see cref="Status.Prepare"/> when the
    /// recording cannot be used.
    /// </exception>
    public static ProcessingRun Start(RunRequest request, IRunObserver? observer = null)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(request);

        var bridge = new ObserverBridge(observer);
        try
        {
            using var arena = new Arena();
            RunRequestNative native = ToNative(request, arena);

            CallbacksNative callbacks;
            CallbacksNative* callbacksPointer = bridge.Fill(&callbacks, observer is not null);

            Library.Check(Native.tmdq_run_start(&native, callbacksPointer, out RunHandle handle));
            handle.Bridge = bridge;   // freed with the run, once no native thread can call back
            return new ProcessingRun(bridge, handle);
        }
        catch
        {
            bridge.Dispose();
            throw;
        }
    }

    /// <summary>True from a successful start until just before the observer is told the run has finished.</summary>
    public bool IsRunning
    {
        get
        {
            try
            {
                return Native.tmdq_run_running(_handle) != 0;
            }
            catch (ObjectDisposedException)
            {
                return false;   // released
            }
        }
    }

    /// <summary>The first exception an observer method threw, or null. It was caught, not allowed to unwind the native thread.</summary>
    public Exception? ObserverException => _bridge.ObserverException;

    /// <summary>Asks the run to stop; it ends with <see cref="IRunObserver.OnFinished"/>(false). Safe from any thread.</summary>
    public void Cancel()
    {
        try
        {
            Native.tmdq_run_cancel(_handle);
        }
        catch (ObjectDisposedException)
        {
            // released: nothing is running
        }
    }

    /// <summary>Blocks until every thread of the run has ended, so every observer call has returned.</summary>
    public void Wait()
    {
        try
        {
            Native.tmdq_run_wait(_handle);
        }
        catch (ObjectDisposedException)
        {
            // released: every thread has ended
        }
    }

    /// <summary>
    /// Cancels a run still in flight, waits for it, and releases it. Safe to call twice. A <see cref="Wait"/> on another
    /// thread keeps the run open until it returns, so the run is cancelled first.
    /// </summary>
    public void Dispose()
    {
        Cancel();
        _handle.Dispose();
    }

    private static RunRequestNative ToNative(RunRequest request, Arena arena)
    {
        int count = request.Streams.Count;
        StreamConfigNative** streams = (StreamConfigNative**)arena.Alloc((nuint)(count * sizeof(void*)));
        for (int i = 0; i < count; i++)
        {
            StreamConfigNative* one = arena.Alloc<StreamConfigNative>(1);
            *one = StreamToNative(request.Streams[i], arena);
            streams[i] = one;
        }

        return new RunRequestNative
        {
            StructSize = (uint)sizeof(RunRequestNative),
            TimeChannelId = request.TimeChannelId,
            FileNameUtf8 = arena.Utf8(request.FileName),
            AppRootUtf8 = arena.Utf8(request.AppRoot),
            Streams = count == 0 ? null : streams,
            StreamCount = count,
            SourceId = request.SourceId,
            SwapBytes = request.SwapBytes ? 1 : 0,
        };
    }

    private static StreamConfigNative StreamToNative(StreamConfig s, Arena arena)
    {
        int profileCount = s.Calibration.Count;
        CalProfileNative* profiles = profileCount == 0 ? null : arena.Alloc<CalProfileNative>(profileCount);
        for (int i = 0; i < profileCount; i++)
        {
            CalibrationProfile profile = s.Calibration[i];
            CalPointNative* points = arena.Alloc<CalPointNative>(profile.Points.Count);
            for (int p = 0; p < profile.Points.Count; p++)
            {
                points[p] = new CalPointNative { RawAvg = profile.Points[p].RawAvg, TrueDb = profile.Points[p].TrueDb };
            }
            profiles[i] = new CalProfileNative
            {
                Points = profile.Points.Count == 0 ? null : points,
                WordIndex = profile.WordIndex,
                PointCount = profile.Points.Count,
                Valid = profile.Valid ? 1 : 0,
            };
        }

        return new StreamConfigNative
        {
            StructSize = (uint)sizeof(StreamConfigNative),
            PcmChannelId = s.PcmChannelId,
            LabelUtf8 = arena.Utf8(s.Label),
            SyncPatternUtf8 = arena.Utf8(s.SyncPattern),
            SyncMaskUtf8 = arena.Utf8(s.SyncMask),
            ReceiverParamsTomlUtf8 = arena.Utf8(s.ReceiverParamsToml),
            Calibration = profiles,
            DataRateMbps = s.DataRateMbps,
            ScaleDdBPerV = s.ScaleDdBPerV,
            Mode = (int)s.Mode,
            BitsInMinorFrame = s.BitsInMinorFrame,
            Randomized = s.Randomized ? 1 : 0,
            Inverted = s.Inverted ? 1 : 0,
            SamplePeriodIndex = (int)s.SamplePeriod,
            PolarityIndex = s.PolarityIndex,
            SlopeIndex = s.SlopeIndex,
            NumReceivers = s.NumReceivers,
            ReceiverChannels = s.ReceiverChannels,
            CalibrationCount = profileCount,
        };
    }
}
