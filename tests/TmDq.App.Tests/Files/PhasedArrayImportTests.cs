using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Avalonia.Headless.XUnit;
using TmDq.App.Models.Export;
using TmDq.App.Models.Logging;
using TmDq.App.Models.Processing;
using TmDq.App.ViewModels;
using TmDq.App.ViewModels.Plot;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// Importing a phased array antenna's SNR log (US10.0): the question every import asks, what kind of file it is; the log's
/// 30 streams shown as 90 series that read H, V and C and not as receivers; and nothing of them ever exported. The format is
/// the core's (<c>test_phasedarraylog.cpp</c>, <c>PhasedArrayLogTests</c> in the library's tests); these hold what the application does with it.
/// </summary>
[Collection(NativeCoreCollection.Name)]
public sealed class PhasedArrayImportTests
{
    public PhasedArrayImportTests() => NativeCore.EnsureLoaded();

    private static string Sample => Repo.Recording("av_snr_log.csv");

    private static MainWindowViewModel Window(FakeCsvKinds kinds) => new(new MainWindowServices { CsvKinds = kinds });

    [Fact]
    public void TheDialogOffersTheTwoKindsUnderTheTitlesTheStoryGivesThem()
    {
        Assert.Equal(new[] { CsvKind.TelemetryReceivers, CsvKind.PhasedArrayAntenna }, CsvKinds.All);
        Assert.Equal("Standalone Telemetry Receivers…", CsvKinds.Title(CsvKind.TelemetryReceivers));
        Assert.Equal("AV Phased Array Antenna…", CsvKinds.Title(CsvKind.PhasedArrayAntenna));
    }

    [Fact]
    public async Task TheSampleImportsAsNinetySeriesTitledByItsNameAndIsRemembered()
    {
        var kinds = new FakeCsvKinds();
        kinds.Answers.Enqueue(CsvKind.PhasedArrayAntenna);
        MainWindowViewModel vm = Window(kinds);
        int before = vm.Log.Entries.Count;

        await vm.ImportCsvAsync(Sample);

        Assert.Equal(new[] { "av_snr_log.csv" }, kinds.Asked);
        Assert.Equal(new[] { "Info Importing CSV: av_snr_log.csv", "Success Imported CSV: av_snr_log.csv" },
                     vm.Log.Entries.Skip(before).Select(e => e.Level + " " + e.Text));
        Assert.Equal(90, vm.Plot.Series.Count);
        Assert.All(vm.Plot.Series, s => Assert.True(s.IsPhasedArray && s.Metric == MetricType.Snr));
        // Only each stream's combined polarization shows at first (the 30 of the 90): Customize View shows the rest.
        Assert.Equal(30, vm.Plot.Series.Count(s => s.Visible));
        Assert.All(vm.Plot.Series.Where(s => s.Visible), s => Assert.Equal(PlotColors.CombinedChannelIndex, s.ChannelIndex));
        Assert.Equal("av_snr_log", vm.Plot.Title);
        Assert.Equal(("Imported av_snr_log.csv.", LogLevel.Success), (vm.Status, vm.StatusLevel));
        Assert.Contains(Sample, vm.RecentFiles);
        // 2026-11-13 is day 317, and the first sample is at 19:15:35.513, so the axis reads as the clock does for the application's own time column.
        Assert.Equal("317:19:15:35", vm.Plot.FormatTime(0.0));
        Assert.Equal("317:19:16:35", vm.Plot.FormatTime(60.0));
    }

    [Fact]
    public async Task EveryWayOfImportingAsksWhatTheFileIsAndCancelImportsNothing()
    {
        var kinds = new FakeCsvKinds();
        kinds.Answers.Enqueue(null);                            // the menu, cancelled
        kinds.Answers.Enqueue(null);                            // a drop or Recent Files, cancelled
        kinds.Answers.Enqueue(CsvKind.PhasedArrayAntenna);      // Recent Files again, answered
        MainWindowViewModel vm = Window(kinds);
        int before = vm.Log.Entries.Count;

        await vm.ImportCsvAsync(Sample);
        await vm.OpenPathAsync(Sample);
        Assert.Equal(before, vm.Log.Entries.Count);   // nothing was said, nothing was cleared, nothing was read
        Assert.False(vm.Plot.HasData);
        Assert.False(vm.IsBusy);

        await vm.OpenPathAsync(Sample);
        Assert.Equal(3, kinds.Asked.Count);           // asked each time: the kind is not remembered with the file
        Assert.Equal(90, vm.Plot.Series.Count);
    }

