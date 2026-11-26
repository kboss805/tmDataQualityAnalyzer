using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Headless.XUnit;
using TmDq.App.Models.Export;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Streams;
using TmDq.App.Services;
using TmDq.App.ViewModels;
using TmDq.App.ViewModels.Plot;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// Add Recording: processes another recording as Open does, but keeps everything on the plot (the recordings processed before and
/// what was imported), each recording its own entry in Plot File and Remove File. With an empty plot it is Open.
/// </summary>
[Collection(NativeCoreCollection.Name)]
public sealed class AddRecordingTests : System.IDisposable
{
    private readonly string _second = TempFiles.NewPath("flight 2", ".ch10");

    public AddRecordingTests()
    {
        NativeCore.EnsureLoaded();
        File.Copy(Prn, _second);   // a second recording to add: the committed sample again, under another name
    }

    public void Dispose() => TempFiles.Delete(_second);

    private static string Prn => Repo.Recording("prn_testfile, sample.ch10");

    private string Second => _second;

    private static string AntennaLog => Repo.Recording("av_snr_log.csv");

    private static string NameOf(string path) => MainWindowViewModel.BaseName(Path.GetFileName(path));

    private sealed class FakeKinds : ICsvKindDialog
    {
        public Task<CsvKind?> ChooseAsync(string fileName) => Task.FromResult<CsvKind?>(CsvKind.PhasedArrayAntenna);
    }

    /// <summary>The defaults for each recording, until it is told to cancel.</summary>
    private sealed class CancellableConfigurator : IStreamConfigurator
    {
        public bool Cancel;

        public Task<StreamsChoice?> ConfigureAsync(string path, Ch10File file) =>
            Cancel
                ? Task.FromResult<StreamsChoice?>(null)
                : new FirstChannelRun().ConfigureAsync(path, file);
    }

    [AvaloniaFact]
    public async Task AddingARecordingKeepsTheOneOnThePlotAndAddsTheNewBesideIt()
    {
        var vm = new MainWindowViewModel();
        await vm.LoadAsync(Prn);
        PlotSeries first = Assert.Single(PlotFixtures.Locks(vm));
        int onePlot = vm.Plot.Series.Count;
        int firstId = vm.Sources[0].SourceId;

        await vm.LoadAsync(Second, add: true);

        Assert.Same(first, Assert.Single(vm.Plot.Series, s => s.SourceId == firstId && s.Metric == MetricType.FrameSyncLock));   // untouched
        Assert.True(vm.Plot.Series.Count > onePlot);
        Assert.Equal(2, vm.Sources.Count);                              // Save Template is offered both
        Assert.Equal(new[] { NameOf(Prn), NameOf(Second) }, vm.Plot.SourceList().Select(x => x.Label));
        Assert.Equal(2, vm.Sources.Select(s => s.SourceId).Distinct().Count());
        Assert.Equal(NameOf(Prn), vm.Plot.Title);                       // the plot's title is the first's
        Assert.Equal(new[] { Prn, Second }, vm.Sources.Select(s => s.FilePath));
    }

    [AvaloniaFact]
    public async Task AddingWithNothingOnThePlotIsOpen()
    {
        var vm = new MainWindowViewModel();

        await vm.LoadAsync(Prn, add: true);

        Assert.Equal(NameOf(Prn), vm.Plot.Title);
        Assert.Single(vm.Sources);
        Assert.Equal(NameOf(Prn), Assert.Single(vm.Plot.SourceList()).Label);
        Assert.Equal(0, vm.Sources[0].SourceId);
        Assert.Contains(vm.Log.Entries, e => e.Text.Contains("Opening: ", System.StringComparison.Ordinal));
    }

    [AvaloniaFact]
    public async Task OpenStillReplacesTheRecordingsOnThePlot()
    {
        var vm = new MainWindowViewModel();
        await vm.LoadAsync(Prn);
        await vm.LoadAsync(Second, add: true);
        Assert.Equal(2, vm.Sources.Count);

        await vm.LoadAsync(Prn);

        Assert.Single(vm.Sources);
        Assert.Equal(NameOf(Prn), Assert.Single(vm.Plot.SourceList()).Label);
        Assert.Equal(NameOf(Prn), vm.Plot.Title);
    }

    [AvaloniaFact]
    public async Task TheSameRecordingAddedAgainIsNamedApartInPlotFileAndRemoveFile()
    {
        var vm = new MainWindowViewModel();
        await vm.LoadAsync(Prn);
        await vm.LoadAsync(Prn, add: true);
        await vm.LoadAsync(Prn, add: true);

        string name = NameOf(Prn);
        Assert.Equal(new[] { name, name + " (2)", name + " (3)" }, vm.Plot.SourceList().Select(x => x.Label));
        Assert.Equal(3, vm.Sources.Count);
        Assert.Equal(3, vm.Sources.Select(s => s.SourceId).Distinct().Count());

        // Removing the middle one with Remove File leaves the other two.
        vm.Plot.RemoveSource(vm.Sources[1].SourceId);
        Assert.Equal(new[] { name, name + " (3)" }, vm.Plot.SourceList().Select(x => x.Label));
    }

