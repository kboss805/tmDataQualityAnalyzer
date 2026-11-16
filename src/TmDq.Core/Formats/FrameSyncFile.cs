using System;

namespace TmDq.Core;

/// <summary>What a frame-sync pattern file says; anything it does not usefully carry is empty or null.</summary>
/// <param name="Pattern">`FrameSync`, as written (not case-folded); empty if absent.</param>
/// <param name="Mask">`FrameSyncMask`, as written; empty if absent.</param>
/// <param name="BitsPerFrame">The frame length, if the file gives a usable one.</param>
/// <param name="BitsFromLegacyKey">The length came from the older `WordsInMinorFrame` key.</param>
/// <param name="Inverted">`Inverted`; false when the key is absent.</param>
/// <param name="Randomized">`Randomized`, or null when the file has no such key (older files, the shipped patterns among them: leave the operator's choice alone).</param>
public sealed record FrameSyncFileData(
    string Pattern,
    string Mask,
    int? BitsPerFrame,
    bool BitsFromLegacyKey,
    bool Inverted,
    bool? Randomized);

/// <summary>The frame-sync pattern file the stream dialogs load and save: pattern, mask, frame length and the two code-format flags.</summary>
public static unsafe class FrameSyncFile
{
    /// <summary>
    /// Reads a pattern file. A file that is missing, empty or unreadable reads as a file that says nothing
    /// (every field empty or null), as in the application.
    /// </summary>
    public static FrameSyncFileData Read(string path)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(path);

        using var arena = new Arena();
        Library.Check(Native.tmdq_frame_sync_file_read(arena.Utf8(path), out IntPtr file));
        try
        {
            FrameSyncFileInfoNative info = default;
            info.StructSize = (uint)sizeof(FrameSyncFileInfoNative);
            Library.Check(Native.tmdq_frame_sync_file_info_get(file, &info));
            return new FrameSyncFileData(
                Text.Read(info.PatternUtf8),
                Text.Read(info.MaskUtf8),
                info.HasBitsPerFrame != 0 ? info.BitsPerFrame : null,
                info.BitsFromLegacyKey != 0,
                info.Inverted != 0,
                info.HasRandomized != 0 ? info.Randomized != 0 : null);
        }
        finally
        {
            Native.tmdq_frame_sync_file_free(file);
        }
    }

    /// <summary>
    /// Writes a pattern file: the pattern and mask as given, the frame length and the two flags. Data rate and
    /// sample rate are deliberately not written. A null pattern or mask is written as empty.
    /// </summary>
    /// <exception cref="TmdqException"><see cref="Status.Io"/> when the file cannot be created or fully written.</exception>
    public static void Write(string path, string? pattern, string? mask, int bitsPerFrame, bool inverted, bool randomized)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(path);

        using var arena = new Arena();
        Library.Check(Native.tmdq_frame_sync_file_write(
            arena.Utf8(path), arena.Utf8(pattern), arena.Utf8(mask), bitsPerFrame, inverted ? 1 : 0, randomized ? 1 : 0));
    }
}
