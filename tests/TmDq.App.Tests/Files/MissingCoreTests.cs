using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Headless.XUnit;
using TmDq.App.Models.Logging;
using TmDq.App.ViewModels;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// The collection of the tests that point TMDQ_CORE_DLL at a missing file for a moment. xunit runs a collection that
/// disables parallelization alone, after the others have finished, so a load from another class never meets the
/// missing file; and no other class has to wait for it, which is why none of them shares a collection any more.
/// </summary>
[CollectionDefinition(Name, DisableParallelization = true)]
public sealed class MissingCoreCollection
{
    public const string Name = "Missing core";
}

/// <summary>What Open and the batch say when tmdq_core.dll cannot be loaded: in the status and the log, not thrown.</summary>
[Collection(MissingCoreCollection.Name)]
public sealed class MissingCoreTests
{
    private static string Prn => Repo.Recording("prn_testfile, sample.ch10");

    /// <summary>Runs <paramref name="body"/> with TMDQ_CORE_DLL pointing at a file that is not there.</summary>
    private static async Task WithNoCoreAsync(Func<Task> body)
    {
        string? before = Environment.GetEnvironmentVariable("TMDQ_CORE_DLL");
        Environment.SetEnvironmentVariable("TMDQ_CORE_DLL", Path.Combine(Path.GetTempPath(), "tmdq-no-such-core", "tmdq_core.dll"));
        try
        {
            await body();
        }
        finally
        {
            Environment.SetEnvironmentVariable("TMDQ_CORE_DLL", before);
        }
    }

    [AvaloniaFact]
    public Task AMissingNativeCoreIsReportedNotThrown() => WithNoCoreAsync(async () =>
    {
        var vm = new MainWindowViewModel();
        await vm.LoadAsync(Prn);
        Assert.Null(Startup.FirstLockSeries(vm.Plot));
        Assert.False(vm.IsBusy);
        Assert.StartsWith("The processing core could not be loaded: ", vm.Status);
        Assert.Equal((vm.Status, LogLevel.Error), (vm.Log.Entries[^1].Text, vm.Log.Entries[^1].Level));
    });

    [Fact]
    public Task ApplyTemplateWithoutTheNativeCoreSaysSoAndStops() => WithNoCoreAsync(async () =>
    {
        // As Open, Import CSV and the batch say it: in the status and the log, not thrown out of the command. The
        // check of the files is the first thing that needs the core, after the template and the recordings are chosen.
        string template = Path.Combine(Repo.Root, "tests", "TmDq.App.Tests", "Templates", "prn-sample.json");
        var files = new TemplateBatchTests.FakeTemplateFiles { Template = template, Recordings = new[] { Prn } };
        var dialogs = new TemplateBatchTests.FakeTemplateDialogs();
        var vm = new MainWindowViewModel(new MainWindowServices { TemplateFiles = files, TemplateDialogs = dialogs, AppRoot = Repo.Root });
        await vm.ApplyTemplateCommand.ExecuteAsync(null);
        Assert.StartsWith(MainWindowViewModel.CoreNotLoaded, vm.Status);
        Assert.Equal((vm.Status, LogLevel.Error), (vm.Log.Entries[^1].Text, vm.Log.Entries[^1].Level));
        Assert.Null(dialogs.Shown);   // nothing was asked
        Assert.False(vm.IsBusy);
    });
}
