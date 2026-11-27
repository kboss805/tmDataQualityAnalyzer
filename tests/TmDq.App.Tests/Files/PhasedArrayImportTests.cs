using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Avalonia.Headless.XUnit;
using TmDq.App.Models.Logging;
using TmDq.App.Models.Processing;
using TmDq.App.ViewModels;
using TmDq.App.ViewModels.Plot;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// Importing a phased array antenna's SNR log (US10.0): told from this application's own export by the file, with no question; the log's
/// 30 streams shown as 90 series that read H, V and C and not as receivers; and nothing of them ever exported. The format is
/// the core's (<c>test_phasedarraylog.cpp</c>, <c>PhasedArrayLogTests</c> in the library's tests); these hold what the application does with it.
/// </summary>
[Collection(NativeCoreCollection.Name)]
public sealed class PhasedArrayImportTests
{
    public PhasedArrayImportTests() => NativeCore.EnsureLoaded();

    private static string Sample => Repo.Recording("av_snr_log.csv");

    [Fact]
    public void EachLayoutIsReadAsWhatItIsAndAFileOfNeitherIsNothing()
    {
        // No one says which: each reader refuses the other's file at its first line, so the file decides.
        var log = new PlotViewModel();
        Assert.True(log.LoadCsv(Sample));
        Assert.All(log.Series, s => Assert.True(s.IsPhasedArray));
        string ours = WriteOwnExport();
        string neither = WriteNeither();
        try
        {
            var export = new PlotViewModel();
            Assert.True(export.LoadCsv(ours));
            PlotSeries only = Assert.Single(export.Series);
            Assert.False(only.IsPhasedArray);
            Assert.Equal(MetricType.FrameSyncLock, only.Metric);
            var none = new PlotViewModel();
            Assert.False(none.LoadCsv(neither));
            Assert.Empty(none.Series);
        }
        finally
        {
            TempFiles.Delete(ours);
            TempFiles.Delete(neither);
        }
    }

