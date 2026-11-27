using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Headless.XUnit;
using TmDq.App.Models.Processing;
using TmDq.App.ViewModels;
using TmDq.App.ViewModels.Plot;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// Open stacks: a recording opened with something on the plot is processed and added beside it (the recordings processed before and
/// what was imported), each recording its own entry in Plot File and Remove File, and nothing on the plot is replaced. An empty plot is
/// where a recording starts. (File > Add Recording was this, before Open did it too.)
/// </summary>
public sealed class OpenStacksTests : IClassFixture<ShortRecordings>
{
    private readonly ShortRecordings _recordings;

    public OpenStacksTests(ShortRecordings recordings)
    {
        NativeCore.EnsureLoaded();
        _recordings = recordings;
    }

    // A short recording, and the same again under another name, so that processing one costs a fraction of the whole sample.
    private string Prn => _recordings.First;

    private string Second => _recordings.Second;

    private static string AntennaLog => Repo.Recording("av_snr_log.csv");

    private static string NameOf(string path) => MainWindowViewModel.BaseName(Path.GetFileName(path));

    [AvaloniaFact]
    public async Task OpeningASecondRecordingKeepsTheOneOnThePlotAndAddsTheNewBesideIt()
    {
        var vm = new MainWindowViewModel();
        await vm.LoadAsync(Prn);
        PlotSeries first = Assert.Single(PlotFixtures.Locks(vm));
        int onePlot = vm.Plot.Series.Count;
        int firstId = vm.Sources[0].SourceId;

        await vm.LoadAsync(Second);

        Assert.Same(first, Assert.Single(vm.Plot.Series, s => s.SourceId == firstId && s.Metric == MetricType.FrameSyncLock));   // untouched
        Assert.True(vm.Plot.Series.Count > onePlot);
        Assert.Equal(2, vm.Sources.Count);                              // Save Template is offered both
        Assert.Equal(new[] { NameOf(Prn), NameOf(Second) }, vm.Plot.SourceList().Select(x => x.Label));
        Assert.Equal(2, vm.Sources.Select(s => s.SourceId).Distinct().Count());
        Assert.Equal(NameOf(Prn), vm.Plot.Title);                       // the plot's title is the first's
        Assert.Equal(new[] { Prn, Second }, vm.Sources.Select(s => s.FilePath));
    }

    [AvaloniaFact]
    public async Task OpeningASecondRecordingLeavesTheColoursShownAndColoursTheNewOneAsItWouldBeAlone()
    {
        var alone = new MainWindowViewModel();
        await alone.LoadAsync(Second);
        var vm = new MainWindowViewModel();
        await vm.LoadAsync(Prn);
        PlotSeries first = Assert.Single(PlotFixtures.Locks(vm));
        vm.Plot.RecolorSeries(first.Id, Avalonia.Media.Colors.Teal);   // as Customize View or a template recolours it
        int firstId = first.SourceId;

        await vm.LoadAsync(Second);

        Assert.All(vm.Plot.Series.Where(s => s.SourceId == firstId && s.IsLeftAxis), s => Assert.Equal(Avalonia.Media.Colors.Teal, s.Color));
        Assert.Equal(alone.Plot.Series.Select(s => (s.Metric, s.Color, s.Dotted, s.Strong)),
                     vm.Plot.Series.Where(s => s.SourceId != firstId).Select(s => (s.Metric, s.Color, s.Dotted, s.Strong)));
    }

    [AvaloniaFact]
    public async Task OpeningWithNothingOnThePlotTitlesItAndNumbersItFromZero()
    {
        var vm = new MainWindowViewModel();

        await vm.LoadAsync(Prn);

        Assert.Equal(NameOf(Prn), vm.Plot.Title);
        Assert.Single(vm.Sources);
        Assert.Equal(NameOf(Prn), Assert.Single(vm.Plot.SourceList()).Label);
        Assert.Equal(0, vm.Sources[0].SourceId);
        Assert.Contains(vm.Log.Entries, e => e.Text.Contains("Opening: ", System.StringComparison.Ordinal));   // there was nothing to add to
    }

    [AvaloniaFact]
    public async Task OpenNoLongerReplacesAnythingAndAThirdOpenIsStackedToo()
    {
        var vm = new MainWindowViewModel();
        await vm.LoadAsync(Prn);
        await vm.LoadAsync(Second);
        Assert.Equal(2, vm.Sources.Count);
        int series = vm.Plot.Series.Count;

        await vm.LoadAsync(Prn);

        Assert.Equal(3, vm.Sources.Count);                              // what was there stays, and the third is beside it
        Assert.True(vm.Plot.Series.Count > series);
        Assert.Equal(NameOf(Prn), vm.Plot.Title);
        Assert.Equal(new[] { Prn, Second, Prn }, vm.Sources.Select(s => s.FilePath));
    }

    [AvaloniaFact]
    public async Task TheSameRecordingOpenedAgainIsNamedApartInPlotFileAndRemoveFile()
    {
        var vm = new MainWindowViewModel();
        await vm.LoadAsync(Prn);
        await vm.LoadAsync(Prn);
        await vm.LoadAsync(Prn);

        string name = NameOf(Prn);
        Assert.Equal(new[] { name, name + " (2)", name + " (3)" }, vm.Plot.SourceList().Select(x => x.Label));
        Assert.Equal(3, vm.Sources.Count);
        Assert.Equal(3, vm.Sources.Select(s => s.SourceId).Distinct().Count());

        // Removing the middle one with Remove File leaves the other two.
        vm.Plot.RemoveSource(vm.Sources[1].SourceId);
        Assert.Equal(new[] { name, name + " (3)" }, vm.Plot.SourceList().Select(x => x.Label));
    }

