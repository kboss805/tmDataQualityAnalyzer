using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using TmDq.Core;

namespace TmDq.App.Streams
{
    /// <summary>
    /// The rules of Configure Streams and its setup dialogs (US1.0, US2.0 to US2.2, US7.0), as pure functions over
    /// <see cref="StreamSetup"/>. They follow the Qt StreamConfigDialog except in three places where it was wrong, each
    /// a decision recorded in the plan (step 4.1) and pinned by a test:
    /// <list type="bullet">
    /// <item>a checked row that was never configured is not processed with whatever it holds: Process waits until every
    /// checked row is ready (the Qt dialog enabled Process once any row was, and ran the rest on defaults);</item>
    /// <item>changing a configured row's Mode makes it unconfigured (the Qt dialog kept it green, holding the other
    /// mode's frame sync);</item>
    /// <item>opening a row in Frame Sync Lock after Receiver SNR really drops its receiver-parameters file (the Qt
    /// dialog cleared it on a copy it then discarded).</item>
    /// </list>
    /// </summary>
    public static class StreamRules
    {
        public const int MinBitsInMinorFrame = 64;
        public const int MaxBitsInMinorFrame = 65536;
        public const int MaxSyncHexDigits = 16;   // 64 bits
        public const double MaxDataRateMbps = 1000;
        public const double MinScaleDdBPerV = 0.001;
        public const double MaxScaleDdBPerV = 999.999;
        public const int MaxReceivers = 100;
        public const int MaxReceiverChannels = 100;
        public const int MaxSlopeIndex = 3;

        /// <summary>The setup dialog's message for an empty pattern, as the Qt dialog words it.</summary>
        public const string PatternRequired = "A frame sync pattern is required (e.g. FE6B2840).";

        // --- the Ready column and the Process button ---

        public static Readiness ReadinessOf(StreamSetup s)
        {
            if (!s.Process) return Readiness.Idle;
            if (!s.Configured) return Readiness.NeedsConfiguration;
            return s.SyncPattern.Trim().Length == 0 ? Readiness.MissingPattern : Readiness.Ready;
        }

        /// <summary>The Ready column's tooltip; empty when there is nothing to say.</summary>
        public static string ReadinessTip(Readiness r) => r switch
        {
            Readiness.NeedsConfiguration => "Click the gear icon to configure this stream.",
            Readiness.MissingPattern => "A frame sync pattern is required.",
            _ => string.Empty,
        };

        /// <summary>Process is available when at least one row is checked and every checked row is ready.</summary>
        public static bool CanProcess(IEnumerable<StreamSetup> rows)
        {
            bool any = false;
            foreach (StreamSetup s in rows)
            {
                if (!s.Process) continue;
                if (ReadinessOf(s) != Readiness.Ready) return false;
                any = true;
            }
            return any;
        }

        /// <summary>Why the rows cannot be processed, as the dialog says it; null when they can.</summary>
        public static string? WhyNotProcess(IReadOnlyList<StreamSetup> rows)
        {
            if (!rows.Any(s => s.Process)) return "Check at least one stream to process.";
            foreach (StreamSetup s in rows)
            {
                switch (ReadinessOf(s))
                {
                    case Readiness.NeedsConfiguration:
                        return s.Label + ": click the gear icon to configure this stream.";
                    case Readiness.MissingPattern:
                        return s.Label + ": a frame sync pattern is required. Click the gear icon to configure this stream.";
                }
            }
            return null;
        }

        // --- the row's controls ---

        /// <summary>The row's Mode set to <paramref name="mode"/>. A change makes the row unconfigured: what it holds
        /// belongs to the other mode until its dialog is accepted again.</summary>
        public static StreamSetup WithMode(StreamSetup s, StreamMode mode) =>
            mode == s.Mode ? s : s with { Mode = mode, Configured = false };

        /// <summary>What the setup dialog opens with: the row as it is, or, when the row's settings belong to the other
        /// mode, that mode's frame-sync defaults first (and, opening in Frame Sync Lock, no receiver-parameters file).
        /// Derandomize, Invert Data, the data rate, the period and the receiver settings are kept.</summary>
        public static StreamSetup ForEditing(StreamSetup s, string? appRoot)
        {
            if (s.Mode == s.SettingsMode) return s;
            (string pattern, string mask, int bits) = StreamDefaults.FrameSyncFor(s.Mode, appRoot);
            StreamSetup edited = s with { SyncPattern = pattern, SyncMask = mask, BitsInMinorFrame = bits };
            return s.Mode == StreamMode.FrameSyncLock ? edited with { ReceiverParamsToml = string.Empty } : edited;
        }

