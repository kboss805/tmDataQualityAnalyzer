using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using TmDq.App.Models.Export;
using TmDq.App.Models.Logging;
using TmDq.App.Models.Processing;
using TmDq.App.Services;
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

    private sealed class FakeKinds : ICsvKindDialog
    {
        public readonly Queue<CsvKind?> Answers = new();
        public readonly List<string> Asked = new();

        public Task<CsvKind?> ChooseAsync(string fileName)
        {
            Asked.Add(fileName);
            return Task.FromResult(Answers.Count > 0 ? Answers.Dequeue() : null);
        }
    }

    private static MainWindowViewModel Window(FakeKinds kinds) => new(new MainWindowServices { CsvKinds = kinds });

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
        var kinds = new FakeKinds();
        kinds.Answers.Enqueue(CsvKind.PhasedArrayAntenna);
        MainWindowViewModel vm = Window(kinds);
        int before = vm.Log.Entries.Count;

        await vm.ImportCsvAsync(Sample);

        Assert.Equal(new[] { "av_snr_log.csv" }, kinds.Asked);
        Assert.Equal(new[] { "Info Importing CSV: av_snr_log.csv", "Success Imported CSV: av_snr_log.csv" },
                     vm.Log.Entries.Skip(before).Select(e => e.Level + " " + e.Text));
        Assert.Equal(90, vm.Plot.Series.Count);
        Assert.All(vm.Plot.Series, s => Assert.True(s.IsPhasedArray && s.Visible && s.Metric == MetricType.Snr));
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
        var kinds = new FakeKinds();
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
        var kinds = new FakeKinds();
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
        var kinds = new FakeKinds();
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

        // The legend names the series for their stream and polarization, not as a receiver's CH, RCVR and L, R, C.
        Assert.Equal("Stream 1 H", PlotViewModel.LegendLabel(vm.Plot.Series[0]));
        Assert.Equal("Stream 30 C", PlotViewModel.LegendLabel(vm.Plot.Series[^1]));
        Assert.Equal(new[] { "H", "V", "C", "4" }, Enumerable.Range(0, 4).Select(PlotViewModel.PolarizationLabel));
    }

    [Fact]
    public async Task NothingOfAnAntennaIsEverExportedNotEvenBesideASeriesThatIs()
    {
        var kinds = new FakeKinds();
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
}
