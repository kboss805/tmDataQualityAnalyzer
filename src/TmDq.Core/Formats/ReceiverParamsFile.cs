using System;

namespace TmDq.Core;

/// <summary>The scalar settings of a receiver-parameters file (US5.1). Its word map is not here: the core reads it
/// when a stream is processed, and <see cref="ReceiverParamsFile.Save"/> writes it.</summary>
/// <param name="PolarityIndex">0 positive, 1 negative.</param>
/// <param name="SlopeIndex">Index into the voltage ranges: 0 ±10 V, 1 ±5 V, 2 0–10 V, 3 0–5 V.</param>
/// <param name="ScaleDdBPerV">Calibration scale, dB per volt.</param>
/// <param name="NumReceivers">Receivers contributing channels.</param>
/// <param name="ReceiverChannels">Channels per receiver.</param>
public sealed record ReceiverParams(int PolarityIndex, int SlopeIndex, double ScaleDdBPerV, int NumReceivers, int ReceiverChannels);

/// <summary>The receiver-parameters file the Receiver SNR dialog loads and saves, through the core, so the file
/// is read and written in one place.</summary>
public static unsafe class ReceiverParamsFile
{
    /// <summary>The application's defaults: what a stream starts with, and what a missing key falls back to.</summary>
    public static ReceiverParams Defaults()
    {
        Library.RequireLoaded();
        ReceiverParamsNative result = default;
        result.StructSize = (uint)sizeof(ReceiverParamsNative);
        using var arena = new Arena();
        // An empty path is a file that says nothing, read from the core's own defaults.
        Library.Check(Native.tmdq_receiver_params_read(arena.Utf8(string.Empty), null, &result));
        return FromNative(result);
    }

    /// <summary>
    /// Reads a file's scalar settings. A key the file does not carry keeps its value from <paramref name="current"/>
    /// (pass what is shown; null means <see cref="Defaults"/>), so a partial file changes only what it says; a
    /// missing or unreadable file changes nothing. Counts are read from [Receivers], else the older [Parameters] keys;
    /// a scale or count that is not positive is ignored.
    /// </summary>
    public static ReceiverParams Read(string path, ReceiverParams? current = null)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(path);
        ReceiverParamsNative result = default;
        result.StructSize = (uint)sizeof(ReceiverParamsNative);
        using var arena = new Arena();
        if (current is null)
        {
            Library.Check(Native.tmdq_receiver_params_read(arena.Utf8(path), null, &result));
        }
        else
        {
            ReceiverParamsNative start = ToNative(current);
            Library.Check(Native.tmdq_receiver_params_read(arena.Utf8(path), &start, &result));
        }
        return FromNative(result);
    }

    /// <summary>
    /// Writes a complete file: the scalars, then the word map of <paramref name="wordMapSource"/> when that names a
    /// file that loads in a frame of <paramref name="bitsPerFrame"/> bits, otherwise the default map for the receiver
    /// counts (null or empty means the default map). The file is replaced, never merged.
    /// </summary>
    /// <exception cref="TmdqException"><see cref="Status.Io"/> with the core's reason: no usable word map (more channels
    /// than the frame has words), or the file could not be written.</exception>
    public static void Save(string path, ReceiverParams parameters, string? wordMapSource, int bitsPerFrame)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(parameters);
        ReceiverParamsNative native = ToNative(parameters);
        using var arena = new Arena();
        Library.Check(Native.tmdq_receiver_params_save(arena.Utf8(path), &native, arena.Utf8(wordMapSource),
                                                       FrameGeometry.WordsInMinorFrame(bitsPerFrame)));
    }

    private static ReceiverParamsNative ToNative(ReceiverParams p) => new()
    {
        StructSize = (uint)sizeof(ReceiverParamsNative),
        PolarityIndex = p.PolarityIndex,
        SlopeIndex = p.SlopeIndex,
        ScaleDbPerV = p.ScaleDdBPerV,
        NumReceivers = p.NumReceivers,
        ReceiverChannels = p.ReceiverChannels,
    };

    private static ReceiverParams FromNative(ReceiverParamsNative n) =>
        new(n.PolarityIndex, n.SlopeIndex, n.ScaleDbPerV, n.NumReceivers, n.ReceiverChannels);
}
