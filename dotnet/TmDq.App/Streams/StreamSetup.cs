using System;
using System.Collections.Generic;
using TmDq.App.Processing;
using TmDq.Core;

namespace TmDq.App.Streams
{
    /// <summary>
    /// One stream's settings in Configure Streams: what the Qt application's StreamConfig carries (US1.0, US2.0, US2.1),
    /// plus the row state that dialog keeps beside it. Immutable: every rule in <see cref="StreamRules"/> takes a setup
    /// and returns a new one, so each can be tested on its own. The core's view of it is
    /// <see cref="StreamRules.ToStreamConfig"/>.
    /// </summary>
    public sealed record StreamSetup
    {
        /// <summary>The PCM channel, as TMATS numbers it.</summary>
        public required int PcmChannelId { get; init; }

        /// <summary>The bare TMATS channel name (the channel number is not part of it).</summary>
        public required string Label { get; init; }

        /// <summary>The rate TMATS declares, in Mbps; 0 if it declares none. Shown where the data rate is left at 0.</summary>
        public double TmatsDataRateMbps { get; init; }

        /// <summary>The row's Process box.</summary>
        public bool Process { get; init; }

        public StreamMode Mode { get; init; } = StreamMode.FrameSyncLock;

        /// <summary>The setup dialog was accepted for this row (or it received an Apply to all) in its current mode.
        /// Unlike the Qt dialog, a Mode change clears it: the row then holds the other mode's settings.</summary>
        public bool Configured { get; init; }

        /// <summary>The mode the frame-sync settings below belong to. When the dialog opens in another mode, that mode's
        /// defaults replace them first (<see cref="StreamRules.ForEditing"/>).</summary>
        public StreamMode SettingsMode { get; init; } = StreamMode.FrameSyncLock;

        // --- frame sync (both modes) ---

        /// <summary>Hex, as typed; up to 16 characters (64 bits).</summary>
        public string SyncPattern { get; init; } = FrameSyncDefaults.Pattern;

        /// <summary>Hex; empty means every bit of the pattern.</summary>
        public string SyncMask { get; init; } = FrameSyncDefaults.Mask;

        public int BitsInMinorFrame { get; init; } = FrameSyncDefaults.BitsInMinorFrame;

        /// <summary>Derandomize (RNRZ-L).</summary>
        public bool Randomized { get; init; }

        /// <summary>Invert Data.</summary>
        public bool Inverted { get; init; }

        /// <summary>0 means the rate TMATS declares.</summary>
        public double DataRateMbps { get; init; }

        /// <summary>Average Period.</summary>
        public SamplePeriod SamplePeriod { get; init; } = FrameSyncDefaults.Period;

        // --- Receiver SNR ---

        public int PolarityIndex { get; init; }

        public int SlopeIndex { get; init; } = 3;

        public double ScaleDdBPerV { get; init; } = 20.0;

        public int NumReceivers { get; init; } = 16;

        public int ReceiverChannels { get; init; } = 3;

        /// <summary>The receiver-parameters file the word map comes from; empty means the shipped default.</summary>
        public string ReceiverParamsToml { get; init; } = string.Empty;

        // --- the calibration Apply Cal gave (Extract Calibration, plan step 4.6) and what it was extracted from ---

        public IReadOnlyList<CalibrationProfile> Calibration { get; init; } = Array.Empty<CalibrationProfile>();

        public string CalCh10Path { get; init; } = string.Empty;

        public string StepTomlPath { get; init; } = string.Empty;

        public double ClipStartSec { get; init; }

        public double ClipEndSec { get; init; }
    }

    /// <summary>What a row's Ready column shows.</summary>
    public enum Readiness
    {
        /// <summary>Process unchecked: a gray cross, nothing to say.</summary>
        Idle,

        /// <summary>Checked but never configured in its mode: a red cross.</summary>
        NeedsConfiguration,

        /// <summary>Checked and configured, but without a frame sync pattern: a red cross.</summary>
        MissingPattern,

        /// <summary>A green tick: the row will be processed.</summary>
        Ready,
    }
}