    [Fact]
    public async Task TheSampleImportsAsNinetySeriesTitledByItsNameAndIsRemembered()
    {
        var vm = new MainWindowViewModel();
        int before = vm.Log.Entries.Count;

        await vm.ImportCsvAsync(Sample);

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
    public async Task EveryWayOfImportingReadsTheLogWithNoQuestion()
    {
        var vm = new MainWindowViewModel();

        await vm.ImportCsvAsync(Sample);   // the menu
        Assert.Equal(90, vm.Plot.Series.Count);
        await vm.OpenPathAsync(Sample);    // a drop or Recent Files: the same log again, added beside the first
        Assert.Equal(180, vm.Plot.Series.Count);
        Assert.All(vm.Plot.Series, s => Assert.True(s.IsPhasedArray));
        Assert.False(vm.IsBusy);
    }

    [Fact]
    public async Task AFileOfNeitherLayoutIsOneErrorThatNamesBoth()
    {
        var vm = new MainWindowViewModel();
        string neither = WriteNeither();
        try
        {
            await vm.ImportCsvAsync(neither);
            Assert.Equal($"ERROR: '{Path.GetFileName(neither)}' is not a recognized CSV: neither an export of this application (a 'Time (DOY:HH:MM:SS.mmm)' column) " +
                         "nor an AV Phased Array Antenna log (the header 'Time, Name, Polarity, Stream 1, ...'), with data rows. Nothing was imported.", vm.Status);
            Assert.Equal(LogLevel.Error, vm.StatusLevel);
            Assert.False(vm.Plot.HasData);
            Assert.DoesNotContain(neither, vm.RecentFiles);
        }
        finally
        {
            TempFiles.Delete(neither);
        }
    }

    [Fact]
    public async Task AFileThatIsNotThereOrIsHeldElsewhereIsSaidToBeUnreadableNotOfTheWrongLayout()
    {
        // A stale Recent Files entry, or a file another program holds, was reported as "not a recognized CSV".
        var vm = new MainWindowViewModel();
        string gone = TempFiles.NewPath("gone", ".csv");
        await vm.ImportCsvAsync(gone);
        Assert.Equal($"ERROR: '{Path.GetFileName(gone)}' could not be read: the file is not there. Nothing was imported.", vm.Status);
        Assert.Equal(LogLevel.Error, vm.StatusLevel);
        Assert.False(vm.Plot.HasData);

        string held = WriteNeither();
        try
        {
            using var lockIt = new FileStream(held, FileMode.Open, FileAccess.ReadWrite, FileShare.None);
            await vm.ImportCsvAsync(held);
            Assert.StartsWith($"ERROR: '{Path.GetFileName(held)}' could not be read: ", vm.Status);
            Assert.DoesNotContain("not a recognized", vm.Status);
            Assert.EndsWith(" Nothing was imported.", vm.Status);
            Assert.False(vm.Plot.HasData);
        }
        finally
        {
            TempFiles.Delete(held);
        }
    }

    [Fact]
    public async Task CustomizeViewReadsTheAntennasStreamsWithHVAndCAndNotAsReceivers()
    {
        var vm = new MainWindowViewModel();
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
        var vm = new MainWindowViewModel();
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
        var dialog = new FakeExportDialog();
        var vm = new MainWindowViewModel(new MainWindowServices { ExportDialog = dialog });
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

    /// <summary>A file that is neither layout: a CSV, but no one's this application reads.</summary>
    private static string WriteNeither()
    {
        string path = TempFiles.NewPath("neither", ".csv");
        File.WriteAllText(path, "Date, Value\r\n2026-11-13, 5\r\n");
        return path;
    }

    /// <summary>A window, and what its plot warned of.</summary>
    private static (MainWindowViewModel Vm, List<string> Warnings) NewWindow()
    {
        var vm = new MainWindowViewModel();
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
            (MainWindowViewModel vm, List<string> warnings) = NewWindow();
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
            (MainWindowViewModel vm, List<string> warnings) = NewWindow();
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
    public async Task RemovingTheLogThatStartedFirstStartsTheAxisAtTheOtherOnItsOwnClock()
    {
        string first = WriteLog("late", Noon, 20);                     // 19:00:00 to 19:00:19
        string second = WriteLog("early", Noon.AddSeconds(-5), 20);    // 18:59:55 to 19:00:14
        try
        {
            (MainWindowViewModel vm, _) = NewWindow();
            await vm.ImportCsvAsync(first);
            await vm.ImportCsvAsync(second);
            PlotSeries late = vm.Plot.Series[0];
            Assert.Equal(5.0, late.X[0], 6);

            Assert.True(vm.Plot.RemoveSource(vm.Plot.Series[^1].SourceId));

            // Zero is the first sample left, and the clock is the files' own: a clock worked out from the absolute base,
            // which an imported file's clock never set, read day 1 of 1970.
            Assert.Equal(0.0, late.X[0], 9);
            Assert.Equal("317:19:00:00", vm.Plot.FormatTime(0.0));
            Assert.Equal("317:19:00:19", vm.Plot.FormatTime(late.X[^1]));
            Assert.Equal((0.0, 19.0), (vm.Plot.XViewMin, Math.Round(vm.Plot.XViewMax, 6)));
        }
        finally
        {
            TempFiles.Delete(first);
            TempFiles.Delete(second);
        }
    }

    [Fact]
    public async Task ALogJoinedToWhatIsShownLeavesItsColours()
    {
        string first = WriteLog("first", Noon, 5), second = WriteLog("second", Noon.AddSeconds(2), 5);
        try
        {
            (MainWindowViewModel vm, _) = NewWindow();
            await vm.ImportCsvAsync(first);
            PlotSeries shown = vm.Plot.Series[0];
            vm.Plot.RecolorSeries(shown.Id, Avalonia.Media.Colors.Teal);

            await vm.ImportCsvAsync(second);

            Assert.Equal(Avalonia.Media.Colors.Teal, shown.Color);
            Assert.Equal(vm.Plot.Series[1].Color, vm.Plot.Series[7].Color);   // the joined log's own, as the first's were given
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
            (MainWindowViewModel vm, List<string> warnings) = NewWindow();
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
        var vm = new MainWindowViewModel();
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
            (MainWindowViewModel vm, List<string> warnings) = NewWindow();
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
    public async Task AFileOfNeitherLayoutLeavesWhatIsShownAlone()
    {
        string first = WriteLog("kept", Noon, 10);
        string neither = WriteNeither();
        try
        {
            (MainWindowViewModel vm, _) = NewWindow();
            await vm.ImportCsvAsync(first);
            await vm.ImportCsvAsync(neither);

            Assert.StartsWith("ERROR: ", vm.Status, StringComparison.Ordinal);
            Assert.Equal(6, vm.Plot.Series.Count);                      // the plot is as it was, not cleared first
            Assert.Equal(Path.GetFileNameWithoutExtension(first), vm.Plot.Title);
        }
        finally
        {
            TempFiles.Delete(first);
            TempFiles.Delete(neither);
        }
    }

    [Fact]
    public async Task ACsvOfTheApplicationsOwnJoinsAnAntennasLogAndBothStay()
    {
        // The order the user found it in: the antenna's log first, then an export of this application's own.
        string first = WriteLog("log-first", Noon, 10);
        string ours = WriteOwnExport();
        try
        {
            var vm = new MainWindowViewModel();
            await vm.ImportCsvAsync(first);
            Assert.Equal(6, vm.Plot.Series.Count);
            string title = vm.Plot.Title;
            await vm.ImportCsvAsync(ours);

            Assert.Equal(7, vm.Plot.Series.Count);                                  // the log is still there, and the export with it
            Assert.Equal(6, vm.Plot.Series.Count(s => s.IsPhasedArray));
            PlotSeries added = Assert.Single(vm.Plot.Series, s => !s.IsPhasedArray);
            Assert.Equal(MetricType.FrameSyncLock, added.Metric);
            Assert.NotEqual(vm.Plot.Series.First(s => s.IsPhasedArray).SourceId, added.SourceId);   // a recording of its own
            Assert.Equal(title, vm.Plot.Title);                                     // the title stays, as it does for a log
            Assert.Equal(new[] { Path.GetFileNameWithoutExtension(first), Path.GetFileNameWithoutExtension(ours) },
                         vm.Plot.SourceList().Select(s => s.Label));
        }
        finally
        {
            TempFiles.Delete(first);
            TempFiles.Delete(ours);
        }
    }
}