    [Fact]
    public async Task AFileOfTheOtherKindIsAnErrorThatNamesTheKindChosen()
    {
        var kinds = new FakeCsvKinds();
        MainWindowViewModel vm = Window(kinds);

        kinds.Answers.Enqueue(CsvKind.TelemetryReceivers);
        await vm.ImportCsvAsync(Sample);   // the antenna's log, said to be this application's own export
        Assert.StartsWith("ERROR: 'av_snr_log.csv' is not a recognized exported CSV", vm.Status, StringComparison.Ordinal);
        Assert.False(vm.Plot.HasData);

        string ours = TempFiles.NewPath("ours", ".csv");
        File.WriteAllText(ours, "Time (DOY:HH:MM:SS.mmm),A Lock (%)\r\n120:00:00:00.000,5\r\n");
        try
        {
            kinds.Answers.Enqueue(CsvKind.PhasedArrayAntenna);
            await vm.ImportCsvAsync(ours);   // this application's export, said to be the antenna's
            Assert.Equal($"ERROR: '{Path.GetFileName(ours)}' is not a recognized AV Phased Array Antenna log (expected the header 'Time, Name, Polarity, Stream 1, ...' with data rows). Nothing was imported.", vm.Status);
            Assert.Equal(LogLevel.Error, vm.StatusLevel);
            Assert.False(vm.Plot.HasData);
        }
        finally
        {
            TempFiles.Delete(ours);
        }
    }

    [Fact]
    public async Task WithNoDialogACsvIsReadAsTheApplicationsOwn()
    {
        var vm = new MainWindowViewModel();
        await vm.ImportCsvAsync(Sample);   // the antenna's log, with no one to say what it is
        Assert.StartsWith("ERROR: ", vm.Status, StringComparison.Ordinal);
        Assert.False(vm.Plot.HasData);
    }

    [Fact]
    public async Task CustomizeViewReadsTheAntennasStreamsWithHVAndCAndNotAsReceivers()
    {
        var kinds = new FakeCsvKinds();
        kinds.Answers.Enqueue(CsvKind.PhasedArrayAntenna);
        MainWindowViewModel vm = Window(kinds);
        await vm.ImportCsvAsync(Sample);

        var customize = new CustomizeViewModel(vm.Plot, new NoPlotPrompts());
        Assert.Empty(customize.LockRows);
        SnrStreamGroup unit = Assert.Single(customize.SnrStreams);
        Assert.Equal("SCU1", unit.Label);
        ReceiverGroup[] streams = unit.Receivers.OrderBy(r => r.Index).ToArray();
        Assert.Equal(30, streams.Length);
        Assert.Equal("Stream 1", streams[0].Label);
        Assert.Equal("Stream 30", streams[^1].Label);
        Assert.Equal(new[] { "H", "V", "C" }, streams[0].Channels.Select(c => c.Label));
        Assert.Equal(new[] { false, false, true }, streams[0].Channels.Select(c => c.IsChecked));   // C alone is shown at first
        Assert.Null(streams[0].IsChecked);                                                           // so its box is partly set

        // The legend names the series for their stream and polarization, not as a receiver's CH, RCVR and L, R, C.
        Assert.Equal("Stream 1 H", PlotViewModel.LegendLabel(vm.Plot.Series[0]));
        Assert.Equal("Stream 30 C", PlotViewModel.LegendLabel(vm.Plot.Series[^1]));
        Assert.Equal(new[] { "H", "V", "C", "4" }, Enumerable.Range(0, 4).Select(PlotViewModel.PolarizationLabel));
    }

