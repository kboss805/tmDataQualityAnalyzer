using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using TmDq.App.Processing;
using TmDq.App.Services;
using TmDq.App.Streams;
using TmDq.App.Templates;
using TmDq.App.ViewModels;
using TmDq.App.Views;
using TmDq.Core;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// The side-by-side soak (plan step 5.2): every run in <c>Soak/soak.json</c> processed as the application processes
    /// it, its CSV export compared byte for byte with what v2.14.2 (the Qt application) exported for the same run (the
    /// manifest's <c>qt</c> block, recorded by a throwaway harness that drove v2.14.2's own pieces).
    /// <para>
    /// Explicit: most runs read recordings that are not committed (the full recordings, gigabytes, and the local ones
    /// under tests/data), so the tests are listed but not run unless asked for, and a missing recording fails rather than
    /// skips. <c>eng\run_soak.ps1</c> runs them, one process a run. With <c>TMDQ_SOAK_OUT</c> set each run also writes
    /// its CSV, a 1600 x 900 image of its plot in the light theme and <c>result.json</c> (the CSV's length and SHA-256, the
    /// time the run took, the process's peak working set, the log) to a folder of its own there; <c>TMDQ_SOAK_ONLY</c>
    /// (run names, comma separated) narrows the runs listed.
    /// </para>
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class SoakTests
    {
        public SoakTests() => NativeCore.EnsureLoaded();

        public static string ManifestPath => Path.Combine(Repo.Root, "tests", "TmDq.App.Tests", "Soak", "soak.json");

        private static JsonArray LoadRuns() => JsonNode.Parse(File.ReadAllText(ManifestPath))!["runs"]!.AsArray();

        public static IEnumerable<object[]> Runs()
        {
            string[] only = (Environment.GetEnvironmentVariable("TMDQ_SOAK_ONLY") ?? "").Split(',', StringSplitOptions.RemoveEmptyEntries);
            return LoadRuns().Select(r => (string)r!["name"]!)
                             .Where(n => only.Length == 0 || only.Contains(n))
                             .Select(n => new object[] { n });
        }

        [AvaloniaTheory(Explicit = true)]
        [MemberData(nameof(Runs))]
        public async Task EachRunExportsTheCsvV214Exported(string name)
        {
            JsonObject run = LoadRuns().Single(r => (string)r!["name"]! == name)!.AsObject();
            string? outRoot = Environment.GetEnvironmentVariable("TMDQ_SOAK_OUT");
            string dir = outRoot is { Length: > 0 } ? Path.Combine(outRoot, name) : Path.Combine(Path.GetTempPath(), "tmdq-soak-" + Guid.NewGuid().ToString("N"));
            if (Directory.Exists(dir)) Directory.Delete(dir, recursive: true);
            Directory.CreateDirectory(dir);

            var app = (App)Application.Current!;
            var before = app.RequestedThemeVariant;
            app.ApplyTheme(false);
            var view = new PlotView { Width = 1600, Height = 900 };
            var window = new Window { Content = view, SizeToContent = SizeToContent.WidthAndHeight };
            var configurator = new SoakConfigurator(run);
            var vm = new MainWindowViewModel(new MainWindowServices { Configurator = configurator, ImageExporter = new PlotViewImageExporter(view), AppRoot = Repo.Root });
            view.Plot = vm.Plot;
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var result = new JsonObject();
            try
            {
                if (run.ContainsKey("template"))
                {
                    ProcessingTemplate? template = vm.ReadTemplate(Path.Combine(Path.GetDirectoryName(ManifestPath)!, (string)run["template"]!));
                    Assert.NotNull(template);
                    string[] recordings = run["recordings"]!.AsArray().Select(r => Data((string)r!)).ToArray();
                    var clock = Stopwatch.StartNew();
                    await vm.RunBatchAsync(template, recordings, dir, reuseAppearance: true);
                    result["seconds"] = Math.Round(clock.Elapsed.TotalSeconds, 3);
                }
                else
                {
                    await vm.LoadAsync(Data((string)run["recording"]!));
                    result["seconds"] = Math.Round(configurator.Clock!.Elapsed.TotalSeconds, 3);
                    result["calibrationFailed"] = configurator.CalibrationFailed;
                    string csv = Path.Combine(dir, name + ".csv");
                    vm.Plot.ExportCsv(csv);
                    Dispatcher.UIThread.RunJobs();
                    await PlotImageExport.ExportAsync(view, Path.Combine(dir, name + ".png"));
                }
            }
            finally
            {
                window.Close();
                app.RequestedThemeVariant = before;
            }

            var files = new JsonObject();
            foreach (string csv in Directory.GetFiles(dir, "*.csv").OrderBy(f => f, StringComparer.Ordinal))
            {
                byte[] bytes = File.ReadAllBytes(csv);
                files[Path.GetFileName(csv)] = new JsonArray((long)bytes.Length, Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant());
            }
            result["files"] = files;
            result["peakWorkingSetMb"] = Math.Round(Process.GetCurrentProcess().PeakWorkingSet64 / 1048576.0, 1);
            result["log"] = new JsonArray(configurator.Log.Concat(vm.Log.Entries.Select(e => e.Level + " " + e.Text)).Select(l => (JsonNode)l!).ToArray());
            File.WriteAllText(Path.Combine(dir, "result.json"), result.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));

            // What v2.14.2 wrote for this run, file by file.
            JsonObject? qt = run["qt"]?["files"]?.AsObject();
            Assert.True(qt is not null, $"{name}: the manifest holds no Qt result for this run");
            Assert.Equal(qt!.Select(f => f.Key).OrderBy(k => k, StringComparer.Ordinal), files.Select(f => f.Key));
            foreach ((string file, JsonNode? expected) in qt!)
            {
                JsonArray mine = files[file]!.AsArray();
                Assert.True(((long)expected![0]!, (string)expected[1]!) == ((long)mine[0]!, (string)mine[1]!),
                            $"{name}/{file}: {(long)mine[0]!} bytes, SHA-256 {(string)mine[1]!}; the Qt application wrote {(long)expected[0]!} bytes, {(string)expected[1]!}");
            }
            if (outRoot is not { Length: > 0 }) Directory.Delete(dir, recursive: true);
        }

        /// <summary>A recording under tests/data; a missing one fails, never skips.</summary>
        private static string Data(string relative)
        {
            string path = Path.Combine(Repo.Root, "tests", "data", relative);
            Assert.True(File.Exists(path), $"the soak needs {path}, which is not on this machine (the full and local recordings are not committed)");
            return path;
        }

        private static string Settings(string relative) => Path.Combine(Repo.Root, "settings", relative);

        /// <summary>
        /// Configure Streams as the user would leave it for a run: the real dialog's view model over the recording, each
        /// of the run's channels set up and checked as its setup dialog's OK leaves it (with Apply Cal's calibration,
        /// extracted by the real Extract Calibration view model, where the run has one), then Process. What it hands
        /// back is what <see cref="DialogConfigurator"/> hands back.
        /// </summary>
        private sealed class SoakConfigurator : IStreamConfigurator
        {
            private readonly JsonObject _run;

            public SoakConfigurator(JsonObject run) => _run = run;

            /// <summary>Started when Process is pressed, where the v2.14.2 harness started its clock.</summary>
            public Stopwatch? Clock { get; private set; }

            public bool CalibrationFailed { get; private set; }

            public List<string> Log { get; } = new();

            public async Task<StreamsChoice?> ConfigureAsync(string path, Ch10File file)
            {
                var dialog = new ConfigureStreamsViewModel(path, file, null, null, Repo.Root) { LegacyChapter10 = !(bool)_run["swapBytes"]! };
                foreach (JsonNode? node in _run["streams"]!.AsArray())
                {
                    JsonObject s = node!.AsObject();
                    StreamRowViewModel row = dialog.Rows.Single(r => r.Setup.PcmChannelId == (int)s["channel"]!);
                    StreamMode mode = (string)s["mode"]! == "snr" ? StreamMode.ReceiverChannelInfo : StreamMode.FrameSyncLock;
                    string receiverParams = (string)s["receiverParams"]!;
                    StreamSetup setup = row.Setup with
                    {
                        Process = true,
                        Mode = mode,
                        SettingsMode = mode,
                        Configured = true,
                        SyncPattern = (string)s["pattern"]!,
                        SyncMask = (string)s["mask"]!,
                        BitsInMinorFrame = (int)s["bits"]!,
                        Randomized = (bool)s["randomized"]!,
                        Inverted = (bool)s["inverted"]!,
                        DataRateMbps = (double)s["dataRateMbps"]!,
                        SamplePeriod = (string)s["period"]! switch
                        {
                            "1s" => SamplePeriod.OneSecond,
                            "100ms" => SamplePeriod.HundredMilliseconds,
                            _ => SamplePeriod.TenMilliseconds,
                        },
                        PolarityIndex = (int)s["polarity"]!,
                        SlopeIndex = (int)s["slope"]!,
                        ScaleDdBPerV = (double)s["scale"]!,
                        NumReceivers = (int)s["receivers"]!,
                        ReceiverChannels = (int)s["channels"]!,
                        ReceiverParamsToml = receiverParams.Length == 0 ? string.Empty : Settings(receiverParams),
                    };
                    if (s["calibration"] is JsonObject cal)
                        setup = await CalibratedAsync(setup, cal, dialog.TimeChannelId, !dialog.LegacyChapter10);
                    row.Setup = setup;
                }
                Assert.True(dialog.CanProcess, "Configure Streams would not process: " + dialog.WhyNotProcess);
                dialog.ProcessCommand.Execute(null);
                Assert.True(dialog.Accepted);
                Clock = Stopwatch.StartNew();
                return new StreamsChoice(dialog.ToRunRequest(), dialog.Setups, dialog.TimeChannelIndex);
            }

            /// <summary>Apply Cal: the Extract Calibration dialog over the setup as it stands, its clips set, the
            /// calibration recording chosen, and OK keeping what it extracted.</summary>
            private async Task<StreamSetup> CalibratedAsync(StreamSetup s, JsonObject cal, int timeChannelId, bool swapBytes)
            {
                var calibrationBase = new CalibrationBase(timeChannelId, s.PcmChannelId, s.SyncPattern.Trim().ToUpperInvariant(),
                                                          s.SyncMask.Trim().ToUpperInvariant(), s.BitsInMinorFrame, s.Randomized,
                                                          s.Inverted, s.DataRateMbps, swapBytes, s.ReceiverParamsToml,
                                                          s.NumReceivers, s.ReceiverChannels);
                var extract = new ExtractCalibrationViewModel(calibrationBase, Repo.Root, Settings((string)cal["steps"]!))
                {
                    ClipStart = (decimal)(double)cal["clipStart"]!,
                    ClipEnd = (decimal)(double)cal["clipEnd"]!,
                };
                await extract.LoadCalibrationAsync(Data((string)cal["recording"]!));
                if (extract.Extraction is { } running) await running;
                Log.Add("CAL " + extract.CalStatus);
                if (!extract.CanAccept)
                {
                    CalibrationFailed = true;
                    return s;
                }
                extract.OkCommand.Execute(null);
                return s with
                {
                    Calibration = extract.Profiles,
                    CalCh10Path = extract.CalPath,
                    StepTomlPath = extract.StepPath,
                    ClipStartSec = extract.ClipStartSec,
                    ClipEndSec = extract.ClipEndSec,
                };
            }
        }
    }
}
