using System;
using System.IO;
using TmDq.Core;

namespace TmDq.App.Streams
{
    /// <summary>
    /// The setup dialogs' Load and Save buttons (US1.0, US5.0, US5.1): the frame-sync pattern file and the
    /// receiver-parameters file, read and written by the core (plan rule 5), applied to a row the way the Qt dialogs
    /// apply them. The one deliberate difference: a save that fails says so (the Qt frame-sync save ignored it).
    /// </summary>
    public static class StreamFiles
    {
        /// <summary>
        /// A pattern file's contents applied to a row: the pattern and mask when the file gives them (upper-cased), the
        /// frame length when it gives one (held to the range the dialog allows, as its spin box holds it), Invert Data always (a file
        /// without the key clears it), and Derandomize only when the file carries the key: an older file, written before
        /// the flag was saved, leaves the operator's choice alone.
        /// </summary>
        public static StreamSetup ApplyFrameSyncFile(StreamSetup s, FrameSyncFileData file)
        {
            StreamSetup r = s;
            if (file.Pattern.Length > 0) r = r with { SyncPattern = file.Pattern.ToUpperInvariant() };
            if (file.Mask.Length > 0) r = r with { SyncMask = file.Mask.ToUpperInvariant() };
            if (file.BitsPerFrame is int bits)
                r = r with { BitsInMinorFrame = Math.Clamp(bits, StreamRules.MinBitsInMinorFrame, StreamRules.MaxBitsInMinorFrame) };
            r = r with { Inverted = file.Inverted };
            if (file.Randomized is bool randomized) r = r with { Randomized = randomized };
            return r;
        }

        public static StreamSetup LoadFrameSync(StreamSetup s, string path) => ApplyFrameSyncFile(s, FrameSyncFile.Read(path));

        /// <summary>Writes the row's pattern, mask, frame length, Invert Data and Derandomize (the data rate and the period
        /// are per-session choices and stay out). The pattern and mask are written trimmed and upper-cased.</summary>
        /// <returns>The path written: <paramref name="path"/>, with ".toml" added when it names no extension.</returns>
        /// <exception cref="TmdqException">The file could not be written.</exception>
        public static string SaveFrameSync(StreamSetup s, string path)
        {
            string target = WithTomlExtension(path);
            FrameSyncFile.Write(target, s.SyncPattern.Trim().ToUpperInvariant(), s.SyncMask.Trim().ToUpperInvariant(),
                                s.BitsInMinorFrame, s.Inverted, s.Randomized);
            return target;
        }

        /// <summary>A receiver-parameters file's settings applied to a row: what the file says replaces what is shown,
        /// what it does not say stays, and the file becomes the row's word map.</summary>
        public static StreamSetup LoadReceiverParams(StreamSetup s, string path)
        {
            ReceiverParams read = ReceiverParamsFile.Read(path, ReceiverParamsOf(s));
            return s with
            {
                PolarityIndex = read.PolarityIndex,
                SlopeIndex = read.SlopeIndex,
                ScaleDdBPerV = read.ScaleDdBPerV,
                NumReceivers = read.NumReceivers,
                ReceiverChannels = read.ReceiverChannels,
                ReceiverParamsToml = path,
            };
        }

        /// <summary>Writes a complete receiver-parameters file: the row's settings, and the word map the row would process
        /// with (its own file's when that loads, else the default map for its counts). The row's word-map file is not
        /// changed by saving.</summary>
        /// <returns>The path written, with ".toml" added when it names no extension.</returns>
        /// <exception cref="TmdqException">No usable word map, or the file could not be written; the message says which.</exception>
        public static string SaveReceiverParams(StreamSetup s, string path)
        {
            string target = WithTomlExtension(path);
            ReceiverParamsFile.Save(target, ReceiverParamsOf(s), s.ReceiverParamsToml, s.BitsInMinorFrame);
            return target;
        }

        public static ReceiverParams ReceiverParamsOf(StreamSetup s) =>
            new(s.PolarityIndex, s.SlopeIndex, s.ScaleDdBPerV, s.NumReceivers, s.ReceiverChannels);

        public const string FrameSyncFolder = "framesync_patterns";
        public const string ReceiverParamsFolder = "receiver_params";

        /// <summary>Where a Load dialog starts: the shipped <paramref name="subfolder"/> of settings/ when it is there, else
        /// the last folder a file was loaded from or saved to (as the Qt dialogs' settingsSubdir). A Save starts at the
        /// last folder.</summary>
        public static string? LoadFolder(string? appRoot, string subfolder, string? lastFolder)
        {
            if (string.IsNullOrEmpty(appRoot)) return lastFolder;
            string shipped = Path.Combine(appRoot, "settings", subfolder);
            return Directory.Exists(shipped) ? shipped : lastFolder;
        }

        /// <summary>The path with ".toml" added when its file name has no extension, as the Qt save dialogs do.</summary>
        public static string WithTomlExtension(string path) => Path.HasExtension(path) ? path : path + ".toml";
    }
}