    [AvaloniaFact]
    public async Task OpeningKeepsWhatWasImportedAsWellAsWhatWasProcessed()
    {
        var vm = new MainWindowViewModel();
        await vm.ImportCsvAsync(AntennaLog);
        await vm.LoadAsync(Prn);
        await vm.LoadAsync(Second);

        Assert.Equal(90, vm.Plot.Series.Count(s => s.IsPhasedArray));
        Assert.Equal(3, vm.Plot.SourceList().Count);                    // the log and the two recordings
        Assert.Equal(2, vm.Sources.Count);
    }

    [AvaloniaFact]
    public async Task ARecordingThatFailsToOpenOrIsCancelledLeavesTheWholePlotAsItWas()
    {
        string junk = TempFiles.NewPath("add-junk", ".ch10");
        File.WriteAllText(junk, "not a recording");
        try
        {
            bool cancel = false;
            var configurator = FakeConfigurator.From((path, file) => cancel ? Task.FromResult<StreamsChoice?>(null) : new FirstChannelRun().ConfigureAsync(path, file));
            var vm = new MainWindowViewModel(new MainWindowServices { Configurator = configurator });
            await vm.LoadAsync(Prn);
            int series = vm.Plot.Series.Count;

            await vm.LoadAsync(junk);                        // not a recording
            Assert.Equal((series, 1), (vm.Plot.Series.Count, vm.Sources.Count));

            cancel = true;
            await vm.LoadAsync(Second);                     // Configure Streams cancelled
            Assert.Equal((series, 1), (vm.Plot.Series.Count, vm.Sources.Count));
            Assert.Equal(NameOf(Prn), vm.Plot.Title);
        }
        finally
        {
            TempFiles.Delete(junk);
        }
    }

    [AvaloniaFact]
    public async Task ARunCancelledInTheProgressDialogLeavesTheWholePlotAsItWas()
    {
        bool cancelRun = false;
        var progress = new FakeProgressDialog { OnShow = run => { if (cancelRun) run.CancelCommand.Execute(null); } };
        var vm = new MainWindowViewModel(new MainWindowServices { ProcessingDialog = progress });
        await vm.LoadAsync(Prn);
        int series = vm.Plot.Series.Count;

        cancelRun = true;
        await vm.LoadAsync(Second);

        Assert.Equal((series, 1), (vm.Plot.Series.Count, vm.Sources.Count));
        Assert.Equal(NameOf(Prn), Assert.Single(vm.Plot.SourceList()).Label);
        Assert.StartsWith("Cancelled.", vm.Status, System.StringComparison.Ordinal);
    }

    [AvaloniaFact]
    public async Task TheMenuEntryIsTheViewModelsAndAsksThePickerEachTimeAndStacks()
    {
        var picker = new FakeRecordingPicker();
        var vm = new MainWindowViewModel(new MainWindowServices { Picker = picker });
        Assert.True(vm.OpenCommand.CanExecute(null));

        picker.Answers.Enqueue(Prn);
        await vm.OpenCommand.ExecuteAsync(null);
        picker.Answers.Enqueue(Second);
        await vm.OpenCommand.ExecuteAsync(null);                        // with something on the plot: added beside it

        Assert.Equal(2, picker.Asked);
        Assert.Equal(2, vm.Sources.Count);

        // Without a picker there is nothing to ask.
        Assert.False(new MainWindowViewModel().OpenCommand.CanExecute(null));
    }

    [AvaloniaFact]
    public async Task ARecordingTakenOutWithRemoveFileIsNoLongerOneSaveTemplateCaptures()
    {
        var vm = new MainWindowViewModel();
        await vm.LoadAsync(Prn);
        await vm.LoadAsync(Second);
        Assert.Equal(2, vm.Sources.Count);

        vm.Plot.RemoveSource(vm.Sources[1].SourceId);
        Assert.Equal(new[] { Prn }, vm.Sources.Select(s => s.FilePath));

        vm.Plot.RemoveSource(vm.Sources[0].SourceId);
        Assert.Empty(vm.Sources);
    }

    [AvaloniaFact]
    public async Task TheWarningThatARecordingDoesNotOverlapNamesTheRecordingAdded()
    {
        string safran = Repo.Recording("safran_testfile, sample.ch10");
        var vm = new MainWindowViewModel(new MainWindowServices { Configurator = FakeConfigurator.SafranOrDefault(), AppRoot = Repo.Root });
        await vm.LoadAsync(Prn);

        await vm.LoadAsync(safran);

        Assert.Equal(2, vm.Sources.Count);
        // The two recordings are from different days: the warning names the one just added, not nothing or a batch's last file.
        Assert.Contains(vm.Log.Entries, e => e.Text.Contains("does not overlap", System.StringComparison.Ordinal)
                                             && e.Text.Contains("('" + Path.GetFileName(safran) + "')", System.StringComparison.Ordinal));
    }
}
