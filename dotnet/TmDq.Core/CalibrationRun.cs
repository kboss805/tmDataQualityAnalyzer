using System;
using System.Collections.Generic;
using System.Threading;

namespace TmDq.Core;

/// <summary>
/// A calibration extraction in flight: a calibration recording read once and every receiver channel's steps
/// detected and turned into a <see cref="CalibrationProfile"/>. Created by <see cref="Start"/>; reports to the
/// <see cref="IRunObserver"/> it was given (<see cref="IRunObserver.OnStreamCompleted"/> is never called, there
/// being no stream result). Disposing cancels one still going, waits for its threads and releases it.
/// </summary>
public sealed unsafe class CalibrationRun : IDisposable
{
    private IntPtr _handle;
    private readonly ObserverBridge _bridge;

    private CalibrationRun(ObserverBridge bridge)
    {
        _bridge = bridge;
    }

    /// <summary>
    /// Checks the request (decoding the sync text and resolving the word map exactly as the main run does) and
    /// starts the extraction; returns at once. A failure the extractor can see before any work starts (no steps, a
    /// word map it cannot use, a recording it cannot open) is not an exception: the observer is told
    /// <see cref="IRunObserver.OnError"/> then <see cref="IRunObserver.OnFinished"/>(false) before this returns,
    /// and the run is returned already finished.
    /// </summary>
    /// <exception cref="TmdqException">
    /// <see cref="Status.BadArgument"/> for a request with no file name or no steps; <see cref="Status.Job"/>
    /// when the sync pattern or mask cannot be read ("Invalid frame sync pattern '...'.").
    /// </exception>
    public static CalibrationRun Start(CalibrationRequest request, IRunObserver? observer = null)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(request);

        var run = new CalibrationRun(new ObserverBridge(observer));
        try
        {
            using var arena = new Arena();
            int stepCount = request.Steps.Count;
            double* steps = arena.Alloc<double>(stepCount);
            for (int i = 0; i < stepCount; i++)
            {
                steps[i] = request.Steps[i];
            }

            var native = new CalibrationRequestNative
            {
                StructSize = (uint)sizeof(CalibrationRequestNative),
                TimeChannelId = request.TimeChannelId,
                CalFileNameUtf8 = arena.Utf8(request.FileName),
                AppRootUtf8 = arena.Utf8(request.AppRoot),
                SyncPatternUtf8 = arena.Utf8(request.SyncPattern),
                SyncMaskUtf8 = arena.Utf8(request.SyncMask),
                ReceiverParamsTomlUtf8 = arena.Utf8(request.ReceiverParamsToml),
                StepDb = stepCount == 0 ? null : steps,
                DataRateMbps = request.DataRateMbps,
                ClipStartSec = request.ClipStartSec,
                ClipEndSec = request.ClipEndSec,
                PcmChannelId = request.PcmChannelId,
                BitsInMinorFrame = request.BitsInMinorFrame,
                Randomized = request.Randomized ? 1 : 0,
                Inverted = request.Inverted ? 1 : 0,
                SwapBytes = request.SwapBytes ? 1 : 0,
                NumReceivers = request.NumReceivers,
                ReceiverChannels = request.ReceiverChannels,
                StepCount = stepCount,
            };

            CallbacksNative callbacks;
            CallbacksNative* callbacksPointer = run._bridge.Fill(&callbacks, observer is not null);

            Library.Check(Native.tmdq_calibration_start(&native, callbacksPointer, out run._handle));
            return run;
        }
        catch
        {
            run._bridge.Dispose();
            throw;
        }
    }

    /// <summary>True from a successful start until just before the observer is told the run has finished.</summary>
    public bool IsRunning => _handle != IntPtr.Zero && Native.tmdq_calibration_running(_handle) != 0;

    /// <summary>The first exception an observer method threw, or null. It was caught, not allowed to unwind the native thread.</summary>
    public Exception? ObserverException => _bridge.ObserverException;

    /// <summary>Asks the extraction to stop; it ends with <see cref="IRunObserver.OnFinished"/>(false). Safe from any thread.</summary>
    public void Cancel()
    {
        IntPtr handle = _handle;
        if (handle != IntPtr.Zero)
        {
            Native.tmdq_calibration_cancel(handle);
        }
    }

    /// <summary>Blocks until every thread of the extraction has ended, so every observer call has returned.</summary>
    public void Wait()
    {
        IntPtr handle = _handle;
        if (handle != IntPtr.Zero)
        {
            Native.tmdq_calibration_wait(handle);
        }
    }

    /// <summary>
    /// What the extraction found: a managed copy, valid after the run is disposed. Available once the run has
    /// finished (the observer was told, or <see cref="Wait"/> returned); a cancelled or failed run still has a
    /// result, whose <see cref="CalibrationResult.Summary"/> and <see cref="CalibrationResult.Error"/> say why.
    /// </summary>
    /// <exception cref="TmdqException"><see cref="Status.State"/> while the extraction is still running.</exception>
    /// <exception cref="ObjectDisposedException">The run has been disposed.</exception>
    public CalibrationResult GetResult()
    {
        IntPtr handle = _handle;
        if (handle == IntPtr.Zero)
        {
            throw new ObjectDisposedException(nameof(CalibrationRun));
        }

        CalibrationInfoNative info = default;
        info.StructSize = (uint)sizeof(CalibrationInfoNative);
        Library.Check(Native.tmdq_calibration_result(handle, &info));

        var channels = new CalibrationChannel[info.ChannelCount];
        for (int i = 0; i < channels.Length; i++)
        {
            CalibrationChannelNative c = default;
            c.StructSize = (uint)sizeof(CalibrationChannelNative);
            Library.Check(Native.tmdq_calibration_channel_result(handle, i, &c));

            var points = new CalibrationPoint[c.PointCount];
            for (int p = 0; p < points.Length; p++)
            {
                points[p] = new CalibrationPoint(c.Points[p].RawAvg, c.Points[p].TrueDb);
            }
            channels[i] = new CalibrationChannel(
                c.WordIndex,
                Text.Read(c.NameUtf8),
                points,
                new ReadOnlySpan<double>(c.PlateauLevels, c.PlateauLevelCount).ToArray(),
                new ReadOnlySpan<double>(c.UnresolvedDb, c.UnresolvedCount).ToArray(),
                c.EdgeThreshold,
                c.Valid != 0,
                c.HadData != 0,
                (CalibrationOutcome)c.Outcome,
                c.DetectedPlateaus,
                c.ExtraPlateaus != 0,
                c.ThresholdRelaxed != 0,
                c.Saturated != 0,
                c.SweepLevels);
        }

        return new CalibrationResult(
            Text.Read(info.SummaryUtf8),
            Text.Read(info.ErrorUtf8),
            Text.Read(info.ReportUtf8),
            info.RecordingSeconds,
            info.ClipIgnored != 0,
            channels);
    }

    /// <summary>Cancels an extraction still in flight, waits for it, and releases it. Safe to call twice.</summary>
    public void Dispose()
    {
        IntPtr handle = Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (handle != IntPtr.Zero)
        {
            Native.tmdq_calibration_free(handle);   // cancels and waits, so no native thread can still call back
            _bridge.Dispose();
        }
        GC.SuppressFinalize(this);
    }

    ~CalibrationRun() => Dispose();
}