        /// <summary>The row after its setup dialog is accepted with <paramref name="edited"/> (what
        /// <see cref="ForEditing"/> gave the dialog, as the user left it): configured, its settings now this mode's.</summary>
        public static StreamSetup Accept(StreamSetup row, StreamSetup edited) => row with
        {
            Configured = true,
            SettingsMode = row.Mode,
            SyncPattern = edited.SyncPattern,
            SyncMask = edited.SyncMask,
            BitsInMinorFrame = edited.BitsInMinorFrame,
            Randomized = edited.Randomized,
            Inverted = edited.Inverted,
            DataRateMbps = edited.DataRateMbps,
            SamplePeriod = edited.SamplePeriod,
            PolarityIndex = edited.PolarityIndex,
            SlopeIndex = edited.SlopeIndex,
            ScaleDdBPerV = edited.ScaleDdBPerV,
            NumReceivers = edited.NumReceivers,
            ReceiverChannels = edited.ReceiverChannels,
            ReceiverParamsToml = edited.ReceiverParamsToml,
            Calibration = edited.Calibration,
            CalCh10Path = edited.CalCh10Path,
            StepTomlPath = edited.StepTomlPath,
            ClipStartSec = edited.ClipStartSec,
            ClipEndSec = edited.ClipEndSec,
        };

        /// <summary>
        /// Apply to all (US2.2): <paramref name="source"/>'s settings copied to every other checked row in the same mode,
        /// each then configured. Frame Sync Lock copies the frame sync (pattern, mask, length, Derandomize, Invert Data,
        /// data rate, period); Receiver SNR copies that and the receiver settings, the receiver-parameters file and the
        /// calibration. A row's own channel, label, Process box and mode are never copied; unchecked rows and rows in
        /// the other mode are left alone.
        /// </summary>
        public static IReadOnlyList<StreamSetup> ApplyToAll(IReadOnlyList<StreamSetup> rows, int source)
        {
            StreamSetup from = rows[source];
            var result = new List<StreamSetup>(rows.Count);
            for (int i = 0; i < rows.Count; i++)
            {
                StreamSetup to = rows[i];
                if (i == source || !to.Process || to.Mode != from.Mode)
                {
                    result.Add(to);
                    continue;
                }
                StreamSetup copied = to with
                {
                    Configured = true,
                    SettingsMode = from.Mode,
                    SyncPattern = from.SyncPattern,
                    SyncMask = from.SyncMask,
                    BitsInMinorFrame = from.BitsInMinorFrame,
                    Randomized = from.Randomized,
                    Inverted = from.Inverted,
                    DataRateMbps = from.DataRateMbps,
                    SamplePeriod = from.SamplePeriod,
                };
                if (from.Mode == StreamMode.ReceiverChannelInfo)
                {
                    copied = copied with
                    {
                        PolarityIndex = from.PolarityIndex,
                        SlopeIndex = from.SlopeIndex,
                        ScaleDdBPerV = from.ScaleDdBPerV,
                        NumReceivers = from.NumReceivers,
                        ReceiverChannels = from.ReceiverChannels,
                        ReceiverParamsToml = from.ReceiverParamsToml,
                        Calibration = from.Calibration,
                        CalCh10Path = from.CalCh10Path,
                        StepTomlPath = from.StepTomlPath,
                        ClipStartSec = from.ClipStartSec,
                        ClipEndSec = from.ClipEndSec,
                    };
                }
                result.Add(copied);
            }
            return result;
        }

        /// <summary>The "All" box: every row's Process set to <paramref name="process"/>, nothing else changed.</summary>
        public static IReadOnlyList<StreamSetup> SetAll(IReadOnlyList<StreamSetup> rows, bool process) =>
            rows.Select(s => s with { Process = process }).ToList();

        // --- the setup dialog's fields (US7.0) ---

        /// <summary>Hex digits only, 1 to 16 of them (empty is allowed only where <paramref name="allowEmpty"/>).</summary>
        public static bool IsHexSync(string text, bool allowEmpty)
        {
            string t = text.Trim();
            if (t.Length == 0) return allowEmpty;
            return t.Length <= MaxSyncHexDigits && t.All(Uri.IsHexDigit);
        }