    [Fact]
    public async Task NothingOfAnAntennaIsEverExportedNotEvenBesideASeriesThatIs()
    {
        var kinds = new FakeCsvKinds();
        kinds.Answers.Enqueue(CsvKind.PhasedArrayAntenna);
        MainWindowViewModel vm = Window(kinds);
        await vm.ImportCsvAsync(Sample);
        string only = TempFiles.NewPath("array-only", ".csv");
        string mixed = TempFiles.NewPath("array-mixed", ".csv");
        try
        {
            Assert.False(vm.Plot.ExportCsv(only));   // nothing to write: no file
            Assert.False(File.Exists(only));

            // Beside a series of a telemetry receiver, which US6.1 still exports in full.
            List<PlotSeries> series = vm.Plot.Series.Take(3).ToList();
            series.Add(PlotFixtures.Lock(1000, "CH-01 PRN", 1, 0, new[] { 0.0, 1.0 }, new[] { 99.0, 98.0 }));
            vm.Plot.SetSeries(series);
            Assert.True(vm.Plot.ExportCsv(mixed));
            CsvData written = CsvFile.Parse(mixed);
            Assert.Equal(new[] { "CH-01 PRN" }, written.Series.Select(s => s.Name));
            Assert.DoesNotContain("Stream", File.ReadAllText(mixed), StringComparison.Ordinal);
        }
        finally
        {
            TempFiles.Delete(only);
            TempFiles.Delete(mixed);
        }
    }

    [Fact]
    public async Task ExportDataCannotBeTickedWhileTheAntennasLogIsAllThePlotHolds()
    {
        var kinds = new FakeCsvKinds();
        kinds.Answers.Enqueue(CsvKind.PhasedArrayAntenna);
        var dialog = new FakeExportDialog();
        var vm = new MainWindowViewModel(new MainWindowServices { CsvKinds = kinds, ExportDialog = dialog });
        await vm.ImportCsvAsync(Sample);

        Assert.False(vm.Plot.HasExportableData);
        Assert.True(vm.ExportCommand.CanExecute(null));   // the plot image and the log still export
        await vm.ExportCommand.ExecuteAsync(null);
        Assert.False(dialog.Shown!.DataAvailable);

        // Beside a series of a telemetry receiver there is data to write again.
        List<PlotSeries> series = vm.Plot.Series.Take(3).ToList();
        series.Add(PlotFixtures.Lock(1000, "CH-01 PRN", 1, 0, new[] { 0.0, 1.0 }, new[] { 99.0, 98.0 }));
        vm.Plot.SetSeries(series);
        Assert.True(vm.Plot.HasExportableData);
        await vm.ExportCommand.ExecuteAsync(null);
        Assert.True(dialog.Shown.DataAvailable);
    }

    // ---- an antenna's log joins what the plot shows, placed by the clock (US10.0) -------------------------------------

    private static readonly DateTime Noon = new(2026, 11, 13, 19, 0, 0, DateTimeKind.Utc);

    /// <summary>A log of two streams, one sample a second from <paramref name="start"/>, written to a temp file.</summary>
    private static string WriteLog(string stem, DateTime start, int samples)
    {
        string path = TempFiles.NewPath(stem, ".csv");
        var text = new StringBuilder("Time, Name, Polarity, Stream 1, Stream 2\r\n");
        for (int i = 0; i < samples; i++)
        {
            string time = start.AddSeconds(i).ToString("yyyy-MM-dd'T'HH:mm:ss.fff'Z'", CultureInfo.InvariantCulture);
            foreach (string polarity in new[] { "HPol", "VPol", "Comb" })
                text.Append(CultureInfo.InvariantCulture, $"{time}, SCU1, {polarity}, {i}.0, {i + 1}.0\r\n");
        }
        File.WriteAllText(path, text.ToString());
        return path;
    }

    private static string WriteOwnExport()
    {
        string path = TempFiles.NewPath("ours-export", ".csv");
        File.WriteAllText(path, "Time (DOY:HH:MM:SS.mmm),A Lock (%)\r\n120:00:00:00.000,5\r\n");
        return path;
    }

    /// <summary>A window that answers every kind question with <paramref name="kind"/>, and what its plot warned of.</summary>
    private static (MainWindowViewModel Vm, List<string> Warnings) WindowAnswering(CsvKind kind)
    {
        var kinds = new FakeCsvKinds();
        for (int i = 0; i < 4; i++) kinds.Answers.Enqueue(kind);
        MainWindowViewModel vm = Window(kinds);
        var warnings = new List<string>();
        vm.Plot.Warning += (_, message) => warnings.Add(message);
        return (vm, warnings);
    }

