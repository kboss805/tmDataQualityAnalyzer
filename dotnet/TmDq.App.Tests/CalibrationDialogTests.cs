using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Threading;
using TmDq.App.Processing;
using TmDq.App.Services;
using TmDq.App.Streams;
using TmDq.App.ViewModels;
using TmDq.App.Views;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// Non-linear step calibration (plan step 4.6, US5.3): Extract Calibration (the Qt <c>CalibrationSetupDialog</c>), Apply
    /// Cal in the Receiver SNR setup dialog and its report, and the time channel chosen in Configure Streams reaching the
    /// extraction. The extraction itself is the core's, held to the core's own answers by the managed library's tests;
    /// these hold what the dialogs do with it, against step_cal, as <c>TestStreamConfigDialog</c>'s cases did.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class CalibrationDialogTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "tmdq-cal-" + Guid.NewGuid().ToString("N"));

        public CalibrationDialogTests()
        {
            NativeCore.EnsureLoaded();
            Directory.CreateDirectory(_dir);
        }

        public void Dispose() => TempFiles.DeleteDirectory(_dir, recursive: true);

        private static string StepCal => Repo.Recording("step_cal, example.ch10");

        private static string Shipped(string name) => Path.Combine(Repo.Root, "settings", "rcvr_cals", name);

        /// <summary>step_cal's stream as recorded: channel 26, the receiver frame sync, 800-bit frames at 0.8 Mbps.</summary>
        private static CalibrationBase StepCalBase(double rate = 0.8) =>
            new(1, 26, "FE6B2840", "", 800, false, false, rate, true, "", 16, 3);

        private static readonly int[] CalibratedWords = { 0, 1, 2, 6, 7, 8, 12, 13, 14, 15, 16, 17 };

        private sealed class FakeProgress : IProcessingDialog
        {
            public readonly List<string> Shown = new();
            public int Closed;
            public Action<ProcessingViewModel>? OnShow;

            public void Show(ProcessingViewModel vm)
            {
                Shown.Add(vm.Text);
                OnShow?.Invoke(vm);
            }

            public void Close() => Closed++;
        }

        private sealed class FakeCalFiles : ICalibrationFilePicker
        {
            public readonly List<(string Kind, string? Folder)> Asked = new();
            public readonly Queue<string?> Answers = new();

            public Task<string?> OpenStepFileAsync(string? folder)
            {
                Asked.Add(("step", folder));
                return Task.FromResult(Answers.Count > 0 ? Answers.Dequeue() : null);
            }

            public Task<string?> OpenCalibrationRecordingAsync(string? folder)
            {
                Asked.Add(("cal", folder));
                return Task.FromResult(Answers.Count > 0 ? Answers.Dequeue() : null);
            }
        }

        // ---- Extract Calibration's state ----------------------------------------------------------------------------------

        [Fact]
        public void TheDialogOpensOnTheStreamsStepFileElseTheShippedOne()
        {
            var vm = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null);
            Assert.Equal("Extract Calibration", ExtractCalibrationViewModel.Title);
            Assert.Equal(("default.toml", "✓ 11 steps parsed (loaded automatically).", CalibrationStatusKind.Ok), (vm.StepName, vm.StepStatus, vm.StepKind));
            Assert.Equal(11, vm.Steps.Count);
            Assert.Equal(("", "No file selected.", CalibrationStatusKind.Pending), (vm.CalName, vm.CalStatus, vm.CalKind));
            Assert.Equal((0m, 0m), (vm.ClipStart, vm.ClipEnd));
            Assert.False(vm.CanAccept);
            Assert.False(vm.OkCommand.CanExecute(null));
            Assert.Null(vm.Extraction);   // nothing to extract until a recording is chosen

            // The file the stream last used, when it is there.
            var rasa = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, Shipped("RASA.toml"));
            Assert.Equal(("RASA.toml", "✓ 8 steps parsed (loaded automatically)."), (rasa.StepName, rasa.StepStatus));
            var gone = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, Path.Combine(_dir, "gone.toml"));
            Assert.Equal("default.toml", gone.StepName);
            var none = new ExtractCalibrationViewModel(StepCalBase(), null, null);
            Assert.Equal(("", "No file selected.", CalibrationStatusKind.Pending), (none.StepName, none.StepStatus, none.StepKind));
            Assert.Empty(none.Steps);
        }

        [Fact]
        public async Task AStepFileThatCannotBeReadIsACrossAndHoldsTheExtractionBack()
        {
            string bad = Path.Combine(_dir, "bad.toml");
            File.WriteAllText(bad, "this is not = [ a step file");
            var progress = new FakeProgress();
            var vm = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, bad) { ProgressDialog = progress };
            Assert.Equal(("bad.toml", CalibrationStatusKind.Fail), (vm.StepName, vm.StepKind));
            Assert.StartsWith("✗ ", vm.StepStatus);
            Assert.Empty(vm.Steps);

            await vm.LoadCalibrationAsync(StepCal);
            Assert.Equal(("Select a valid step cal file to process this recording.", CalibrationStatusKind.Pending), (vm.CalStatus, vm.CalKind));
            Assert.Empty(progress.Shown);
            Assert.False(vm.CanAccept);

            // A good one chosen by hand extracts at once, and does not say it was loaded automatically.
            await vm.LoadStepFileAsync(Shipped("default.toml"));
            Assert.Equal("✓ 11 steps parsed.", vm.StepStatus);
            Assert.Equal("✓ Calibrated 12 of 48 channel(s).", vm.CalStatus);
            Assert.True(vm.CanAccept);
        }

        [Fact]
        public async Task TheCalibrationRecordingIsExtractedWithTheProgressDialogUp()
        {
            var progress = new FakeProgress();
            var vm = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null) { ProgressDialog = progress };
            int closed = 0;
            vm.CloseRequested += (_, _) => closed++;
            await vm.LoadCalibrationAsync(StepCal);
            Assert.Equal(("step_cal, example.ch10", StepCal), (vm.CalName, vm.CalPath));
            Assert.Equal(new[] { ExtractCalibrationViewModel.ProcessingText }, progress.Shown);
            Assert.Equal(1, progress.Closed);
            Assert.Equal(("✓ Calibrated 12 of 48 channel(s).", CalibrationStatusKind.Ok), (vm.CalStatus, vm.CalKind));
            Assert.Equal(CalibratedWords, vm.Profiles.Select(p => p.WordIndex));
            Assert.All(vm.Profiles, p => Assert.True(p.Valid));
            Assert.Contains("Applied non-linear calibration to 12 of 48 channel(s).", vm.Report);
            Assert.True(vm.CanAccept);
            Assert.True(vm.OkCommand.CanExecute(null));
            vm.OkCommand.Execute(null);
            Assert.Equal((true, 1), (vm.Accepted, closed));

            var cancelled = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null);
            cancelled.CancelCommand.Execute(null);
            Assert.False(cancelled.Accepted);
        }

        public static IEnumerable<object[]> Unreadable() => new[]
        {
            new object[] { StepCalBase() with { SyncPattern = "XYZ" }, "Invalid frame sync pattern 'XYZ'." },
            new object[] { StepCalBase() with { SyncMask = "GG" }, "Invalid frame sync mask 'GG'." },
        };

        [Theory]
        [MemberData(nameof(Unreadable))]
        public async Task ASettingThatCannotBeReadIsACrossBeforeAnythingStarts(CalibrationBase calibrationBase, string why)
        {
            var progress = new FakeProgress();
            var vm = new ExtractCalibrationViewModel(calibrationBase, Repo.Root, null) { ProgressDialog = progress };
            await vm.LoadCalibrationAsync(StepCal);
            Assert.Equal(("✗ " + why, CalibrationStatusKind.Fail), (vm.CalStatus, vm.CalKind));
            Assert.Empty(progress.Shown);
            Assert.False(vm.CanAccept);
            Assert.Empty(vm.Profiles);
        }

        [Fact]
        public async Task AFileThatIsNotARecordingOrNoSyncPatternIsACross()
        {
            string notRecording = Path.Combine(_dir, "notes.ch10");
            File.WriteAllText(notRecording, "not a recording");
            var vm = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null) { ProgressDialog = new FakeProgress() };
            await vm.LoadCalibrationAsync(notRecording);
            Assert.Equal(("✗ Error opening data file.", CalibrationStatusKind.Fail), (vm.CalStatus, vm.CalKind));
            Assert.False(vm.CanAccept);

            // Without a frame sync pattern, the recording is not even taken.
            var noSync = new ExtractCalibrationViewModel(StepCalBase() with { SyncPattern = "  " }, Repo.Root, null);
            await noSync.LoadCalibrationAsync(StepCal);
            Assert.Equal("✗ Set a frame sync pattern before loading the calibration file.", noSync.CalStatus);
            Assert.Equal("", noSync.CalName);
            var files = new FakeCalFiles();
            files.Answers.Enqueue(StepCal);
            var noSyncBrowse = new ExtractCalibrationViewModel(StepCalBase() with { SyncPattern = "" }, Repo.Root, null, files);
            await noSyncBrowse.BrowseCalCommand.ExecuteAsync(null);
            Assert.Empty(files.Asked);   // refused before the file dialog
            Assert.Equal("✗ Set a frame sync pattern before loading the calibration file.", noSyncBrowse.CalStatus);
        }

        [Theory]
        [InlineData(true, false, false, true)]    // as recorded
        [InlineData(false, false, false, false)]  // the other byte order
        [InlineData(true, true, false, false)]    // inverted
        [InlineData(true, false, true, false)]    // derandomized
        public async Task OnlyTheRecordingsOwnByteOrderInversionAndRandomizationCalibrate(bool swap, bool inverted, bool randomized, bool calibrates)
        {
            var vm = new ExtractCalibrationViewModel(StepCalBase() with { SwapBytes = swap, Inverted = inverted, Randomized = randomized },
                                                     Repo.Root, null) { ProgressDialog = new FakeProgress() };
            await vm.LoadCalibrationAsync(StepCal);
            Assert.Equal(calibrates, vm.CanAccept);
            Assert.Equal(calibrates, vm.Profiles.Count > 0);
            Assert.Equal(calibrates ? CalibrationStatusKind.Ok : CalibrationStatusKind.Fail, vm.CalKind);
        }

        private static double RecordingSecondsIn(string status)
        {
            const string marker = "exceed the recording (";
            int at = status.IndexOf(marker, StringComparison.Ordinal) + marker.Length;
            return double.Parse(status[at..status.IndexOf(" s)", at, StringComparison.Ordinal)], CultureInfo.InvariantCulture);
        }

        [Fact]
        public async Task ClipsBeyondTheRecordingAreIgnoredAndSaySoAtTheStreamsDataRate()
        {
            var vm = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null) { ProgressDialog = new FakeProgress() };
            vm.ClipEnd = 1000m;   // no recording yet: nothing extracted
            Assert.Null(vm.Extraction);
            await vm.LoadCalibrationAsync(StepCal);
            Assert.Equal(CalibrationStatusKind.Warn, vm.CalKind);
            Assert.StartsWith("⚠ Clip Start + Clip End (1000.0 s) exceed the recording (", vm.CalStatus);
            Assert.EndsWith(" s) - clips ignored. Calibrated 12 of 48 channel(s).", vm.CalStatus);
            double atRate = RecordingSecondsIn(vm.CalStatus);
            Assert.InRange(atRate, 60, 70);
            Assert.StartsWith(vm.CalStatus[2..vm.CalStatus.IndexOf(" Calibrated", StringComparison.Ordinal)] + "\n\n", vm.Report);
            Assert.True(vm.CanAccept);   // still calibrated, over the whole recording

            // The recording's length follows the stream's rate: at half the rate it is twice as long.
            var half = new ExtractCalibrationViewModel(StepCalBase(0.4), Repo.Root, null) { ProgressDialog = new FakeProgress(), ClipEnd = 1000m };
            await half.LoadCalibrationAsync(StepCal);
            Assert.InRange(RecordingSecondsIn(half.CalStatus) / atRate, 1.8, 2.2);

            // A clip that fits is used, and a change of clip extracts again.
            vm.ClipEnd = 0m;
            await vm.Extraction!;
            vm.ClipStart = 5m;
            await vm.Extraction!;
            Assert.Equal((5.0, 0.0), (vm.ToRequest().ClipStartSec, vm.ToRequest().ClipEndSec));
            Assert.Equal(CalibrationStatusKind.Ok, vm.CalKind);
            Assert.DoesNotContain("clips ignored", vm.Report);
        }

        [Fact]
        public async Task TheExtractionIsAskedForWithTheBaseGivenAndTheTimeChannelIsUsed()
        {
            var calibrationBase = new CalibrationBase(7, 26, "FE6B2840", "FFFF0000", 800, true, true, 0.8, false, Shipped("../receiver_params/RASA.toml"), 12, 3);
            var vm = new ExtractCalibrationViewModel(calibrationBase, Repo.Root, null);
            await vm.LoadCalibrationAsync(StepCal);
            CalibrationRequest r = vm.ToRequest();
            Assert.Equal((StepCal, 7, 26, "FE6B2840", "FFFF0000", 800), (r.FileName, r.TimeChannelId, r.PcmChannelId, r.SyncPattern, r.SyncMask, r.BitsInMinorFrame));
            Assert.Equal((true, true, 0.8, false, 12, 3), (r.Randomized, r.Inverted, r.DataRateMbps, r.SwapBytes, r.NumReceivers, r.ReceiverChannels));
            Assert.Equal((calibrationBase.ReceiverParamsToml, Repo.Root), (r.ReceiverParamsToml, r.AppRoot));
            Assert.Equal(vm.Steps, r.Steps);
            // An empty mask and the default word map are left to the core.
            CalibrationRequest plain = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null).ToRequest();
            Assert.Equal(((string?)null, (string?)null), (plain.SyncMask, plain.ReceiverParamsToml));
            // The time channel only sets the reader's clock, and steps are found in elapsed time, so which one is read
            // cannot be seen in the outcome: the request is what holds it (and Configure Streams' choice reaching it, below).
        }

        [Fact]
        public async Task CancelInTheProgressDialogStopsTheExtraction()
        {
            var progress = new FakeProgress { OnShow = p => p.CancelCommand.Execute(null) };
            var vm = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null) { ProgressDialog = progress };
            await vm.LoadCalibrationAsync(StepCal);
            Assert.Equal(1, progress.Closed);
            Assert.Equal(("✗ Calibration extraction cancelled.", CalibrationStatusKind.Fail), (vm.CalStatus, vm.CalKind));
            Assert.Empty(vm.Profiles);
            Assert.False(vm.CanAccept);
        }

        [Fact]
        public async Task BrowseStartsInTheShippedStepFilesThenWhereItLastWent()
        {
            var files = new FakeCalFiles();
            var vm = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null, files, stepFolder: _dir) { ProgressDialog = new FakeProgress() };
            Assert.True(vm.BrowseStepCommand.CanExecute(null));
            files.Answers.Enqueue(Shipped("TRC.toml"));
            await vm.BrowseStepCommand.ExecuteAsync(null);
            Assert.Equal(("step", Path.Combine(Repo.Root, "settings", "rcvr_cals")), files.Asked[0]);
            Assert.Equal(("TRC.toml", "✓ 11 steps parsed."), (vm.StepName, vm.StepStatus));
            Assert.Equal(Path.GetDirectoryName(Shipped("TRC.toml")), vm.StepFolder);

            // The recording's dialog starts where the last recording came from, else where the step file did.
            files.Answers.Enqueue(StepCal);
            await vm.BrowseCalCommand.ExecuteAsync(null);
            Assert.Equal(("cal", vm.StepFolder), files.Asked[1]);
            Assert.Equal(Path.GetDirectoryName(StepCal), vm.CalFolder);
            Assert.Equal(CalibrationStatusKind.Ok, vm.CalKind);
            await vm.BrowseCalCommand.ExecuteAsync(null);   // cancelled: nothing changes
            Assert.Equal(("cal", Path.GetDirectoryName(StepCal)), files.Asked[2]);
            Assert.Equal(StepCal, vm.CalPath);

            var noDialogs = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null);
            Assert.False(noDialogs.BrowseStepCommand.CanExecute(null));
            Assert.False(noDialogs.BrowseCalCommand.CanExecute(null));
        }

        [Fact]
        public void EachStatusIsMarkedAndColouredAsQtsAre()
        {
            Assert.Equal(("✓ a", "⚠ a", "✗ a", "a"),
                         (ExtractCalibrationViewModel.Marked(CalibrationStatusKind.Ok, "a"), ExtractCalibrationViewModel.Marked(CalibrationStatusKind.Warn, "a"),
                          ExtractCalibrationViewModel.Marked(CalibrationStatusKind.Fail, "a"), ExtractCalibrationViewModel.Marked(CalibrationStatusKind.Pending, "a")));
            Color Of(CalibrationStatusKind k) => ((ISolidColorBrush)ExtractCalibrationViewModel.BrushOf(k)).Color;
            Assert.Equal((Colors.Green, Color.Parse("#DAA520"), Color.Parse("#CC0000"), Colors.Gray),
                         (Of(CalibrationStatusKind.Ok), Of(CalibrationStatusKind.Warn), Of(CalibrationStatusKind.Fail), Of(CalibrationStatusKind.Pending)));
            Assert.Equal("rcvr_cals", StreamFiles.StepCalFolder);
        }

        // ---- Apply Cal, in the Receiver SNR setup dialog ------------------------------------------------------------------

        private static StreamSetup StepCalRow(StreamMode mode = StreamMode.ReceiverChannelInfo) => new()
        {
            PcmChannelId = 26, Label = "CH-16 AGC STEP CALS 800K", Mode = mode, SettingsMode = mode, Process = true,
            TmatsDataRateMbps = 0.8, SyncPattern = "FE6B2840", BitsInMinorFrame = 800, DataRateMbps = 0.8,
        };

        [Fact]
        public void ApplyCalIsForAReceiverSnrStreamWithATimeChannelAndNoProblem()
        {
            var dialogs = new FakeStreamDialogs();
            var vm = new StreamSetupViewModel(StepCalRow(), null, Repo.Root, null, 1, true, dialogs);
            Assert.True(vm.CanApplyCal);
            Assert.True(vm.ApplyCalCommand.CanExecute(null));
            Assert.Equal(StreamSetupViewModel.ApplyCalTipText, vm.ApplyCalTip);
            Assert.Equal(("(none — using linear calibration)", false, 0), (vm.CalibrationText, vm.IsCalibrated, vm.CalibratedChannels));

            Assert.False(new StreamSetupViewModel(StepCalRow(StreamMode.FrameSyncLock), null, Repo.Root, null, 1, true, dialogs).CanApplyCal);
            Assert.False(new StreamSetupViewModel(StepCalRow(), null, Repo.Root, null, 1, true, null).CanApplyCal);
            var noTime = new StreamSetupViewModel(StepCalRow(), null, Repo.Root, null, -1, true, dialogs);
            Assert.False(noTime.CanApplyCal);
            Assert.Equal("Select a Time Channel in the Configure Streams dialog before extracting calibration.", noTime.ApplyCalTip);

            int told = 0;
            vm.ApplyCalCommand.CanExecuteChanged += (_, _) => told++;
            vm.SyncPattern = "";
            Assert.False(vm.CanApplyCal);
            Assert.False(vm.ApplyCalCommand.CanExecute(null));
            Assert.True(told > 0);
            Assert.Equal("Correct the settings above before extracting calibration.", vm.ApplyCalTip);
        }

        [Fact]
        public async Task ApplyCalExtractsWithTheSettingsAsTheyStandAndOkKeepsItsCalibration()
        {
            var dialogs = new FakeStreamDialogs();
            dialogs.OnExtract = async e =>
            {
                e.ProgressDialog = new FakeProgress();
                await e.LoadStepFileAsync(Shipped("default.toml"));
                await e.LoadCalibrationAsync(StepCal);
                e.ClipStart = 2m;
                await e.Extraction!;
                e.OkCommand.Execute(null);
            };
            StreamSetup row = StepCalRow() with { StepTomlPath = Shipped("RASA.toml") };
            var vm = new StreamSetupViewModel(row, null, Repo.Root, null, 1, true, dialogs);
            vm.SyncPattern = " fe6b2840 ";
            vm.NumReceivers = 16;
            await vm.ApplyCalCommand.ExecuteAsync(null);

            ExtractCalibrationViewModel e = Assert.Single(dialogs.Extracted);
            // Opened on the stream's last step file, with the settings as they stand, the time channel and the byte order.
            Assert.Equal(new CalibrationBase(1, 26, "FE6B2840", "FFFFFFFF", 800, false, false, 0.8, true, "", 16, 3), e.Base);
            Assert.Equal(("12 channel(s) calibrated (non-linear)", true), (vm.CalibrationText, vm.IsCalibrated));
            Assert.Equal(new[] { ("Calibration Extracted", e.Report) }, dialogs.Reports);

            StreamSetup kept = vm.ToSetup();
            Assert.Equal(CalibratedWords, kept.Calibration.Select(p => p.WordIndex));
            Assert.Equal((StepCal, Shipped("default.toml"), 2.0, 0.0), (kept.CalCh10Path, kept.StepTomlPath, kept.ClipStartSec, kept.ClipEndSec));

            // Opened again, it starts on the step file just used; cancelled, it changes nothing.
            dialogs.OnExtract = e2 => { e2.CancelCommand.Execute(null); return Task.CompletedTask; };
            await vm.ApplyCalCommand.ExecuteAsync(null);
            Assert.Equal(Shipped("default.toml"), dialogs.Extracted[1].StepPath);
            Assert.Single(dialogs.Reports);
            Assert.Equal(12, vm.ToSetup().Calibration.Count);
        }

        [Fact]
        public void TheRowOpensWithTheCalibrationItHas()
        {
            var profile = new CalibrationProfile(3, true, new[] { new CalibrationPoint(1, 2), new CalibrationPoint(3, 4) });
            StreamSetup row = StepCalRow() with { Calibration = new[] { profile, profile with { WordIndex = 4, Valid = false } } };
            var vm = new StreamSetupViewModel(row, null, Repo.Root, null, 1, true, new FakeStreamDialogs());
            Assert.Equal(("1 channel(s) calibrated (non-linear)", 1), (vm.CalibrationText, vm.CalibratedChannels));
            Assert.Equal(row.Calibration, vm.ToSetup().Calibration);
        }

        [Fact]
        public async Task ApplyCalWithAReportlessExtractionSaysHowManyChannels()
        {
            var dialogs = new FakeStreamDialogs();
            var vm = new StreamSetupViewModel(StepCalRow(), null, Repo.Root, null, 1, true, dialogs);
            dialogs.OnExtract = async e =>
            {
                e.ProgressDialog = new FakeProgress();
                await e.LoadCalibrationAsync(StepCal);
                typeof(ExtractCalibrationViewModel).GetProperty(nameof(ExtractCalibrationViewModel.Report))!.SetValue(e, "");
                e.OkCommand.Execute(null);
            };
            await vm.ApplyCalCommand.ExecuteAsync(null);
            Assert.Equal(new[] { ("Calibration Extracted", "Applied non-linear calibration to 12 channel(s).") }, dialogs.Reports);
        }

        // ---- from Configure Streams to the run ----------------------------------------------------------------------------

        [Fact]
        public async Task ConfigureStreamsHandsTheChosenTimeChannelAndByteOrderToTheExtraction()
        {
            // The PRN sample with CH-04 retyped as a time channel (16), as ConfigureStreamsTests makes it: two time channels.
            byte[] bytes = File.ReadAllBytes(Repo.Recording("prn_testfile, sample.ch10"));
            byte[] from = Encoding.ASCII.GetBytes(@"R-1\CDT-9:PCMIN;"), to = Encoding.ASCII.GetBytes(@"R-1\CDT-9:11   ;");
            to.CopyTo(bytes, bytes.AsSpan().IndexOf(from));
            string copy = Path.Combine(_dir, "two-time.ch10");
            File.WriteAllBytes(copy, bytes);

            var dialogs = new FakeStreamDialogs
            {
                OnEdit = async setup =>
                {
                    await setup.ApplyCalCommand.ExecuteAsync(null);
                    setup.CancelCommand.Execute(null);
                },
                OnExtract = e => { e.CancelCommand.Execute(null); return Task.CompletedTask; },
            };
            var vm = new ConfigureStreamsViewModel(copy, StreamsRun.Open(copy), dialogs, null, Repo.Root);
            StreamRowViewModel row = vm.Rows[0];
            row.Process = true;
            row.ModeIndex = 0;   // Receiver SNR
            vm.TimeChannelIndex = 1;
            vm.LegacyChapter10 = true;
            await row.ConfigureCommand.ExecuteAsync(null);
            ExtractCalibrationViewModel e = Assert.Single(dialogs.Extracted);
            Assert.Equal((16, false), (e.Base.TimeChannelId, e.Base.SwapBytes));   // not the recording's first, not swapped

            vm.TimeChannelIndex = 0;
            vm.LegacyChapter10 = false;
            await row.ConfigureCommand.ExecuteAsync(null);
            Assert.Equal((1, true), (dialogs.Extracted[1].Base.TimeChannelId, dialogs.Extracted[1].Base.SwapBytes));
        }

        private sealed class RunLines : IRunLog
        {
            public readonly List<string> Lines = new();

            public void Line(string message)
            {
                lock (Lines) Lines.Add(message);
            }

            public void Error(string message) => Line(message);
        }

        [Fact]
        public async Task ACalibrationAppliedInTheDialogsReachesTheRun()
        {
            var calFiles = new FakeCalFiles();
            var dialogs = new FakeStreamDialogs
            {
                OnEdit = async setup =>
                {
                    await setup.ApplyCalCommand.ExecuteAsync(null);
                    setup.OkCommand.Execute(null);
                },
                OnExtract = async e =>
                {
                    e.ProgressDialog = new FakeProgress();
                    calFiles.Answers.Enqueue(StepCal);
                    await e.BrowseCalCommand.ExecuteAsync(null);
                    e.OkCommand.Execute(null);
                },
            };
            var vm = new ConfigureStreamsViewModel(StepCal, StreamsRun.Open(StepCal), dialogs, null, Repo.Root, calFiles);
            StreamRowViewModel row = vm.Rows[0];
            row.Process = true;
            row.ModeIndex = 0;
            await row.ConfigureCommand.ExecuteAsync(null);
            Assert.Equal(12, row.Setup.Calibration.Count);
            Assert.Equal(StepCal, row.Setup.CalCh10Path);

            RunRequest request = vm.ToRunRequest();
            Assert.Equal(CalibratedWords, request.Streams[0].Calibration.Select(p => p.WordIndex));
            var log = new RunLines();
            using RunOutcome outcome = StreamsRun.Run(request, null, log, TestContext.Current.CancellationToken);
            Assert.Single(outcome.Results);
            Assert.Contains(log.Lines, l => l.Contains("non-linear calibration — provided 12 profile(s), attached 12", StringComparison.Ordinal));

            // The folder the recording came from is where the next dialog's Browse starts.
            await row.ConfigureCommand.ExecuteAsync(null);
            Assert.Equal(("cal", Path.GetDirectoryName(StepCal)), calFiles.Asked[^1]);
        }

        // ---- the windows ---------------------------------------------------------------------------------------------------

        [AvaloniaFact]
        public void TheExtractCalibrationWindowShowsTheDialogsState()
        {
            var vm = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null, new FakeCalFiles());
            var window = new ExtractCalibrationWindow { DataContext = vm };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("Extract Calibration", window.Title);
            Assert.Equal("default.toml", window.FindControl<TextBlock>("StepName")!.Text);
            TextBlock step = window.FindControl<TextBlock>("StepStatus")!, cal = window.FindControl<TextBlock>("CalStatus")!;
            Assert.Equal(("✓ 11 steps parsed (loaded automatically).", Colors.Green), (step.Text, ((ISolidColorBrush)step.Foreground!).Color));
            Assert.Equal(("No file selected.", Colors.Gray), (cal.Text, ((ISolidColorBrush)cal.Foreground!).Color));
            NumericUpDown start = window.FindControl<NumericUpDown>("ClipStart")!, end = window.FindControl<NumericUpDown>("ClipEnd")!;
            Assert.Equal((0m, 0m, 100000m, "0.0"), (start.Value, start.Minimum, start.Maximum, start.FormatString));
            Assert.Equal((0m, 100000m), (end.Value, end.Maximum));
            Assert.Same(vm.BrowseStepCommand, window.FindControl<Button>("BrowseStep")!.Command);
            Assert.Same(vm.BrowseCalCommand, window.FindControl<Button>("BrowseCal")!.Command);
            Button ok = window.FindControl<Button>("OkButton")!;
            Assert.True(ok.IsDefault);
            Assert.True(window.FindControl<Button>("CancelButton")!.IsCancel);
            Assert.False(ok.IsEffectivelyEnabled);
            end.Value = 12m;
            Assert.Equal(12m, vm.ClipEnd);
            window.Close();
        }

        [AvaloniaFact]
        public void TheSetupWindowHasApplyCalWithItsCalibrationBesideIt()
        {
            var vm = new StreamSetupViewModel(StepCalRow(), null, Repo.Root, null, 1, true, new FakeStreamDialogs());
            var window = new StreamSetupWindow { DataContext = vm };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            Button apply = window.FindControl<Button>("ApplyCal")!;
            Assert.Equal("Apply Cal", apply.Content);
            Assert.Same(vm.ApplyCalCommand, apply.Command);
            Assert.Equal(StreamSetupViewModel.ApplyCalTipText, ToolTip.GetTip(apply));
            Assert.True(apply.IsEffectivelyVisible);
            TextBlock label = window.FindControl<TextBlock>("CalibrationLabel")!;
            Assert.Equal("(none — using linear calibration)", label.Text);
            Assert.Equal(Colors.Gray, ((ISolidColorBrush)label.Foreground!).Color);
            window.Close();

            var calibrated = new StreamSetupViewModel(StepCalRow() with { Calibration = new[] { new CalibrationProfile(0, true, Array.Empty<CalibrationPoint>()) } },
                                                      null, Repo.Root, null, 1, true, new FakeStreamDialogs());
            window = new StreamSetupWindow { DataContext = calibrated };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            label = window.FindControl<TextBlock>("CalibrationLabel")!;
            Assert.Equal("1 channel(s) calibrated (non-linear)", label.Text);
            Assert.NotEqual(Colors.Gray, ((ISolidColorBrush)label.Foreground!).Color);
            window.Close();

            var lockStream = new StreamSetupViewModel(StepCalRow(StreamMode.FrameSyncLock), null, Repo.Root, null, 1, true, new FakeStreamDialogs());
            window = new StreamSetupWindow { DataContext = lockStream };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            Assert.False(window.FindControl<Button>("ApplyCal")!.IsEffectivelyVisible);   // Receiver SNR only
            window.Close();
        }

        [AvaloniaFact]
        public void ExtractCalibrationAndItsReportSitOverTheSetupDialogAndClose()
        {
            var owner = new Window { Width = 400, Height = 300 };
            owner.Show();
            var dialogs = new WindowStreamDialogs(owner);
            var vm = new ExtractCalibrationViewModel(StepCalBase(), Repo.Root, null);
            Task shown = dialogs.ExtractCalibrationAsync(vm);
            Dispatcher.UIThread.RunJobs();
            var window = Assert.IsType<ExtractCalibrationWindow>(Assert.Single(owner.OwnedWindows));
            Assert.True(window.IsDialog);
            Assert.IsType<WindowProcessingDialog>(vm.ProgressDialog);
            vm.CancelCommand.Execute(null);
            Dispatcher.UIThread.RunJobs();
            Assert.True(shown.IsCompleted, "Extract Calibration is still open.");

            Task report = dialogs.ShowReportAsync("Calibration Extracted", "line one\nline two");
            Dispatcher.UIThread.RunJobs();
            var box = Assert.IsType<ReportWindow>(Assert.Single(owner.OwnedWindows));
            Assert.Equal(("Calibration Extracted", "line one\nline two"), (box.Title, box.Body.Text));
            Assert.True(box.OkButton.IsDefault);
            box.OkButton.RaiseEvent(new Avalonia.Interactivity.RoutedEventArgs(Button.ClickEvent));
            Dispatcher.UIThread.RunJobs();
            Assert.True(report.IsCompleted, "the report is still open.");
            owner.Close();
        }
    }
}
