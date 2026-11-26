using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Headless.XUnit;
using TmDq.App.Models.Processing;
using TmDq.App.ViewModels;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// Processing a recording leaves what was imported from CSV files on the plot, so that the two can be compared: an antenna's
/// log (US10.0) or this application's own export is kept, the recordings processed before are replaced as they always were,
/// and the new run is numbered so that it never shares a source id with an import.
/// </summary>
[Collection(NativeCoreCollection.Name)]
public sealed class ImportedDataKeptTests : IClassFixture<ShortRecordings>
{
    private readonly ShortRecordings _recordings;

    public ImportedDataKeptTests(ShortRecordings recordings)
    {
        NativeCore.EnsureLoaded();
        _recordings = recordings;
    }

    // A short recording: processing it costs a fraction of the whole sample, and these tests need a recording, not its numbers.
    private string Prn => _recordings.First;

    private static string AntennaLog => Repo.Recording("av_snr_log.csv");

    private string PrnName => MainWindowViewModel.BaseName(Path.GetFileName(Prn));

    [AvaloniaFact]
    public async Task AnAntennasLogImportedBeforeARecordingIsProcessedStaysOnThePlot()
    {
        MainWindowViewModel vm = new MainWindowViewModel();
        await vm.ImportCsvAsync(AntennaLog);
        Assert.Equal(90, vm.Plot.Series.Count);

        await vm.LoadAsync(Prn);

        Assert.Equal(90, vm.Plot.Series.Count(s => s.IsPhasedArray));   // the log is all still there
        Assert.Contains(vm.Plot.Series, s => s.Metric == MetricType.FrameSyncLock);   // and so is the run
        Assert.Equal("av_snr_log", vm.Plot.Title);                     // the log named the plot first, and an Open adds beside it
        Assert.Equal(new[] { "av_snr_log", PrnName }, vm.Plot.SourceList().Select(x => x.Label));
        Assert.Single(vm.Sources);                                      // Save Template is offered the recording, not the log
        int runId = vm.Sources[0].SourceId;
        Assert.DoesNotContain(vm.Plot.Series.Where(s => s.IsPhasedArray), s => s.SourceId == runId);   // the run has an id of its own
    }

    [AvaloniaFact]
    public async Task AnotherRecordingIsAddedBesideTheLastOneAndTheImportStays()
    {
        MainWindowViewModel vm = new MainWindowViewModel();
        await vm.ImportCsvAsync(AntennaLog);
        await vm.LoadAsync(Prn);
        int once = vm.Plot.Series.Count;
        int run = once - 90;

        await vm.LoadAsync(Prn);

        Assert.Equal(90 + 2 * run, vm.Plot.Series.Count);               // the recording again, beside the first: nothing replaced
        Assert.Equal(90, vm.Plot.Series.Count(s => s.IsPhasedArray));
        Assert.Equal(2, vm.Sources.Count);
        Assert.Equal(3, vm.Plot.SourceList().Count);
    }

    [AvaloniaFact]
    public async Task AnExportImportedBeforeARecordingIsProcessedKeepsItsSeriesAndNeverSharesTheRunsSourceId()
    {
        string csv = TempFiles.NewPath("kept-export", ".csv");
        try
        {
            // An export of the same recording, to import as a data set of its own.
            var maker = new MainWindowViewModel();
            await maker.LoadAsync(Prn);
            Assert.True(maker.Plot.ExportCsv(csv));
            int exported = maker.Plot.Series.Count;

            MainWindowViewModel vm = new MainWindowViewModel();
            await vm.ImportCsvAsync(csv);
            int[] importedIds = vm.Plot.Series.Select(s => s.SourceId).Distinct().ToArray();
            Assert.Equal(exported, vm.Plot.Series.Count);

            await vm.LoadAsync(Prn);

            Assert.Equal(2 * exported, vm.Plot.Series.Count);          // the import's and the run's, side by side
            int runId = vm.Sources[0].SourceId;
            Assert.DoesNotContain(runId, importedIds);
            Assert.Equal(exported, vm.Plot.Series.Count(s => s.SourceId == runId));
            Assert.Equal(importedIds.Length + 1, vm.Plot.SourceList().Count);
        }
        finally
        {
            TempFiles.Delete(csv);
        }
    }