    [Fact]
    public async Task ALogImportedAfterAnotherIsAddedBesideItAndPlacedByTheClock()
    {
        string first = WriteLog("first", Noon, 20);                    // 19:00:00 to 19:00:19
        string second = WriteLog("second", Noon.AddSeconds(10), 20);   // 19:00:10 to 19:00:29
        try
        {
            (MainWindowViewModel vm, List<string> warnings) = WindowAnswering(CsvKind.PhasedArrayAntenna);
            await vm.ImportCsvAsync(first);
            await vm.ImportCsvAsync(second);

            Assert.Equal(12, vm.Plot.Series.Count);                    // two streams of three, twice: nothing was cleared
            Assert.Empty(warnings);                                     // they overlap
            Assert.Equal(Path.GetFileNameWithoutExtension(first), vm.Plot.Title);   // the title stays the first's
            PlotSeries[] ofFirst = vm.Plot.Series.Take(6).ToArray(), ofSecond = vm.Plot.Series.Skip(6).ToArray();
            Assert.Equal(0.0, ofFirst[0].X[0], 6);
            Assert.Equal(10.0, ofSecond[0].X[0], 6);                    // ten seconds later on the clock
            Assert.Equal(29.0, ofSecond[0].X[^1], 6);
            Assert.Equal("317:19:00:10", vm.Plot.FormatTime(10.0));     // the axis still starts at the first's clock
            Assert.All(ofSecond, s => Assert.Equal(ofSecond[0].SourceId, s.SourceId));
            Assert.NotEqual(ofFirst[0].SourceId, ofSecond[0].SourceId);
            // Two recordings for the Plot File selector, the second named for its file.
            Assert.Equal(2, vm.Plot.SourceList().Count);
            Assert.Equal(Path.GetFileNameWithoutExtension(second), vm.Plot.SourceList()[1].Label);
            Assert.Equal(29.0, vm.Plot.XViewMax, 6);                    // the view takes in both
        }
        finally
        {
            TempFiles.Delete(first);
            TempFiles.Delete(second);
        }
    }

    [Fact]
    public async Task ALogThatStartsEarlierMovesWhatIsShownLaterAndTheClockWithIt()
    {
        string first = WriteLog("late", Noon, 20);
        string second = WriteLog("early", Noon.AddSeconds(-5), 20);    // 18:59:55 to 19:00:14
        try
        {
            (MainWindowViewModel vm, List<string> warnings) = WindowAnswering(CsvKind.PhasedArrayAntenna);
            await vm.ImportCsvAsync(first);
            int[] versions = vm.Plot.Series.Select(s => s.Version).ToArray();
            await vm.ImportCsvAsync(second);

            Assert.Empty(warnings);
            PlotSeries[] ofFirst = vm.Plot.Series.Take(6).ToArray(), ofSecond = vm.Plot.Series.Skip(6).ToArray();
            Assert.All(ofFirst.Zip(versions), p => Assert.True(p.First.Version > p.Second));   // their samples moved, so the chart redraws them
            Assert.Equal(5.0, ofFirst[0].X[0], 6);                      // moved right by the five seconds the other starts before it
            Assert.Equal(0.0, ofSecond[0].X[0], 6);
            Assert.Equal("317:18:59:55", vm.Plot.FormatTime(0.0));      // elapsed zero is the earliest sample on the plot
            Assert.Equal("317:19:00:00", vm.Plot.FormatTime(5.0));
        }
        finally
        {
            TempFiles.Delete(first);
            TempFiles.Delete(second);
        }
    }

    [Fact]
    public async Task ALogThatDoesNotOverlapWhatIsShownIsSaidSoAndStillAdded()
    {
        string first = WriteLog("today", Noon, 10);
        string second = WriteLog("tomorrow", Noon.AddDays(1), 10);
        try
        {
            (MainWindowViewModel vm, List<string> warnings) = WindowAnswering(CsvKind.PhasedArrayAntenna);
            await vm.ImportCsvAsync(first);
            await vm.ImportCsvAsync(second);

            Assert.Equal(12, vm.Plot.Series.Count);
            Assert.Equal("The log's recording time does not overlap the data already shown. " +
                         "They will share one time axis with a large gap between them.", Assert.Single(warnings));
            Assert.Equal(86400.0, vm.Plot.Series[6].X[0], 6);
        }
        finally
        {
            TempFiles.Delete(first);
            TempFiles.Delete(second);
        }
    }

