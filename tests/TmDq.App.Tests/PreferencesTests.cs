using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Settings;
using TmDq.App.Services;
using TmDq.App.ViewModels;
using TmDq.App.ViewModels.Plot;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// What is remembered between sessions (plan step 4.8): the preferences file, the recent-files rules, and the main
    /// window's view model keeping each preference.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class PreferencesTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "tmdq-prefs-" + Guid.NewGuid().ToString("N"));

        public PreferencesTests() => Directory.CreateDirectory(_dir);

        public void Dispose() => TempFiles.DeleteDirectory(_dir, recursive: true);

        private static string Prn => Repo.Recording("prn_testfile, sample.ch10");

        // ---- the recent files ----------------------------------------------------------------------------------------

        [Fact]
        public void ARecentFileGoesFirstOnceAndTheListKeepsFive()
        {
            IReadOnlyList<string> files = Array.Empty<string>();
            foreach (string f in new[] { "a", "b", "c", "d", "e", "f" }) files = RecentFiles.Add(files, f);
            Assert.Equal(new[] { "f", "e", "d", "c", "b" }, files);
            Assert.Equal(new[] { "c", "f", "e", "d", "b" }, RecentFiles.Add(files, "c"));
            Assert.Equal(new[] { "C", "f", "e", "d", "c" }, RecentFiles.Add(files, "C"));   // case-sensitive
            Assert.Equal(new[] { "b", "d" }, RecentFiles.Prune(new[] { "", "b", "x", "d" }, f => f != "x"));
            Assert.Equal(5, RecentFiles.Prune(new[] { "1", "2", "3", "4", "5", "6" }, _ => true).Count);
        }

        [Theory]
        [InlineData("flight.ch10", true, false)]
        [InlineData("FLIGHT.CH10", true, false)]
        [InlineData("export.csv", true, true)]
        [InlineData("EXPORT.CSV", true, true)]
        [InlineData("notes.txt", false, false)]
        [InlineData("archive.csv.zip", false, false)]
        [InlineData("", false, false)]
        public void ARecordingOrAnExportedCsvIsOpenedByItsSuffix(string path, bool supported, bool csv)
        {
            Assert.Equal(supported, RecentFiles.IsSupported(path));
            Assert.Equal(csv, RecentFiles.IsCsv(path));
        }

        // ---- the preferences file ------------------------------------------------------------------------------------

        private string Folder(string name)
        {
            string folder = Path.Combine(_dir, name);
            Directory.CreateDirectory(folder);
            return folder;
        }

        [Fact]
        public void TheV2SettingsFileIsLeftAloneAndTheFirstRunStartsFromTheDefaults()
        {
            // v3.0.x imported v2.x's INI file on the first run; from v3.1.0 it is not read, and not touched.
            string folder = Folder("first");
            string ini = Path.Combine(folder, "tmDataQualityAnalyzer.ini");
            const string v2 = "[General]\r\nTheme=light\r\nSidebarVisible=false\r\nLegendVisible=false\r\n";
            File.WriteAllText(ini, v2);
            PreferencesStore store = PreferencesStore.Load(folder);
            Assert.Equal(new Preferences(), store.Current);
            Assert.True(File.Exists(Path.Combine(folder, PreferencesStore.FileName)), "the defaults were not written");
            Assert.Equal(v2, File.ReadAllText(ini));
        }

        [Fact]
        public void WithNothingToReadThePreferencesAreTheDefaultsAndWritten()
        {
            string folder = Path.Combine(_dir, "fresh");   // not even the folder exists
            PreferencesStore store = PreferencesStore.Load(folder);
            Assert.Equal(new Preferences(), store.Current);
            Assert.Equal(("dark", true, true, true), (store.Current.Theme, store.Current.IsDark, store.Current.SidebarVisible, store.Current.LegendVisible));
            Assert.True(File.Exists(Path.Combine(folder, PreferencesStore.FileName)));

            // A file that cannot be read is the defaults; so are keys it lacks, and a null where a value belongs.
            string bad = Folder("bad");
            File.WriteAllText(Path.Combine(bad, PreferencesStore.FileName), "{ not json");
            Assert.Equal(new Preferences(), PreferencesStore.Load(bad).Current);
            string partial = Folder("partial");
            File.WriteAllText(Path.Combine(partial, PreferencesStore.FileName), "{\"SidebarVisible\": false, \"Theme\": null, \"RecentFiles\": null}");
            Assert.Equal(new Preferences { SidebarVisible = false }, PreferencesStore.Load(partial).Current);
            string holes = Folder("holes");
            File.WriteAllText(Path.Combine(holes, PreferencesStore.FileName), "{\"RecentFiles\": [null, \"C:\\\\a.ch10\"]}");
            Assert.Equal(new[] { @"C:\a.ch10" }, PreferencesStore.Load(holes, _ => true).Current.RecentFiles);   // an entry that is null skipped
            string nulls = Folder("nulls");
            File.WriteAllText(Path.Combine(nulls, PreferencesStore.FileName), "null");
            Assert.Equal(new Preferences(), PreferencesStore.Load(nulls).Current);
        }

        [Fact]
        public void EveryChangeIsWrittenAtOnceAndOnlyAChange()
        {
            string folder = Folder("writes");
            PreferencesStore store = PreferencesStore.Load(folder, _ => true);
            string file = Path.Combine(folder, PreferencesStore.FileName);
            store.Update(p => p with { RecentFiles = new[] { @"C:\a.ch10" }, LastCalibrationFolder = @"D:\cal" });
            Preferences reread = PreferencesStore.Load(folder, _ => true).Current;
            Assert.Equal(new[] { @"C:\a.ch10" }, reread.RecentFiles);
            Assert.Equal(@"D:\cal", reread.LastCalibrationFolder);

            // The same preferences again are not written (a copy of the list is the same list).
            File.SetLastWriteTimeUtc(file, new DateTime(2001, 1, 1, 0, 0, 0, DateTimeKind.Utc));
            store.Update(p => p with { RecentFiles = p.RecentFiles.ToList() });
            Assert.Equal(new DateTime(2001, 1, 1, 0, 0, 0, DateTimeKind.Utc), File.GetLastWriteTimeUtc(file));

            // A folder that cannot be written keeps the change for the session.
            string blocker = Path.Combine(_dir, "a file");
            File.WriteAllText(blocker, "");
            var unwritable = new PreferencesStore(new Preferences(), Path.Combine(blocker, PreferencesStore.FileName));
            unwritable.Update(p => p with { Theme = Preferences.LightTheme });
            Assert.Equal("light", unwritable.Current.Theme);
            var memory = new PreferencesStore(new Preferences());
            memory.Update(p => p with { SidebarVisible = false });
            Assert.False(memory.Current.SidebarVisible);
        }

        [Fact]
        public void APortableCopyKeepsItsPreferencesBesideItself()
        {
            string app = Folder("app"), appData = Folder("appdata");
            Assert.Equal(Path.Combine(appData, "tmDataQualityAnalyzer"), PreferencesStore.FolderFor(app, appData));
            File.WriteAllText(Path.Combine(app, PreferencesStore.PortableMarker), "");
            Assert.Equal(Path.Combine(app, "tmDataQualityAnalyzer"), PreferencesStore.FolderFor(app, appData));
        }

        [Fact]
        public void PreferencesAreEqualByWhatTheyHold()
        {
            var a = new Preferences { RecentFiles = new[] { "x" } };
            Assert.Equal(a, a with { RecentFiles = new List<string> { "x" } });
            Assert.Equal(a.GetHashCode(), (a with { RecentFiles = new List<string> { "x" } }).GetHashCode());
            Assert.NotEqual(a, a with { RecentFiles = new[] { "y" } });
            Assert.NotEqual(a, a with { Theme = "light" });
            Assert.NotEqual(a, a with { SidebarVisible = false });
            Assert.NotEqual(a, a with { LegendVisible = false });
            Assert.NotEqual(a, a with { LastFolder = "f" });
            Assert.NotEqual(a, a with { LastCalibrationFolder = "f" });
            Assert.False(a.Equals(null));
            Assert.False(new Preferences { Theme = "light" }.IsDark);
            Assert.True(new Preferences { Theme = "anything" }.IsDark);   // all but "light" is dark, as v2.x read it
        }

        // ---- the main window keeps each preference --------------------------------------------------------------------

        [Fact]
        public async Task TheMainWindowRemembersItsFolderItsRecentFilesAndItsChoices()
        {
            var store = new PreferencesStore(new Preferences { LastFolder = @"C:\before", LegendVisible = false, Theme = "light", SidebarVisible = false });
            var vm = new MainWindowViewModel(new MainWindowServices { Configurator = FakeConfigurator.Cancelled(), Preferences = store });
            Assert.Equal((@"C:\before", false, false, false), (vm.LastFolder, vm.Plot.LegendVisible, vm.IsDarkTheme, vm.SidebarVisible));
            Assert.Same(store.Current, vm.Preferences);

            // A recording joins Recent Files once its channels are read, even when Configure Streams is then cancelled, by
            // its full path however it was named.
            await vm.LoadAsync(Path.Combine(Path.GetDirectoryName(Prn)!, ".", Path.GetFileName(Prn)));
            Assert.Equal(new[] { Prn }, vm.RecentFiles);
            Assert.Equal(Path.GetDirectoryName(Prn), store.Current.LastFolder);
            Assert.Equal(new[] { Prn }, store.Current.RecentFiles);

            // A file that is not a recording does not.
            string notRecording = Path.Combine(_dir, "not.ch10");
            File.WriteAllText(notRecording, "no");
            await vm.LoadAsync(notRecording);
            Assert.Equal(new[] { Prn }, vm.RecentFiles);

            // An imported CSV does, once it is read; a CSV that is not an export does not.
            vm.Plot.SetSeries(new[] { PlotFixtures.Lock(0, "a", 1, 0, new[] { 0.0, 1.0 }, new[] { 50.0, 60.0 }) });
            string csv = Path.Combine(_dir, "export.csv"), bad = Path.Combine(_dir, "bad.csv");
            Assert.True(vm.Plot.ExportCsv(csv));
            File.WriteAllText(bad, "Day,Time\n1,2\n");
            var told = new List<string?>();
            vm.PropertyChanged += (_, e) => told.Add(e.PropertyName);
            await vm.ImportCsvAsync(bad);
            Assert.Equal(new[] { Prn }, vm.RecentFiles);
            await vm.ImportCsvAsync(csv);
            Assert.Equal(new[] { csv, Prn }, vm.RecentFiles);
            Assert.Contains(nameof(MainWindowViewModel.RecentFiles), told);
            vm.ClearRecentFilesCommand.Execute(null);
            Assert.Empty(vm.RecentFiles);
            Assert.Empty(store.Current.RecentFiles);

            // The legend, the theme and the sidebar are kept as they change, and said so.
            told.Clear();
            vm.Plot.LegendVisible = true;
            vm.ToggleThemeCommand.Execute(null);
            vm.ToggleSidebarCommand.Execute(null);
            Assert.Equal((true, "dark", true), (store.Current.LegendVisible, store.Current.Theme, store.Current.SidebarVisible));
            Assert.Equal(("Switch to Light Theme", true, true), (vm.ThemeMenuText, vm.IsDarkTheme, vm.SidebarVisible));
            Assert.Equal(new[] { nameof(MainWindowViewModel.IsDarkTheme), nameof(MainWindowViewModel.ThemeMenuText), nameof(MainWindowViewModel.SidebarVisible) },
                         told.Where(n => n is nameof(MainWindowViewModel.IsDarkTheme) or nameof(MainWindowViewModel.ThemeMenuText) or nameof(MainWindowViewModel.SidebarVisible)));
            vm.ToggleThemeCommand.Execute(null);
            Assert.Equal(("light", "Switch to Dark Theme"), (store.Current.Theme, vm.ThemeMenuText));
            told.Clear();
            vm.IsDarkTheme = false;   // no change, no notice
            vm.SidebarVisible = true;
            Assert.Empty(told);

            // Without a store the defaults hold, in memory.
            var plain = new MainWindowViewModel();
            Assert.Equal(((string?)null, true, true, true, 0), (plain.LastFolder, plain.IsDarkTheme, plain.SidebarVisible, plain.Plot.LegendVisible, plain.RecentFiles.Count));
        }

        [Fact]
        public async Task ARecentFileOpensByItsSuffixAndNotWhileBusy()
        {
            var vm = new MainWindowViewModel(new MainWindowServices { Configurator = FakeConfigurator.Cancelled() });
            Assert.False(vm.OpenRecentCommand.CanExecute(null));
            Assert.True(vm.OpenRecentCommand.CanExecute(Prn));
            await vm.OpenRecentCommand.ExecuteAsync(Prn);
            Assert.Equal("prn_testfile, sample.ch10: not processed (Configure Streams was cancelled).", vm.Status);

            vm.Plot.SetSeries(new[] { PlotFixtures.Lock(0, "a", 1, 0, new[] { 0.0, 1.0 }, new[] { 50.0, 60.0 }) });
            string csv = Path.Combine(_dir, "Upper.CSV");
            Assert.True(vm.Plot.ExportCsv(csv));
            await vm.OpenPathAsync(csv);
            Assert.Equal("Imported CSV: Upper.CSV", vm.Status);

            // Busy, it is unavailable, and a path given anyway does nothing; it is told when that ends.
            var gate = new TaskCompletionSource<StreamsChoice?>();
            var slow = new MainWindowViewModel(new MainWindowServices { Configurator = FakeConfigurator.Waiting(gate.Task) });
            var available = new List<bool>();
            slow.OpenRecentCommand.CanExecuteChanged += (_, _) => available.Add(slow.OpenRecentCommand.CanExecute(Prn));
            Task first = slow.OpenPathAsync(Prn);
            Assert.True(slow.IsBusy);
            Task second = slow.OpenPathAsync(csv);
            Assert.True(second.IsCompleted);
            gate.SetResult(null);
            await first;
            Assert.Equal(new[] { false, true }, available);
            Assert.DoesNotContain(slow.Log.Entries, e => e.Text.Contains("Upper.CSV", StringComparison.Ordinal));
        }

        private sealed class CalFiles : TmDq.App.Services.ICalibrationFilePicker
        {
            public Task<string?> OpenStepFileAsync(string? folder) => Task.FromResult<string?>(null);
            public Task<string?> OpenCalibrationRecordingAsync(string? folder) => Task.FromResult<string?>(Repo.Recording("step_cal, example.ch10"));
        }

        [Fact]
        public async Task TheLastCalibrationFolderIsKeptBetweenSessions()
        {
            var store = new PreferencesStore(new Preferences { LastCalibrationFolder = @"D:\cal" });
            string? seen = null;
            var dialogs = new FakeStreamDialogs { OnConfigure = c => { seen = c.LastCalFolder; return Task.CompletedTask; } };
            Ch10File file = StreamsRun.Open(Prn);
            Assert.Null(await new DialogConfigurator(dialogs, null, Repo.Root, null, store).ConfigureAsync(Prn, file));
            Assert.Equal(@"D:\cal", seen);
            Assert.Equal(@"D:\cal", store.Current.LastCalibrationFolder);
            Assert.Null(new ConfigureStreamsViewModel(Prn, file, dialogs, null, Repo.Root).LastCalFolder);
            // The folder Extract Calibration's recording came from is kept for the next session.
            var extracting = new FakeStreamDialogs();
            extracting.OnConfigure = async c =>
            {
                StreamRowViewModel row = c.Rows[0];
                row.Process = true;
                row.ModeIndex = 0;   // Receiver SNR
                await row.ConfigureCommand.ExecuteAsync(null);
            };
            extracting.OnEdit = async setup =>
            {
                await setup.ApplyCalCommand.ExecuteAsync(null);
                setup.CancelCommand.Execute(null);
            };
            extracting.OnExtract = async e =>
            {
                await e.BrowseCalCommand.ExecuteAsync(null);
                e.CancelCommand.Execute(null);
            };
            await new DialogConfigurator(extracting, null, Repo.Root, new CalFiles(), store).ConfigureAsync(Prn, file);
            Assert.Equal(Path.GetDirectoryName(Repo.Recording("step_cal, example.ch10")), store.Current.LastCalibrationFolder);
            // Without a store there is nothing to seed or keep.
            seen = "unset";
            await new DialogConfigurator(dialogs, null, Repo.Root).ConfigureAsync(Prn, file);
            Assert.Null(seen);
        }
    }
}