    [AvaloniaFact]
    public async Task AddingKeepsWhatWasImportedAsWellAsWhatWasProcessed()
    {
        var vm = new MainWindowViewModel(new MainWindowServices { CsvKinds = new FakeKinds() });
        await vm.ImportCsvAsync(AntennaLog);
        await vm.LoadAsync(Prn);
        await vm.LoadAsync(Second, add: true);

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
            var configurator = new CancellableConfigurator();
            var vm = new MainWindowViewModel(new MainWindowServices { Configurator = configurator });
            await vm.LoadAsync(Prn);
            int series = vm.Plot.Series.Count;

            await vm.LoadAsync(junk, add: true);                        // not a recording
            Assert.Equal((series, 1), (vm.Plot.Series.Count, vm.Sources.Count));

            configurator.Cancel = true;
            await vm.LoadAsync(Second, add: true);                     // Configure Streams cancelled
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
        var progress = new CancelOnDemandProgress();
        var vm = new MainWindowViewModel(new MainWindowServices { ProcessingDialog = progress });
        await vm.LoadAsync(Prn);
        int series = vm.Plot.Series.Count;

        progress.Cancel = true;
        await vm.LoadAsync(Second, add: true);

        Assert.Equal((series, 1), (vm.Plot.Series.Count, vm.Sources.Count));
        Assert.Equal(NameOf(Prn), Assert.Single(vm.Plot.SourceList()).Label);
        Assert.StartsWith("Cancelled.", vm.Status, System.StringComparison.Ordinal);
    }

    private sealed class CancelOnDemandProgress : IProcessingDialog
    {
        public bool Cancel;

        public void Show(TmDq.App.ViewModels.Dialogs.ProcessingViewModel vm)
        {
            if (Cancel) vm.CancelCommand.Execute(null);
        }

        public void Close() { }
    }

    [AvaloniaFact]
    public async Task TheMenuEntryIsTheViewModelsAndAsksThePicker()
    {
        var picker = new FakePicker();
        var vm = new MainWindowViewModel(new MainWindowServices { Picker = picker });
        Assert.True(vm.AddRecordingCommand.CanExecute(null));

        picker.Answers.Enqueue(Prn);
        await vm.AddRecordingCommand.ExecuteAsync(null);                // an empty plot: it is Open
        picker.Answers.Enqueue(Second);
        await vm.AddRecordingCommand.ExecuteAsync(null);

        Assert.Equal(2, picker.Asked);
        Assert.Equal(2, vm.Sources.Count);

        // Without a picker there is nothing to ask, as for Open.
        Assert.False(new MainWindowViewModel().AddRecordingCommand.CanExecute(null));
    }

    private sealed class FakePicker : IRecordingPicker
    {
        public readonly Queue<string?> Answers = new();
        public int Asked;

        public Task<string?> PickRecordingAsync()
        {
            Asked++;
            return Task.FromResult(Answers.Count > 0 ? Answers.Dequeue() : null);
        }
    }

    [AvaloniaFact]
    public async Task ARecordingTakenOutWithRemoveFileIsNoLongerOneSaveTemplateCaptures()
    {
        var vm = new MainWindowViewModel();
        await vm.LoadAsync(Prn);
        await vm.LoadAsync(Second, add: true);
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
        var vm = new MainWindowViewModel(new MainWindowServices { Configurator = new SafranOrDefault(), AppRoot = Repo.Root });
        await vm.LoadAsync(Prn);

        await vm.LoadAsync(safran, add: true);

        Assert.Equal(2, vm.Sources.Count);
        // The two recordings are from different days: the warning names the one just added, not nothing or a batch's last file.
        Assert.Contains(vm.Log.Entries, e => e.Text.Contains("does not overlap", System.StringComparison.Ordinal)
                                             && e.Text.Contains(Path.GetFileName(safran), System.StringComparison.Ordinal));
    }

    /// <summary>Safran's channel 13, as 4.5's oracle runs it (its own byte order); the defaults for everything else.</summary>
    private sealed class SafranOrDefault : IStreamConfigurator
    {
        public Task<StreamsChoice?> ConfigureAsync(string path, Ch10File file)
        {
            if (!Path.GetFileName(path).StartsWith("safran", System.StringComparison.Ordinal)) return new FirstChannelRun().ConfigureAsync(path, file);
            var stream = new StreamConfig
            {
                PcmChannelId = 13, Label = "PCM03", Mode = StreamMode.FrameSyncLock, SyncPattern = "334AABBF", SyncMask = "FFFFFFFF",
                BitsInMinorFrame = 32767, SamplePeriod = FrameSyncDefaults.Period,
            };
            var request = new RunRequest(path, file.DefaultTimeChannel, new[] { stream }) { SwapBytes = false };
            return Task.FromResult<StreamsChoice?>(new StreamsChoice(request, System.Array.Empty<StreamSetup>(), 0));
        }
    }
}