    [AvaloniaFact]
    public async Task ALogJoinsAProcessedRecordingAndEveryPointKeepsItsOwnClockTime()
    {
        var kinds = new FakeCsvKinds();
        kinds.Answers.Enqueue(CsvKind.PhasedArrayAntenna);
        MainWindowViewModel vm = Window(kinds);
        await vm.LoadAsync(Repo.Recording("prn_testfile, sample.ch10"));
        PlotSeries lockSeries = Assert.Single(PlotFixtures.Locks(vm));
        int processed = vm.Plot.Series.Count;                            // its lock percentage and missed frames
        string lockStart = vm.Plot.FormatTime(lockSeries.X[0]), lockEnd = vm.Plot.FormatTime(lockSeries.X[^1]);

        await vm.ImportCsvAsync(Sample);

        Assert.Equal(processed + 90, vm.Plot.Series.Count);              // the recording's series and the log's 90
        Assert.Same(lockSeries, vm.Plot.Series.Single(s => s.Metric == MetricType.FrameSyncLock));
        PlotSeries antenna = vm.Plot.Series[processed];
        Assert.True(antenna.IsPhasedArray);
        // Wherever the two start, each sample reads the clock time it was taken at, on the one axis: the recording's as it did
        // before the log came, and the log's as the file wrote it (day 317, 19:15:35).
        Assert.Equal(lockStart, vm.Plot.FormatTime(lockSeries.X[0]));
        Assert.Equal(lockEnd, vm.Plot.FormatTime(lockSeries.X[^1]));
        Assert.Equal("317:19:15:35", vm.Plot.FormatTime(antenna.X[0]));
        Assert.Equal("317:19:56:53", vm.Plot.FormatTime(antenna.X[^1]));
        Assert.Equal(new[] { "prn_testfile, sample", "av_snr_log" }, vm.Plot.SourceList().Select(x => Path.GetFileNameWithoutExtension(x.Label)));
    }

    [Fact]
    public async Task ALogThatEndsBeforeWhatIsShownStartsIsSaidSoToo()
    {
        string first = WriteLog("now", Noon, 10);
        string second = WriteLog("yesterday", Noon.AddDays(-1), 10);
        try
        {
            (MainWindowViewModel vm, List<string> warnings) = WindowAnswering(CsvKind.PhasedArrayAntenna);
            await vm.ImportCsvAsync(first);
            await vm.ImportCsvAsync(second);

            Assert.Single(warnings);
            Assert.Equal(86400.0, vm.Plot.Series[0].X[0], 6);           // what was shown is now a day in
            Assert.Equal(0.0, vm.Plot.Series[6].X[0], 6);
        }
        finally
        {
            TempFiles.Delete(first);
            TempFiles.Delete(second);
        }
    }

    [Fact]
    public async Task AFileThatIsNotALogLeavesWhatIsShownAlone()
    {
        string first = WriteLog("kept", Noon, 10);
        string notALog = WriteOwnExport();
        try
        {
            (MainWindowViewModel vm, _) = WindowAnswering(CsvKind.PhasedArrayAntenna);
            await vm.ImportCsvAsync(first);
            await vm.ImportCsvAsync(notALog);

            Assert.StartsWith("ERROR: ", vm.Status, StringComparison.Ordinal);
            Assert.Equal(6, vm.Plot.Series.Count);                      // the plot is as it was, not cleared first
            Assert.Equal(Path.GetFileNameWithoutExtension(first), vm.Plot.Title);
        }
        finally
        {
            TempFiles.Delete(first);
            TempFiles.Delete(notALog);
        }
    }

    [Fact]
    public async Task ACsvOfTheApplicationsOwnStillReplacesWhatIsShown()
    {
        string first = WriteLog("replaced", Noon, 10);
        string ours = WriteOwnExport();
        try
        {
            var kinds = new FakeCsvKinds();
            kinds.Answers.Enqueue(CsvKind.PhasedArrayAntenna);
            kinds.Answers.Enqueue(CsvKind.TelemetryReceivers);
            MainWindowViewModel vm = Window(kinds);
            await vm.ImportCsvAsync(first);
            Assert.Equal(6, vm.Plot.Series.Count);
            await vm.ImportCsvAsync(ours);

            PlotSeries only = Assert.Single(vm.Plot.Series);   // it replaced the log
            Assert.False(only.IsPhasedArray);
            Assert.Equal(MetricType.FrameSyncLock, only.Metric);
        }
        finally
        {
            TempFiles.Delete(first);
            TempFiles.Delete(ours);
        }
    }
}
