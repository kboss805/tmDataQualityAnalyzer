using System;
using System.Collections.Generic;
using System.Linq;

namespace TmDq.Core;

/// <summary>How one channel's calibration came out (tmdq_calibration_outcome).</summary>
public enum CalibrationOutcome
{
    /// <summary>A valid profile was built.</summary>
    Calibrated = 0,

    /// <summary>No samples for this channel (an absent or disabled word).</summary>
    NoData = 1,

    /// <summary>The samples never moved: one level for the whole recording.</summary>
    FlatNoSignal = 2,

    /// <summary>Fewer confirmed plateaus than expected steps.</summary>
    TooFewPlateaus = 3,

    /// <summary>Enough plateaus, but no run of consecutive levels moving one way.</summary>
    NoMonotonicSweep = 4,
}

/// <summary>
/// One calibration to extract: the calibration recording, the receiver stream's settings (which must be the values
/// the main run will use, because the profiles are matched to words by index), the expected steps and the clips.
/// The same fields the Extract Calibration dialog gathers.
/// </summary>
public sealed record CalibrationRequest
{
    /// <summary>The calibration recording.</summary>
    public required string FileName { get; init; }

    public required int TimeChannelId { get; init; }

    public required int PcmChannelId { get; init; }

    /// <summary>The frame-sync pattern as hex text.</summary>
    public required string SyncPattern { get; init; }

    public required int BitsInMinorFrame { get; init; }

    /// <summary>The expected steps in dB, in order (see <see cref="StepFile.Parse"/>).</summary>
    public required IReadOnlyList<double> Steps { get; init; }

    /// <summary>Hex text; null or empty means every bit of the pattern.</summary>
    public string? SyncMask { get; init; }

    /// <summary>The word-map file; null or empty means the shipped default, if <see cref="AppRoot"/> has one.</summary>
    public string? ReceiverParamsToml { get; init; }

    /// <summary>Where settings/receiver_params/default.toml is looked for; null means nowhere.</summary>
    public string? AppRoot { get; init; }

    public bool Randomized { get; init; }

    public bool Inverted { get; init; }

    /// <summary>File-scoped byte order; an ordinary Chapter 10 recording needs the swap.</summary>
    public bool SwapBytes { get; init; } = true;

    /// <summary>0 means use the rate TMATS declares.</summary>
    public double DataRateMbps { get; init; }

    /// <summary>Seconds of the recording to ignore at the start.</summary>
    public double ClipStartSec { get; init; }

    /// <summary>Seconds of the recording to ignore at the end.</summary>
    public double ClipEndSec { get; init; }

    /// <summary>Used only when no word-map file is resolved.</summary>
    public int NumReceivers { get; init; } = 16;

    /// <summary>Used only when no word-map file is resolved.</summary>
    public int ReceiverChannels { get; init; } = 3;
}

/// <summary>One channel's outcome, as a managed copy.</summary>
public sealed record CalibrationChannel(
    int WordIndex,
    string Name,
    IReadOnlyList<CalibrationPoint> Points,
    IReadOnlyList<double> PlateauLevels,
    IReadOnlyList<double> UnresolvedDb,
    double EdgeThreshold,
    bool Valid,
    bool HadData,
    CalibrationOutcome Outcome,
    int DetectedPlateaus,
    bool ExtraPlateaus,
    bool ThresholdRelaxed,
    bool Saturated,
    int SweepLevels)
{
    /// <summary>The profile to hand to a run (<see cref="StreamConfig.Calibration"/>), which ignores it unless <see cref="Valid"/>.</summary>
    public CalibrationProfile ToProfile() => new(WordIndex, Valid, Points);
}

/// <summary>What a finished calibration says, as a managed copy.</summary>
public sealed record CalibrationResult(
    string Summary,
    string Error,
    string Report,
    double RecordingSeconds,
    bool ClipIgnored,
    IReadOnlyList<CalibrationChannel> Channels)
{
    /// <summary>The usable profiles, ready to pass to a run.</summary>
    public IReadOnlyList<CalibrationProfile> Profiles() => Channels.Where(c => c.Valid).Select(c => c.ToProfile()).ToArray();
}

/// <summary>Reading the step-config files the calibration is paired against.</summary>
public static unsafe class StepFile
{
    /// <summary>The expected steps of an `[[Step]]` step-config file, in file order.</summary>
    /// <exception cref="TmdqException">The file cannot be read or holds no step (<see cref="Status.Prepare"/>), with the parser's reason.</exception>
    public static IReadOnlyList<double> Parse(string path)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(path);

        using var arena = new Arena();
        Library.Check(Native.tmdq_steps_parse(arena.Utf8(path), out IntPtr steps));
        try
        {
            int count = Native.tmdq_steps_count(steps);
            return new ReadOnlySpan<double>(Native.tmdq_steps_db(steps), count).ToArray();
        }
        finally
        {
            Native.tmdq_steps_free(steps);
        }
    }

    /// <summary>
    /// The step file the Extract Calibration dialog opens with: <paramref name="preferred"/> if it names an existing
    /// file, else the shipped default under <paramref name="appRoot"/>, else an empty string.
    /// </summary>
    public static string InitialPath(string? appRoot, string? preferred)
    {
        Library.RequireLoaded();
        using var arena = new Arena();
        IntPtr root = (IntPtr)arena.Utf8(appRoot);
        IntPtr wanted = (IntPtr)arena.Utf8(preferred);
        return Text.Snprintf((buffer, capacity) => Native.tmdq_step_config_initial_path((byte*)root, (byte*)wanted, (byte*)buffer, capacity));
    }
}