    [AvaloniaFact]
    public async Task AnImportedFileIsNamedForItInPlotFileAndRemoveFile()
    {
        string csv = TempFiles.NewPath("named-export", ".csv");
        try
        {
            var maker = new MainWindowViewModel();
            await maker.LoadAsync(Prn);
            Assert.True(maker.Plot.ExportCsv(csv));

            MainWindowViewModel vm = new MainWindowViewModel();
            await vm.ImportCsvAsync(csv);

            Assert.Equal(MainWindowViewModel.BaseName(Path.GetFileName(csv)), Assert.Single(vm.Plot.SourceList()).Label);
        }
        finally
        {
            TempFiles.Delete(csv);
        }
    }

    [AvaloniaFact]
    public async Task AnImportThatReplacesWhatIsShownLeavesNoNameOfTheRecordingBehind()
    {
        string csv = TempFiles.NewPath("no-stale-name", ".csv");
        try
        {
            var vm = new MainWindowViewModel();
            await vm.LoadAsync(Prn);
            Assert.True(vm.Plot.ExportCsv(csv));
            Assert.Equal(PrnName, Assert.Single(vm.Plot.SourceList()).Label);

            Assert.True(vm.Plot.LoadCsv(csv));   // straight into the plot, which replaces what it held, with no name given

            Assert.Equal("Source 0", Assert.Single(vm.Plot.SourceList()).Label);
        }
        finally
        {
            TempFiles.Delete(csv);
        }
    }

    [AvaloniaFact]
    public async Task ARecordingThatFailsToOpenChangesNothing()
    {
        string junk = TempFiles.NewPath("junk", ".ch10");
        File.WriteAllText(junk, "not a recording");
        try
        {
            MainWindowViewModel vm = new MainWindowViewModel();
            await vm.ImportCsvAsync(AntennaLog);
            await vm.LoadAsync(Prn);
            Assert.Single(vm.Sources);
            int series = vm.Plot.Series.Count;

            await vm.LoadAsync(junk);

            Assert.Single(vm.Sources);                                  // the run and the log are both still there
            Assert.Equal(series, vm.Plot.Series.Count);
            Assert.Equal(90, vm.Plot.Series.Count(s => s.IsPhasedArray));
        }
        finally
        {
            TempFiles.Delete(junk);
        }
    }

    [AvaloniaFact]
    public async Task TheSameRecordingOpenedAgainIsStackedAndTheTitleStays()
    {
        var vm = new MainWindowViewModel();
        await vm.LoadAsync(Prn);
        vm.Plot.Title = "my title";

        await vm.LoadAsync(Prn);

        Assert.Equal("my title", vm.Plot.Title);                        // the title the user gave is not replaced
        Assert.Equal(2, vm.Sources.Count);
        Assert.Equal(2, vm.Plot.SourceList().Count);
        Assert.Equal(new[] { 0, 1 }, vm.Sources.Select(s => s.SourceId));   // numbered after what the plot holds
    }

    [AvaloniaFact]
    public async Task AnImportedCsvJoinsWhatTheRunShowedAndTheRunStays()
    {
        string csv = TempFiles.NewPath("joins", ".csv");
        try
        {
            var vm = new MainWindowViewModel();
            await vm.LoadAsync(Prn);
            int run = vm.Plot.Series.Count;
            Assert.True(vm.Plot.ExportCsv(csv));

            await vm.ImportCsvAsync(csv);

            Assert.NotEmpty(vm.Sources);                                // the run stays: imports stack, they do not replace
            Assert.True(vm.Plot.Series.Count > run);
            Assert.Equal(PrnName, vm.Plot.Title);
            // The imported series are a recording of their own, named for the file and not the run's number.
            int[] ids = vm.Plot.Series.Select(s => s.SourceId).Distinct().Order().ToArray();
            Assert.Equal(2, ids.Length);
            Assert.Equal(Path.GetFileNameWithoutExtension(csv), vm.Plot.SourceList().Single(s => s.Id == ids[1]).Label);
        }
        finally
        {
            TempFiles.Delete(csv);
        }
    }
}