        /// <summary>
        /// What is wrong with a setup the user is about to accept, in the order the dialog shows it; empty when it can be
        /// accepted. The frame sync for both modes, the receiver settings for Receiver SNR. Each is a rule the Qt dialog
        /// kept with a validator, a spin box's range or the core's own check; here it is stated, so a value loaded from a
        /// file cannot slip past it.
        /// </summary>
        public static IReadOnlyList<string> Problems(StreamSetup s)
        {
            var problems = new List<string>();
            if (s.SyncPattern.Trim().Length == 0)
                problems.Add(PatternRequired);
            else if (!IsHexSync(s.SyncPattern, allowEmpty: false))
                problems.Add("The frame sync pattern must be 1 to 16 hexadecimal digits.");
            if (!IsHexSync(s.SyncMask, allowEmpty: true))
                problems.Add("The frame sync mask must be up to 16 hexadecimal digits.");
            if (s.BitsInMinorFrame < MinBitsInMinorFrame || s.BitsInMinorFrame > MaxBitsInMinorFrame)
                problems.Add(FormattableString.Invariant($"Bits Per Frame must be from {MinBitsInMinorFrame} to {MaxBitsInMinorFrame}."));
            // A pattern cannot be longer than its frame: 16 digits are 64 bits, the shortest frame allowed.
            if (s.DataRateMbps < 0 || s.DataRateMbps > MaxDataRateMbps || double.IsNaN(s.DataRateMbps))
                problems.Add("Data Rate must be from 0 (the TMATS rate) to 1000 Mbps.");
            if (s.Mode == StreamMode.ReceiverChannelInfo)
            {
                if (s.PolarityIndex is < 0 or > 1)
                    problems.Add("Polarity is invalid.");
                if (s.SlopeIndex < 0 || s.SlopeIndex > MaxSlopeIndex)
                    problems.Add("Slope is invalid.");
                if (!(s.ScaleDdBPerV >= MinScaleDdBPerV && s.ScaleDdBPerV <= MaxScaleDdBPerV))
                    problems.Add("Scale (dB/V) must be from 0.001 to 999.999.");
                if (s.NumReceivers < 1 || s.NumReceivers > MaxReceivers)
                    problems.Add("Num Rcvrs must be from 1 to 100.");
                if (s.ReceiverChannels < 1 || s.ReceiverChannels > MaxReceiverChannels)
                    problems.Add("Num Channels must be from 1 to 100.");
            }
            return problems;
        }

        // --- to the core ---

        /// <summary>The core's view of a row: the pattern and mask trimmed and upper-cased, as the Qt dialogs hand them on.</summary>
        public static StreamConfig ToStreamConfig(StreamSetup s) => new()
        {
            PcmChannelId = s.PcmChannelId,
            Label = s.Label,
            Mode = s.Mode,
            SyncPattern = s.SyncPattern.Trim().ToUpperInvariant(),
            SyncMask = s.SyncMask.Trim().ToUpperInvariant(),
            BitsInMinorFrame = s.BitsInMinorFrame,
            Randomized = s.Randomized,
            Inverted = s.Inverted,
            DataRateMbps = s.DataRateMbps,
            SamplePeriod = s.SamplePeriod,
            PolarityIndex = s.PolarityIndex,
            SlopeIndex = s.SlopeIndex,
            ScaleDdBPerV = s.ScaleDdBPerV,
            NumReceivers = s.NumReceivers,
            ReceiverChannels = s.ReceiverChannels,
            ReceiverParamsToml = s.ReceiverParamsToml.Length == 0 ? null : s.ReceiverParamsToml,
            Calibration = s.Calibration,
        };

        /// <summary>The run Process starts: the checked rows, in table order, over <paramref name="recording"/>.</summary>
        /// <param name="legacyChapter10">The "Legacy Chapter 10" box: the recording's words are NOT byte-swapped.</param>
        public static RunRequest ToRunRequest(string recording, int timeChannelId, IReadOnlyList<StreamSetup> rows,
                                              bool legacyChapter10, string? appRoot) =>
            new(recording, timeChannelId, rows.Where(s => s.Process).Select(ToStreamConfig).ToList())
            {
                SwapBytes = !legacyChapter10,
                AppRoot = string.IsNullOrEmpty(appRoot) ? null : appRoot,
            };

        /// <summary>The time-channel list's text for a channel: "<id> - <name>", as the Qt dialog lists it (the PCM rows
        /// show the bare name).</summary>
        public static string TimeChannelText(ChannelInfo channel) =>
            channel.Id.ToString(CultureInfo.InvariantCulture) + " - " + channel.Name;
    }
}
