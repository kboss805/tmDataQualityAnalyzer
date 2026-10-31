using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using TmDq.App.Processing;
using TmDq.App.Streams;
using TmDq.Core;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// The rules of Configure Streams and its setup dialogs (plan step 4.1a), without a window: readiness and the Process
    /// button, a Mode change, what the dialog opens with and what accepting it keeps, Apply to all (US2.2), the field
    /// checks (US7.0) and the run Process starts. The three Apply-to-all cases port the Qt dialog's tests
    /// (tst_streamconfigdialog.cpp); the three fixes to the Qt behaviour have a test each, named for what they now do.
    /// Nothing here loads the native core.
    /// </summary>
    public sealed class StreamRulesTests
    {
        private static StreamSetup Row(string label = "Ch 01", int id = 1) => new() { PcmChannelId = id, Label = label };

        private static StreamSetup Ready(string label = "Ch 01", int id = 1, StreamMode mode = StreamMode.FrameSyncLock) =>
            Row(label, id) with { Process = true, Configured = true, Mode = mode, SettingsMode = mode };

        private static readonly IReadOnlyList<CalibrationProfile> SomeCalibration =
            new[] { new CalibrationProfile(6, true, new[] { new CalibrationPoint(100, 0), new CalibrationPoint(900, 60) }) };

        /// <summary>A Receiver SNR source whose every copied field differs from a default row's.</summary>
        private static StreamSetup SnrSource() => Ready("Ch 01", 1, StreamMode.ReceiverChannelInfo) with
        {
            SyncPattern = "A345CA5C",
            SyncMask = "FFFF0000",
            BitsInMinorFrame = 1600,
            Randomized = true,
            Inverted = true,
            DataRateMbps = 2.5,
            SamplePeriod = SamplePeriod.TenMilliseconds,
            PolarityIndex = 1,
            SlopeIndex = 1,
            ScaleDdBPerV = 41.5,
            NumReceivers = 7,
            ReceiverChannels = 5,
            ReceiverParamsToml = @"C:\params\rasa.toml",
            Calibration = SomeCalibration,
            CalCh10Path = @"C:\rec\cal.ch10",
            StepTomlPath = @"C:\rec\steps.toml",
            ClipStartSec = 1.5,
            ClipEndSec = 9.5,
        };

        // --- readiness ---

        [Fact]
        public void AnUncheckedRowIsIdleWhateverItHolds()
        {
            Assert.Equal(Readiness.Idle, StreamRules.ReadinessOf(Row()));
            Assert.Equal(Readiness.Idle, StreamRules.ReadinessOf(Ready() with { Process = false }));
            Assert.Equal(Readiness.Idle, StreamRules.ReadinessOf(Row() with { SyncPattern = "" }));
        }

        [Fact]
        public void ACheckedRowIsReadyOnceConfiguredWithAPattern()
        {
            Assert.Equal(Readiness.NeedsConfiguration, StreamRules.ReadinessOf(Row() with { Process = true }));
            Assert.Equal(Readiness.MissingPattern, StreamRules.ReadinessOf(Ready() with { SyncPattern = "" }));
            Assert.Equal(Readiness.MissingPattern, StreamRules.ReadinessOf(Ready() with { SyncPattern = "   " }));
            Assert.Equal(Readiness.Ready, StreamRules.ReadinessOf(Ready()));
        }

        [Fact]
        public void TheReadyColumnSaysWhatTheQtDialogSays()
        {
            Assert.Equal("Click the gear icon to configure this stream.", StreamRules.ReadinessTip(Readiness.NeedsConfiguration));
            Assert.Equal("A frame sync pattern is required.", StreamRules.ReadinessTip(Readiness.MissingPattern));
            Assert.Equal("", StreamRules.ReadinessTip(Readiness.Ready));
            Assert.Equal("", StreamRules.ReadinessTip(Readiness.Idle));
        }

        [Fact]
        public void ProcessWaitsUntilEveryCheckedRowIsReady()
        {
            Assert.False(StreamRules.CanProcess(Array.Empty<StreamSetup>()));
            Assert.False(StreamRules.CanProcess(new[] { Row(), Row("Ch 02", 2) }));
            Assert.True(StreamRules.CanProcess(new[] { Ready() }));
            // An unchecked row, configured or not, is not in the way.
            Assert.True(StreamRules.CanProcess(new[] { Ready(), Row("Ch 02", 2) }));
            // The Qt dialog enabled Process here and ran the second row on whatever it held.
            Assert.False(StreamRules.CanProcess(new[] { Ready(), Row("Ch 02", 2) with { Process = true } }));
            Assert.False(StreamRules.CanProcess(new[] { Ready(), Ready("Ch 02", 2) with { SyncPattern = "" } }));
        }

        [Fact]
        public void WhyNotProcessNamesTheFirstRowInTheWay()
        {
            Assert.Equal("Check at least one stream to process.", StreamRules.WhyNotProcess(new[] { Row() }));
            Assert.Equal("Ch 02: click the gear icon to configure this stream.",
                         StreamRules.WhyNotProcess(new[] { Ready(), Row("Ch 02", 2) with { Process = true }, Row("Ch 03", 3) with { Process = true } }));
            Assert.Equal("Ch 03: a frame sync pattern is required. Click the gear icon to configure this stream.",
                         StreamRules.WhyNotProcess(new[] { Ready(), Row("Ch 02", 2), Ready("Ch 03", 3) with { SyncPattern = " " } }));
            Assert.Null(StreamRules.WhyNotProcess(new[] { Ready(), Row("Ch 02", 2) }));
        }

        // --- a Mode change, and what the dialog opens with ---

        [Fact]
        public void AModeChangeMakesTheRowUnconfigured()
        {
            StreamSetup changed = StreamRules.WithMode(Ready(), StreamMode.ReceiverChannelInfo);
            Assert.Equal(StreamMode.ReceiverChannelInfo, changed.Mode);
            Assert.False(changed.Configured);
            Assert.Equal(Readiness.NeedsConfiguration, StreamRules.ReadinessOf(changed));
            // Its settings still belong to the mode they were made for, so the dialog will offer the new mode's defaults.
            Assert.Equal(StreamMode.FrameSyncLock, changed.SettingsMode);
        }

        [Fact]
        public void ChoosingTheSameModeChangesNothing()
        {
            StreamSetup row = Ready();
            Assert.Same(row, StreamRules.WithMode(row, StreamMode.FrameSyncLock));
        }

        [Fact]
        public void TheDialogOpensOnTheRowWhenItsSettingsAreThisModes()
        {
            StreamSetup row = SnrSource();
            Assert.Same(row, StreamRules.ForEditing(row, appRoot: null));
        }

        [Fact]
        public void TheDialogOpensOnTheOtherModesDefaultsAndKeepsTheRest()
        {
            StreamSetup snr = StreamRules.WithMode(SnrSource(), StreamMode.FrameSyncLock);
            StreamSetup fsl = StreamRules.ForEditing(snr, appRoot: null);
            Assert.Equal(("334AABBF", "FFFFFFFF", 32767), (fsl.SyncPattern, fsl.SyncMask, fsl.BitsInMinorFrame));
            Assert.Equal("", fsl.ReceiverParamsToml);
            Assert.Equal(snr with { SyncPattern = fsl.SyncPattern, SyncMask = fsl.SyncMask, BitsInMinorFrame = fsl.BitsInMinorFrame, ReceiverParamsToml = "" }, fsl);

            StreamSetup lockRow = Ready() with { Randomized = true, DataRateMbps = 4, ReceiverParamsToml = @"C:\p.toml" };
            StreamSetup toSnr = StreamRules.ForEditing(StreamRules.WithMode(lockRow, StreamMode.ReceiverChannelInfo), appRoot: null);
            Assert.Equal(("FE6B2840", "FFFFFFFF", 800), (toSnr.SyncPattern, toSnr.SyncMask, toSnr.BitsInMinorFrame));
            Assert.True(toSnr.Randomized);
            Assert.Equal(4, toSnr.DataRateMbps);
            Assert.Equal(@"C:\p.toml", toSnr.ReceiverParamsToml);
        }

        [Fact]
        public void SwitchingToFrameSyncLockDropsTheReceiverFileForGood()
        {
            // The Qt dialog cleared the file on the copy it edited and then discarded that copy, so the row kept it.
            StreamSetup row = StreamRules.WithMode(SnrSource(), StreamMode.FrameSyncLock);
            StreamSetup accepted = StreamRules.Accept(row, StreamRules.ForEditing(row, appRoot: null));
            Assert.Equal("", accepted.ReceiverParamsToml);
            Assert.Null(StreamRules.ToStreamConfig(accepted).ReceiverParamsToml);
        }

        [Fact]
        public void AcceptingConfiguresTheRowInItsModeWithWhatTheDialogHeld()
        {
            StreamSetup row = StreamRules.WithMode(Row("Ch 04", 4) with { Process = true }, StreamMode.ReceiverChannelInfo);
            StreamSetup edited = SnrSource() with { PcmChannelId = 99, Label = "not mine", Process = false, Mode = StreamMode.FrameSyncLock };
            StreamSetup accepted = StreamRules.Accept(row, edited);

            Assert.True(accepted.Configured);
            Assert.Equal(StreamMode.ReceiverChannelInfo, accepted.SettingsMode);
            Assert.Equal(Readiness.Ready, StreamRules.ReadinessOf(accepted));
            // The row's own identity and choices are never taken from the dialog...
            Assert.Equal((4, "Ch 04", true, StreamMode.ReceiverChannelInfo), (accepted.PcmChannelId, accepted.Label, accepted.Process, accepted.Mode));
            // ...and everything else is.
            Assert.Equal(SnrSource() with { PcmChannelId = 4, Label = "Ch 04" }, accepted);
        }

        // --- Apply to all (US2.2) ---

        [Fact]
        public void ApplyToAllCopiesSettingsToSameModeStreams()
        {
            var rows = new[]
            {
                Ready() with { SyncPattern = "A345CA5C" },
                Row("Ch 02", 2) with { Process = true, SyncPattern = "11111111" },
            };
            IReadOnlyList<StreamSetup> after = StreamRules.ApplyToAll(rows, 0);
            Assert.Equal("A345CA5C", after[0].SyncPattern);
            Assert.Equal("A345CA5C", after[1].SyncPattern);
            Assert.True(after[1].Configured);
            Assert.True(StreamRules.CanProcess(after));
        }

        [Fact]
        public void ApplyToAllLeavesDifferentModeStreamsUnchanged()
        {
            StreamSetup other = Row("Ch 02", 2) with { Process = true, Mode = StreamMode.ReceiverChannelInfo, SyncPattern = "22222222" };
            IReadOnlyList<StreamSetup> after = StreamRules.ApplyToAll(new[] { Ready() with { SyncPattern = "A345CA5C" }, other }, 0);
            Assert.Same(other, after[1]);
        }

        [Fact]
        public void ApplyToAllLeavesUncheckedStreamsUnchanged()
        {
            StreamSetup other = Row("Ch 02", 2) with { SyncPattern = "11111111" };
            IReadOnlyList<StreamSetup> after = StreamRules.ApplyToAll(new[] { Ready() with { SyncPattern = "A345CA5C" }, other }, 0);
            Assert.Same(other, after[1]);
        }

        [Fact]
        public void ApplyToAllFromReceiverSnrCopiesTheReceiverSettingsAndCalibration()
        {
            StreamSetup target = Row("Ch 02", 2) with { Process = true, Mode = StreamMode.ReceiverChannelInfo };
            IReadOnlyList<StreamSetup> after = StreamRules.ApplyToAll(new[] { target, SnrSource() }, 1);
            Assert.Equal(SnrSource() with { PcmChannelId = 2, Label = "Ch 02" }, after[0]);
        }

        [Fact]
        public void ApplyToAllFromFrameSyncLockCopiesOnlyTheFrameSync()
        {
            StreamSetup source = SnrSource() with { Mode = StreamMode.FrameSyncLock, SettingsMode = StreamMode.FrameSyncLock };
            StreamSetup target = Row("Ch 02", 2) with { Process = true };
            IReadOnlyList<StreamSetup> after = StreamRules.ApplyToAll(new[] { source, target }, 0);
            Assert.Equal(target with
            {
                Configured = true,
                SyncPattern = "A345CA5C",
                SyncMask = "FFFF0000",
                BitsInMinorFrame = 1600,
                Randomized = true,
                Inverted = true,
                DataRateMbps = 2.5,
                SamplePeriod = SamplePeriod.TenMilliseconds,
            }, after[1]);
        }

        [Fact]
        public void TheAllBoxSetsEveryRowsProcessAndNothingElse()
        {
            var rows = new[] { Ready(), Row("Ch 02", 2) };
            Assert.Equal(new[] { Ready() with { Process = false }, Row("Ch 02", 2) }, StreamRules.SetAll(rows, false));
            Assert.Equal(new[] { Ready(), Row("Ch 02", 2) with { Process = true } }, StreamRules.SetAll(rows, true));
        }

        // --- the fields (US7.0) ---

        [Theory]
        [InlineData("A34F", false, true)]
        [InlineData("ZXY", false, false)]
        [InlineData("FFFF", true, true)]
        [InlineData("FFZZ", true, false)]
        [InlineData("fe6b2840", false, true)]
        [InlineData(" FE6B2840 ", false, true)]
        [InlineData("0123456789ABCDEF", false, true)]
        [InlineData("0123456789ABCDEF0", false, false)]
        [InlineData("FE6B 2840", false, false)]
        [InlineData("", false, false)]
        [InlineData("", true, true)]
        public void ASyncFieldTakesOneToSixteenHexDigits(string text, bool allowEmpty, bool expected)
        {
            Assert.Equal(expected, StreamRules.IsHexSync(text, allowEmpty));
        }

        [Fact]
        public void TheDefaultSetupsHaveNothingWrong()
        {
            Assert.Empty(StreamRules.Problems(Row()));
            Assert.Empty(StreamRules.Problems(SnrSource()));
            Assert.Empty(StreamRules.Problems(Row() with { SyncMask = "" }));
        }

        [Theory]
        [InlineData("", "FFFFFFFF", 800, StreamRules.PatternRequired)]
        [InlineData("  ", "FFFFFFFF", 800, StreamRules.PatternRequired)]
        [InlineData("FE6B28G0", "FFFFFFFF", 800, "The frame sync pattern must be 1 to 16 hexadecimal digits.")]
        [InlineData("FE6B2840", "FFFFFFFFX", 800, "The frame sync mask must be up to 16 hexadecimal digits.")]
        [InlineData("FE6B2840", "FFFFFFFF", 63, "Bits Per Frame must be from 64 to 65536.")]
        [InlineData("FE6B2840", "FFFFFFFF", 65537, "Bits Per Frame must be from 64 to 65536.")]
        [InlineData("0123456789ABCDEF0123", "FFFFFFFF", 64, "The frame sync pattern must be 1 to 16 hexadecimal digits.")]
        public void AFrameSyncProblemIsNamed(string pattern, string mask, int bits, string problem)
        {
            Assert.Equal(new[] { problem }, StreamRules.Problems(Row() with { SyncPattern = pattern, SyncMask = mask, BitsInMinorFrame = bits }));
        }

        [Fact]
        public void TheLongestPatternFitsTheShortestFrame()
        {
            Assert.Equal(StreamRules.MinBitsInMinorFrame, StreamRules.MaxSyncHexDigits * 4);
            Assert.Empty(StreamRules.Problems(Row() with { SyncPattern = "0123456789ABCDEF", BitsInMinorFrame = StreamRules.MinBitsInMinorFrame }));
        }

        [Theory]
        [InlineData(-0.001, false)]
        [InlineData(0.0, true)]
        [InlineData(1000.0, true)]
        [InlineData(1000.001, false)]
        [InlineData(double.NaN, false)]
        public void TheDataRateIsFromZeroToAThousand(double rate, bool fine)
        {
            Assert.Equal(fine, StreamRules.Problems(Row() with { DataRateMbps = rate }).Count == 0);
        }

        [Fact]
        public void TheReceiverSettingsAreCheckedOnlyForReceiverSnr()
        {
            StreamSetup bad = SnrSource() with { PolarityIndex = 2, SlopeIndex = 4, ScaleDdBPerV = 0, NumReceivers = 0, ReceiverChannels = 101 };
            Assert.Equal(new[]
            {
                "Polarity is invalid.",
                "Slope is invalid.",
                "Scale (dB/V) must be from 0.001 to 999.999.",
                "Num Rcvrs must be from 1 to 100.",
                "Num Channels must be from 1 to 100.",
            }, StreamRules.Problems(bad));
            Assert.Empty(StreamRules.Problems(bad with { Mode = StreamMode.FrameSyncLock }));
        }

        [Theory]
        [InlineData(0, 0, 0.001, 1, 1, true)]
        [InlineData(1, 3, 999.999, 100, 100, true)]
        [InlineData(-1, 3, 20, 16, 3, false)]
        [InlineData(0, -1, 20, 16, 3, false)]
        [InlineData(0, 3, 1000, 16, 3, false)]
        [InlineData(0, 3, double.NaN, 16, 3, false)]
        [InlineData(0, 3, 20, 101, 3, false)]
        [InlineData(0, 3, 20, 16, 0, false)]
        public void TheReceiverSettingsHaveTheDialogsRanges(int polarity, int slope, double scale, int receivers, int channels, bool fine)
        {
            StreamSetup s = SnrSource() with { PolarityIndex = polarity, SlopeIndex = slope, ScaleDdBPerV = scale, NumReceivers = receivers, ReceiverChannels = channels };
            Assert.Equal(fine, StreamRules.Problems(s).Count == 0);
        }

        // --- to the core ---

        [Fact]
        public void TheCoreGetsTheRowWithItsHexTrimmedAndUpperCased()
        {
            StreamConfig c = StreamRules.ToStreamConfig(SnrSource() with { SyncPattern = " a345ca5c ", SyncMask = "ffff0000 " });
            Assert.Equal(new StreamConfig
            {
                PcmChannelId = 1,
                Label = "Ch 01",
                Mode = StreamMode.ReceiverChannelInfo,
                SyncPattern = "A345CA5C",
                SyncMask = "FFFF0000",
                BitsInMinorFrame = 1600,
                Randomized = true,
                Inverted = true,
                DataRateMbps = 2.5,
                SamplePeriod = SamplePeriod.TenMilliseconds,
                PolarityIndex = 1,
                SlopeIndex = 1,
                ScaleDdBPerV = 41.5,
                NumReceivers = 7,
                ReceiverChannels = 5,
                ReceiverParamsToml = @"C:\params\rasa.toml",
                Calibration = SomeCalibration,
            }, c);
        }

        [Fact]
        public void TheRunIsTheCheckedRowsInTableOrder()
        {
            var rows = new[] { Ready("A", 3), Row("B", 5), Ready("C", 7) };
            RunRequest run = StreamRules.ToRunRequest(@"C:\rec\a.ch10", 1, rows, legacyChapter10: false, appRoot: @"C:\app");
            Assert.Equal(@"C:\rec\a.ch10", run.FileName);
            Assert.Equal(1, run.TimeChannelId);
            Assert.Equal(new[] { 3, 7 }, run.Streams.Select(s => s.PcmChannelId));
            Assert.Equal(@"C:\app", run.AppRoot);
        }

        [Fact]
        public void LegacyChapter10IsTheInverseOfTheByteSwap()
        {
            var rows = new[] { Ready() };
            Assert.True(StreamRules.ToRunRequest("a.ch10", 1, rows, legacyChapter10: false, appRoot: null).SwapBytes);
            Assert.False(StreamRules.ToRunRequest("a.ch10", 1, rows, legacyChapter10: true, appRoot: null).SwapBytes);
        }

        [Fact]
        public void NoAppRootIsNull()
        {
            Assert.Null(StreamRules.ToRunRequest("a.ch10", 1, new[] { Ready() }, false, appRoot: null).AppRoot);
            Assert.Null(StreamRules.ToRunRequest("a.ch10", 1, new[] { Ready() }, false, appRoot: "").AppRoot);
        }

        [Fact]
        public void TheTimeChannelListShowsTheNumberAndTheName()
        {
            Assert.Equal("1 - TIME", StreamRules.TimeChannelText(new ChannelInfo(1, "TIME", 0)));
        }

        // --- defaults ---

        [Fact]
        public void TheReceiverSnrDefaultsAreTheQtApplicationsFromTheHeader()
        {
            string header = File.ReadAllText(Repo.File(Path.Combine("include", "constants.h")));
            string Text(string name) => Regex.Match(header, name + @"\s*=\s*""([0-9A-Fa-f]+)""").Groups[1].Value;
            int Number(string name) => int.Parse(Regex.Match(header, @"int\s+" + name + @"\s*=\s*(\d+)\s*;").Groups[1].Value, CultureInfo.InvariantCulture);
            Assert.Equal(ReceiverSnrDefaults.Pattern, Text("kDefaultReceiverSNRPattern"));
            Assert.Equal(ReceiverSnrDefaults.BitsInMinorFrame, Number("kDefaultReceiverSNRBits"));
            Assert.Equal(StreamRules.MinBitsInMinorFrame, Number("kMinFrameLengthBits"));
        }

        [Fact]
        public void ANewRowIsFrameSyncLockOnPrn15AndUnconfigured()
        {
            StreamSetup row = Row();
            Assert.False(row.Process);
            Assert.False(row.Configured);
            Assert.Equal(StreamMode.FrameSyncLock, row.Mode);
            Assert.Equal(StreamMode.FrameSyncLock, row.SettingsMode);
            Assert.Equal((FrameSyncDefaults.Pattern, FrameSyncDefaults.Mask, FrameSyncDefaults.BitsInMinorFrame, FrameSyncDefaults.Period),
                         (row.SyncPattern, row.SyncMask, row.BitsInMinorFrame, row.SamplePeriod));
        }
    }
}
