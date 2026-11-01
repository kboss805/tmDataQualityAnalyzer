using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using System.Xml.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.LogicalTree;
using Avalonia.Media;
using Avalonia.Threading;
using SkiaSharp;
using TmDq.App.Plot;
using TmDq.App.Processing;
using TmDq.App.Services;
using TmDq.App.ViewModels;
using TmDq.App.Views;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// Export and import (plan step 4.5): the plot as CSV, as an image and the log as text, the Export dialog, and an
    /// exported CSV read back, with the Qt application's rejections and messages. The CSV oracle is the Qt application
    /// itself: each committed recording was processed by the Qt pipeline into the Qt plot and exported by it, once, and
    /// the app must write the very same bytes.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class ExportImportTests
    {
        public ExportImportTests() => NativeCore.EnsureLoaded();

        // ---- the oracle: the Qt application's export of every committed recording ---------------------------------------

        /// <summary>
        /// What the Qt application wrote for each scenario (its size and SHA-256), recorded by a throwaway case in its own
        /// suite: the core's job builder over these very settings, a <c>ProcessingCoordinator</c> run, each stream handed
        /// to the Qt <c>PlotViewModel::addStreamData</c> as it finished, then <c>exportCsv</c>. One stream mode a run, as
        /// Configure Streams allows one row a channel.
        /// </summary>
        public static IEnumerable<object[]> QtExports() => new[]
        {
            new object[] { "prn", 9479, "1be395481250be54acbd94c95a093be75b166e7e5616abf94895b622a91e305a" },
            new object[] { "step_cal", 32537, "61d80af27c78596230e299beab5a0be4052761c8a3ed6cac4507037623aacf43" },
            new object[] { "step_cal_lock", 2617, "b6065e5b3fe5e8d888b8aad4287ee9967821f23155e1696149521d11b7fe74be" },
            new object[] { "trc", 92308, "b85a3fe76e6ec21f688b8fe4ffe4a96c636e4899338da2a7f3b220799c83a0ea" },
            new object[] { "trc_lock", 7691, "93f447d7e8d3a032abecf8b70c35c5ff5513e52b907ff67b2326901e888b3387" },
            new object[] { "rasa", 43697, "997029776bd9b5db8602627c87f5cd06766c8ac95b020a387bfd332549a4d7f4" },
            new object[] { "safran", 6225, "a190071b0bc89677594aba00eb78a38fad1976f95fa9dcf005d590434c23234f" },
        };

        private static StreamConfig Lock(int channel, string label, string pattern, int bits, bool randomized = false) => new()
        {
            PcmChannelId = channel, Label = label, SyncPattern = pattern, BitsInMinorFrame = bits, Randomized = randomized,
        };

        private static StreamConfig Receivers(int channel, string label, int bits, bool randomized = false) => new()
        {
            PcmChannelId = channel, Label = label, Mode = StreamMode.ReceiverChannelInfo, SyncPattern = "FE6B2840",
            BitsInMinorFrame = bits, Randomized = randomized,
        };

        /// <summary>The settings each recording was exported with, as the Qt case stated them.</summary>
        private static (string File, bool SwapBytes, StreamConfig[] Streams) Scenario(string name) => name switch
        {
            "prn" => ("prn_testfile, sample.ch10", true, new[]
            {
                Lock(13, "CH-01 PRN 15 5M", "334AABBF", 32767),
                Lock(15, "CH-03 PRN 11 1M", "A345CA5C", 2047),
            }),
            "step_cal" => ("step_cal, example.ch10", true, new[] { Receivers(26, "CH-16 AGC STEP CALS 800K", 800) }),
            "step_cal_lock" => ("step_cal, example.ch10", true, new[] { Lock(26, "CH-16 AGC STEP CALS 800K", "FE6B2840", 800) }),
            "trc" => ("agc_rnrz-l_trc_testfile.ch10", true, new[] { Receivers(13, "CH-01 2250.5MHZ AGC 800Kbps RNRZ-L", 8000, true) }),
            "trc_lock" => ("agc_rnrz-l_trc_testfile.ch10", true, new[] { Lock(13, "CH-01 2250.5MHZ AGC 800Kbps RNRZ-L", "FE6B2840", 8000, true) }),
            "rasa" => ("agc_nzl_rasa_testfile.ch10", true, new[]
            {
                Receivers(26, "CH-01", 800) with
                {
                    ReceiverParamsToml = Path.Combine(Repo.Root, "settings", "receiver_params", "RASA.toml"),
                    PolarityIndex = 1, SlopeIndex = 2, ScaleDdBPerV = 10.0, NumReceivers = 12, ReceiverChannels = 3,
                },
            }),
            // Safran's channel 13 is PRN-15 recorded in the other byte order (Legacy Chapter 10 on).
            "safran" => ("safran_testfile, sample.ch10", false, new[] { Lock(13, "PCM03", "334AABBF", 32767) }),
            _ => throw new ArgumentOutOfRangeException(nameof(name)),
        };

        private static PlotViewModel Processed(string name)
        {
            (string file, bool swap, StreamConfig[] streams) = Scenario(name);
            using RunOutcome outcome = StreamsRun.Run(new RunRequest(Repo.Recording(file), 1, streams) { AppRoot = Repo.Root, SwapBytes = swap }, null);
            Assert.Equal(streams.Length, outcome.Results.Count);
            var plot = new PlotViewModel();
            foreach (StreamResult result in outcome.Results) plot.AddStream(result);
            return plot;
        }

        private static string TempPath(string suffix) => Path.Combine(Path.GetTempPath(), "tmdq-export-" + Guid.NewGuid().ToString("N") + suffix);

        private static string Sha256(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

        [Theory]
        [MemberData(nameof(QtExports))]
        public void EveryRecordingExportsTheQtApplicationsCsvByteForByte(string scenario, int length, string sha256)
        {
            PlotViewModel plot = Processed(scenario);
            string path = TempPath(".csv");
            try
            {
                Assert.True(plot.ExportCsv(path));
                byte[] bytes = File.ReadAllBytes(path);
                Assert.Equal(length, bytes.Length);
                Assert.Equal(sha256, Sha256(bytes));
            }
            finally
            {
                TempFiles.Delete(path);
            }
        }

        // ---- the plot as an image -----------------------------------------------------------------------------------------

        private static readonly Color LineColour = Color.FromRgb(0, 150, 0);

        /// <summary>A plot whose one series is a flat line low on the chart, in a colour nothing else is drawn in, so its
        /// colour anywhere else is the legend's swatch.</summary>
        private static PlotViewModel LowLine()
        {
            var plot = new PlotViewModel();
            plot.SetSeries(new[] { PlotFixtures.Snr(0, "line", 40, 0, 1, 0, new[] { 0.0, 2.0 }, new[] { 10.0, 10.0 }) });
            plot.RecolorSeries(0, LineColour);
            plot.SetRightMinOverride(0);
            plot.SetRightMaxOverride(100);
            return plot;
        }

        private static (PlotView View, Window Window) Show(PlotViewModel plot)
        {
            var view = new PlotView { Width = 600, Height = 400, Plot = plot };
            var window = new Window { Content = view, SizeToContent = SizeToContent.WidthAndHeight };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            return (view, window);
        }

        /// <summary>Pixels of <paramref name="colour"/> in <paramref name="area"/>, as <see cref="Frames.Near"/> counts them: an
        /// antialiased line is its colour blended with white, so it is the direction away from white that is compared.</summary>
        private static int Near(SKBitmap image, Rect area, Color colour)
        {
            double cr = 255 - colour.R, cg = 255 - colour.G, cb = 255 - colour.B;
            double cn = Math.Sqrt(cr * cr + cg * cg + cb * cb);
            int count = 0;
            for (int y = Math.Max(0, (int)area.Top); y < Math.Min(image.Height, (int)area.Bottom); y++)
                for (int x = Math.Max(0, (int)area.Left); x < Math.Min(image.Width, (int)area.Right); x++)
                {
                    SKColor p = image.GetPixel(x, y);
                    double r = 255 - p.Red, g = 255 - p.Green, b = 255 - p.Blue;
                    double n = Math.Sqrt(r * r + g * g + b * b);
                    if (p.Alpha > 200 && n > 40 && (r * cr + g * cg + b * cb) / (n * cn) > 0.995) count++;
                }
            return count;
        }

        private static Rect LegendBox(PlotView view) => new(view.Legend.Position, view.Legend.Bounds.Size);

        private static async Task<SKBitmap> PngOf(PlotView view)
        {
            string path = TempPath(".png");
            try
            {
                (string Path, ImageFormat Format)? written = await PlotImageExport.ExportAsync(view, path);
                Assert.Equal((path, ImageFormat.Png), written);
                return SKBitmap.Decode(path);
            }
            finally
            {
                TempFiles.Delete(path);
            }
        }

        [AvaloniaFact]
        public async Task APngIsTheChartAtItsSizeWithTheLegendWhereItIs()
        {
            PlotViewModel plot = LowLine();
            (PlotView view, Window window) = Show(plot);
            using SKBitmap first = await PngOf(view);
            Assert.Equal((600, 400), (first.Width, first.Height));
            Rect topRight = LegendBox(view);
            Assert.True(Near(first, topRight, LineColour) > 20, "no swatch where the legend is");
            Assert.True(Near(first, new Rect(0, view.Chart.RightToPixel(10) - 3, 600, 6), LineColour) > 200, "no line");

            // Dragged, it is drawn where it was put, and nowhere else.
            Point grab = Frames.InWindow(view.Legend, window).Center;
            window.MouseDown(grab, MouseButton.Left, RawInputModifiers.None);
            window.MouseMove(grab + new Point(-300, 120), RawInputModifiers.LeftMouseButton);
            window.MouseUp(grab + new Point(-300, 120), MouseButton.Left, RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            using SKBitmap moved = await PngOf(view);
            Assert.True(Near(moved, LegendBox(view), LineColour) > 20, "no swatch where the legend was dragged");
            Assert.Equal(0, Near(moved, topRight, LineColour));

            // Hidden, it is not drawn.
            plot.LegendVisible = false;
            Dispatcher.UIThread.RunJobs();
            using SKBitmap hidden = await PngOf(view);
            Assert.Equal(0, Near(hidden, LegendBox(view), LineColour));
            window.Close();
        }

        [AvaloniaFact]
        public async Task AnImageShowsOnlyWhatTheChartShows()
        {
            // A step from 10 to 90 halfway along, in green, and a flat red series at 50, hidden.
            Color red = Color.FromRgb(200, 0, 0);
            var plot = new PlotViewModel();
            plot.SetSeries(new[]
            {
                PlotFixtures.Snr(0, "step", 40, 0, 1, 0, new[] { 0.0, 0.99, 1.01, 2.0 }, new[] { 10.0, 10.0, 90.0, 90.0 }),
                PlotFixtures.Snr(1, "hidden", 40, 0, 2, 0, new[] { 0.0, 2.0 }, new[] { 50.0, 50.0 }),
            });
            plot.RecolorSeries(0, LineColour);
            plot.RecolorSeries(1, red);
            plot.SetRightMinOverride(0);
            plot.SetRightMaxOverride(100);
            plot.SetSeriesVisible(1, false);
            plot.LegendVisible = false;
            (PlotView view, Window window) = Show(plot);
            Rect Row(double value) => new(0, view.Chart.RightToPixel(value) - 3, 600, 6);

            using SKBitmap whole = await PngOf(view);
            Assert.True(Near(whole, Row(10), LineColour) > 100);
            Assert.True(Near(whole, Row(90), LineColour) > 100);
            Assert.Equal(0, Near(whole, new Rect(0, 0, 600, 400), red));   // hidden, so not in the image

            // Zoomed to the first half, the image is that half: the step's top is off the chart, as on screen.
            plot.SetXViewByUser(0.0, 0.9);
            Dispatcher.UIThread.RunJobs();
            using SKBitmap zoomed = await PngOf(view);
            Assert.True(Near(zoomed, Row(10), LineColour) > 100);
            Assert.Equal(0, Near(zoomed, Row(90), LineColour));

            // Shown again, the hidden series is drawn.
            plot.ResetXRange();
            plot.SetSeriesVisible(1, true);
            Dispatcher.UIThread.RunJobs();
            using SKBitmap both = await PngOf(view);
            Assert.True(Near(both, Row(50), red) > 100);
            window.Close();
        }

        [AvaloniaFact]
        public async Task TheCursorsCrosshairIsNotExported()
        {
            (PlotView view, Window window) = Show(LowLine());
            using SKBitmap before = await PngOf(view);
            window.MouseMove(Frames.InWindow(view, window).TopLeft + new Point(200, 150), RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.True(view.Chart.CrosshairVisible);
            using SKBitmap during = await PngOf(view);
            Assert.Equal(before.Bytes, during.Bytes);
            window.Close();
        }

        [AvaloniaFact]
        public async Task AnSvgAndAPdfAreTheChartsSizeAndAnyOtherNameIsAPdf()
        {
            PlotViewModel plot = LowLine();
            plot.Title = "Pass 3";
            (PlotView view, Window window) = Show(plot);
            string svg = TempPath(".svg"), pdf = TempPath(".PDF"), other = TempPath(".bogus");
            try
            {
                Assert.Equal((svg, ImageFormat.Svg), await PlotImageExport.ExportAsync(view, svg));
                XElement root = XDocument.Load(svg).Root!;
                Assert.Equal("svg", root.Name.LocalName);
                Assert.Equal(("600", "400"), ((string?)root.Attribute("width"), (string?)root.Attribute("height")));

                Assert.Equal((pdf, ImageFormat.Pdf), await PlotImageExport.ExportAsync(view, pdf));
                string text = Encoding.Latin1.GetString(File.ReadAllBytes(pdf));
                Assert.StartsWith("%PDF-", text);
                Assert.Contains("/MediaBox [0 0 600 400]", text);   // the chart's size, in points
                Assert.Contains("/Title (Pass 3)", text);            // the plot's title, as the document's
                Assert.Contains("/Creator (tmDataQualityAnalyzer)", text);

                Assert.Equal((other + ".pdf", ImageFormat.Pdf), await PlotImageExport.ExportAsync(view, other));
                Assert.StartsWith("%PDF-", Encoding.Latin1.GetString(File.ReadAllBytes(other + ".pdf")));
                Assert.False(File.Exists(other));
            }
            finally
            {
                foreach (string f in new[] { svg, pdf, other + ".pdf" }) TempFiles.Delete(f);
                window.Close();
            }
        }

        [AvaloniaFact]
        public async Task AnImageThatCannotBeWrittenIsAFailure()
        {
            (PlotView view, Window window) = Show(LowLine());
            Assert.Null(await PlotImageExport.ExportAsync(view, Path.Combine(TempPath(""), "no-such-folder", "plot.png")));
            window.Close();
        }

        [Theory]
        [InlineData("a.png", "a.png", ImageFormat.Png)]        [InlineData("a.PNG", "a.PNG", ImageFormat.Png)]
        [InlineData("a.Svg", "a.Svg", ImageFormat.Svg)]
        [InlineData("a.pdf", "a.pdf", ImageFormat.Pdf)]
        [InlineData("a.Pdf", "a.Pdf", ImageFormat.Pdf)]
        [InlineData("a.bogus", "a.bogus.pdf", ImageFormat.Pdf)]
        [InlineData("a", "a.pdf", ImageFormat.Pdf)]
        [InlineData("a.png.txt", "a.png.txt.pdf", ImageFormat.Pdf)]
        public void AnImagesNameDecidesItsFormat(string path, string file, ImageFormat format) =>
            Assert.Equal((file, format), ImageFiles.Resolve(path));

        // ---- the plot's CSV, both ways ------------------------------------------------------------------------------------

        private static string WriteCsv(string text)
        {
            string path = TempPath(".csv");
            File.WriteAllText(path, text, new UTF8Encoding(false));
            return path;
        }

        private static string HeaderOf(string path) => File.ReadAllLines(path)[0];

        [Fact]
        public void AnExportHoldsEverySeriesShownOrNotAndNothingToWriteWritesNoFile()
        {
            var plot = new PlotViewModel();
            PlotSeries missed = PlotFixtures.Missed(1, "Ch32", 32, 0, X2, new[] { 0.0, 1.0 });
            missed.Visible = false;
            plot.SetSeries(new[] { PlotFixtures.Lock(0, "Ch32", 32, 0, X2, new[] { 90.0, 95.0 }), missed });
            string path = TempPath(".csv"), none = TempPath(".csv");
            try
            {
                Assert.True(plot.ExportCsv(path));
                Assert.Equal("Time (DOY:HH:MM:SS.mmm),Ch32 Lock (%),Ch32 Accumulated Missed Frames", HeaderOf(path));
                byte[] everything = File.ReadAllBytes(path);

                // The CSV is the data, not the view (US6.1): the other metric in view, every series hidden, zoomed in,
                // it is the same file.
                plot.SetLockAxisView(LockAxisView.MissedFrames);
                plot.SetSeriesVisible(0, false);
                plot.SetSeriesVisible(1, false);
                plot.SetXViewByUser(0.2, 0.4);
                Assert.True(plot.ExportCsv(path));
                Assert.Equal(everything, File.ReadAllBytes(path));

                Assert.False(new PlotViewModel().ExportCsv(none));
                Assert.False(File.Exists(none));
                Assert.False(plot.ExportCsv(Path.Combine(TempPath(""), "no-such-folder", "x.csv")));
            }
            finally
            {
                TempFiles.Delete(path);
            }
        }

        private static readonly double[] X2 = { 0.0, 1.0 };

        [Fact]
        public void ALaterSourcesColumnsAreQualifiedAndASourceCanBeExportedAlone()
        {
            var plot = new PlotViewModel();
            plot.SetSeries(new[]
            {
                PlotFixtures.Make(0, "Ch5", MetricType.FrameSyncLock, 5, 0, X2, X2),
                PlotFixtures.Make(1, "Ch5", MetricType.FrameSyncLock, 5, 1, X2, X2, source: 2),
            });
            string all = TempPath(".csv"), one = TempPath(".csv"), missing = TempPath(".csv");
            try
            {
                Assert.True(plot.ExportCsv(all));
                Assert.Equal("Time (DOY:HH:MM:SS.mmm),Ch5 Lock (%),S2| Ch5 Lock (%)", HeaderOf(all));
                Assert.True(plot.ExportCsv(one, 2));
                Assert.Equal("Time (DOY:HH:MM:SS.mmm),S2| Ch5 Lock (%)", HeaderOf(one));
                Assert.False(plot.ExportCsv(missing, 7));
                Assert.False(File.Exists(missing));
                Assert.True(plot.ExportCsv(one, 0));   // the first source is a source like any other
                Assert.Equal("Time (DOY:HH:MM:SS.mmm),Ch5 Lock (%)", HeaderOf(one));

                // Read back, each series keeps its source, so it is written qualified again.
                var imported = new PlotViewModel();
                Assert.True(imported.LoadCsv(all));
                Assert.Equal(new[] { 0, 2 }, imported.Series.Select(s => s.SourceId));
                Assert.True(imported.ExportCsv(one));
                Assert.Equal(File.ReadAllBytes(all), File.ReadAllBytes(one));
            }
            finally
            {
                TempFiles.Delete(all);
                TempFiles.Delete(one);
            }
        }

        [Theory]
        [MemberData(nameof(QtExports))]
        public void EveryExportReadsBackAsItsSeriesAndExportsAgainByteForByte(string scenario, int length, string sha256)
        {
            _ = length;
            _ = sha256;
            PlotViewModel processed = Processed(scenario);
            string first = TempPath(".csv"), second = TempPath(".csv");
            try
            {
                Assert.True(processed.ExportCsv(first));
                var imported = new PlotViewModel();
                Assert.True(imported.LoadCsv(first));
                Assert.Equal(processed.Series.Select(s => (s.Metric, s.Name, s.Visible, s.X.Length)),
                             imported.Series.Select(s => (s.Metric, s.Name, s.Visible, s.X.Length)));
                Assert.Equal(processed.XMax, imported.XMax, 3);
                Assert.Equal(processed.FormatTime(0), imported.FormatTime(0));
                Assert.True(imported.ExportCsv(second));
                Assert.Equal(File.ReadAllBytes(first), File.ReadAllBytes(second));
            }
            finally
            {
                TempFiles.Delete(first);
                TempFiles.Delete(second);
            }
        }

        private const string TwoStreams =
            "Time (DOY:HH:MM:SS.mmm),A Lock (%),A Accumulated Missed Frames,B Lock (%),40 - X L_RCVR1,40 - X R_RCVR1\r\n" +
            "001:23:59:59.000,90.000000,0.000000,80.000000,10.000000,20.000000\r\n" +
            "002:00:00:01.000,95.000000,1.000000,85.000000,11.000000,21.000000\r\n";

        [Fact]
        public void AnImportedPlotIsShownAsARunsWouldBe()
        {
            string path = WriteCsv(TwoStreams);
            try
            {
                var plot = new PlotViewModel();
                plot.SetSeries(new[] { PlotFixtures.Lock(0, "earlier", 1, 0, X2, X2) });
                plot.PushXZoom(0.2, 0.4);
                plot.ReadoutSeriesId = 0;
                int seriesChanged = 0, hasData = 0;
                plot.SeriesChanged += (_, _) => seriesChanged++;
                plot.PropertyChanged += (_, e) => hasData += e.PropertyName == nameof(PlotViewModel.HasData) ? 1 : 0;
                Assert.True(plot.LoadCsv(path));
                Assert.Equal(new[] { "A", "A", "B", "40 - X L_RCVR1", "40 - X R_RCVR1" }, plot.Series.Select(s => s.Name));
                // Shown at once: the chart, the legend and Export are told; the pin to a series now gone is dropped.
                Assert.Equal((1, 1), (seriesChanged, hasData));
                Assert.Null(plot.ReadoutSeriesId);
                Assert.All(plot.Series, s => Assert.True(s.SortedX));
                Assert.Equal((10.0, 25.0), (plot.RightMin, plot.RightMax));   // the SNR shown, to 5 dB
                // The clock is the file's: the second row is two seconds on, across midnight.
                Assert.Equal(new[] { 0.0, 2.0 }, plot.Series[0].X);
                Assert.Equal("001:23:59:59", plot.FormatTime(0));
                Assert.Equal("002:00:00:01", plot.FormatTime(2));
                Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
                Assert.False(plot.CanZoomBack);
                // Each Frame Sync Lock stream has its own order, shared by its two metrics, so its own colour.
                Assert.Equal(plot.Series[0].StreamOrder, plot.Series[1].StreamOrder);
                Assert.NotEqual(plot.Series[0].StreamOrder, plot.Series[2].StreamOrder);
                Assert.Equal(plot.Series[0].Color, plot.Series[1].Color);
                Assert.NotEqual(plot.Series[0].Color, plot.Series[2].Color);
                Assert.Equal(40, plot.Series[3].StreamOrder);
                Assert.Equal((1, 0), (plot.Series[3].ReceiverIndex, plot.Series[3].ChannelIndex));
                Assert.Equal((1, 1), (plot.Series[4].ReceiverIndex, plot.Series[4].ChannelIndex));
                // The metric of the view in use is shown, the other hidden; SNR always.
                Assert.Equal(new[] { true, false, true, true, true }, plot.Series.Select(s => s.Visible));
                Assert.True(plot.CanSwitchViewMode);
                Assert.Equal(10.0, plot.Series[3].YMin);

                plot.SetLockAxisView(LockAxisView.MissedFrames);
                Assert.True(plot.LoadCsv(path));
                Assert.Equal(new[] { false, true, false, true, true }, plot.Series.Select(s => s.Visible));
            }
            finally
            {
                TempFiles.Delete(path);
            }
        }

        public static IEnumerable<object[]> NotExports() => new[]
        {
            new object[] { "Day,Time,A Lock (%)\r\n001,00:00:00.000,90\r\n" },   // the legacy layout
            new object[] { "" },
            new object[] { "Time (DOY:HH:MM:SS.mmm),A Lock (%)\r\n" },           // a header and no data
            new object[] { "Time (DOY:HH:MM:SS.mmm),A Lock (%)\r\nnot a time,90\r\n" },
        };

        [Theory]
        [MemberData(nameof(NotExports))]
        public void AFileThatIsNotAnExportedCsvChangesNothing(string text)
        {
            string path = WriteCsv(text);
            try
            {
                var plot = new PlotViewModel();
                plot.SetSeries(new[] { PlotFixtures.Lock(0, "kept", 1, 0, X2, X2) });
                Assert.False(plot.LoadCsv(path));
                Assert.Equal("kept", Assert.Single(plot.Series).Name);
            }
            finally
            {
                TempFiles.Delete(path);
            }
            Assert.False(new PlotViewModel().LoadCsv(TempPath(".csv")));   // a missing file
        }

        [Fact]
        public void MalformedRowsAreSkippedWithAWarningThatCountsThem()
        {
            string path = WriteCsv("Time (DOY:HH:MM:SS.mmm),A Lock (%),B Lock (%)\r\n" +
                                   "001:00:00:00.000,1,2\r\n001:00:00:01.000\r\n001:00:00:02.000,3,4\r\nbad,5,6\r\n001:00:00:03.000,7,8\r\n");
            try
            {
                var plot = new PlotViewModel();
                var warnings = new List<string>();
                plot.Warning += (_, w) => warnings.Add(w);
                Assert.True(plot.LoadCsv(path));
                Assert.Equal(new[] { "Skipped 2 malformed row(s) while loading the CSV." }, warnings);
                Assert.Equal(3, plot.Series[0].X.Length);

                warnings.Clear();
                File.WriteAllText(path, TwoStreams);
                Assert.True(plot.LoadCsv(path));
                Assert.Empty(warnings);
            }
            finally
            {
                TempFiles.Delete(path);
            }
        }

        [Fact]
        public async Task ACsvIsReadOffTheUiThreadWithLoadingShownAndASecondRequestDropped()
        {
            string good = WriteCsv(TwoStreams), bad = WriteCsv("Day,Time\r\n");
            try
            {
                var plot = new PlotViewModel();
                var loading = new List<bool>();
                plot.PropertyChanged += (_, e) =>
                {
                    if (e.PropertyName == nameof(PlotViewModel.IsLoading)) loading.Add(plot.IsLoading);
                };
                Task<CsvLoad> first = plot.LoadCsvAsync(good);
                Assert.True(plot.IsLoading);
                Assert.Equal(CsvLoad.Dropped, await plot.LoadCsvAsync(good));
                Assert.Equal(CsvLoad.Loaded, await first);
                Assert.False(plot.IsLoading);
                Assert.Equal(new[] { true, false }, loading);
                Assert.Equal(5, plot.Series.Count);

                Assert.Equal(CsvLoad.Failed, await plot.LoadCsvAsync(bad));
                Assert.False(plot.IsLoading);
                Assert.Equal(5, plot.Series.Count);
            }
            finally
            {
                TempFiles.Delete(good);
                TempFiles.Delete(bad);
            }
        }

        // ---- the Export dialog's state ------------------------------------------------------------------------------------

        private sealed class FakeFiles : IDataFilePicker
        {
            public readonly List<(ExportKind Kind, string Current)> Saves = new();
            public string? Folder;
            public string? Answer;

            public Task<string?> OpenCsvAsync(string? folder)
            {
                Folder = folder;
                return Task.FromResult(Answer);
            }

            public Task<string?> SaveExportAsync(ExportKind kind, string current)
            {
                Saves.Add((kind, current));
                return Task.FromResult(Answer);
            }
        }

        [Fact]
        public void TheDialogStartsWithTheImageTickedAndEachRowFollowsItsBox()
        {
            var files = new FakeFiles();
            var vm = new ExportViewModel("a.png", "a.csv", "a_log.txt", files);
            Assert.Equal("Export Data", ExportViewModel.Title);
            Assert.Equal((true, false, false), (vm.ExportImage, vm.ExportData, vm.ExportLog));
            Assert.Equal(("a.png", "a.csv", "a_log.txt"), (vm.ImagePath, vm.DataPath, vm.LogPath));
            Assert.Equal((true, false, false), (vm.BrowseImageCommand.CanExecute(null), vm.BrowseDataCommand.CanExecute(null), vm.BrowseLogCommand.CanExecute(null)));
            Assert.True(vm.ExportCommand.CanExecute(null));

            vm.ExportImage = false;
            vm.ExportLog = true;
            Assert.Equal((false, false, true), (vm.BrowseImageCommand.CanExecute(null), vm.BrowseDataCommand.CanExecute(null), vm.BrowseLogCommand.CanExecute(null)));
            vm.ExportData = true;
            Assert.True(vm.BrowseDataCommand.CanExecute(null));
            Assert.False(new ExportViewModel("a", "b", "c").BrowseImageCommand.CanExecute(null));   // no file dialogs
        }

        [Fact]
        public void ExportWaitsForATickedRowAndAPathForEach()
        {
            var vm = new ExportViewModel("a.png", "a.csv", "a_log.txt");
            int changes = 0;
            vm.ExportCommand.CanExecuteChanged += (_, _) => changes++;
            vm.ImagePath = "";
            Assert.False(vm.CanExport);
            Assert.False(vm.ExportCommand.CanExecute(null));
            Assert.True(changes > 0);
            vm.ExportImage = false;
            Assert.False(vm.CanExport);   // nothing ticked
            vm.ExportData = true;
            Assert.True(vm.CanExport);   // the data alone
            vm.DataPath = "";
            Assert.False(vm.CanExport);
            vm.ExportData = false;
            vm.ExportLog = true;
            Assert.True(vm.CanExport);   // the log alone
            vm.LogPath = "";
            Assert.False(vm.CanExport);
            vm.LogPath = "x.txt";
            vm.ExportImage = true;
            Assert.False(vm.CanExport);  // the image ticked with no path
        }

        [Fact]
        public async Task BrowseStartsAtTheRowsPathAndANameWithoutASuffixGetsTheRowsOwn()
        {
            var files = new FakeFiles();
            var vm = new ExportViewModel(@"C:\f\a.png", @"C:\f\a.csv", @"C:\f\a_log.txt", files) { ExportData = true, ExportLog = true };
            files.Answer = @"C:\g\picture";
            await vm.BrowseImageCommand.ExecuteAsync(null);
            files.Answer = @"C:\g\numbers";
            await vm.BrowseDataCommand.ExecuteAsync(null);
            files.Answer = @"C:\g\words";
            await vm.BrowseLogCommand.ExecuteAsync(null);
            Assert.Equal(new[] { (ExportKind.Image, @"C:\f\a.png"), (ExportKind.Data, @"C:\f\a.csv"), (ExportKind.Log, @"C:\f\a_log.txt") }, files.Saves);
            Assert.Equal((@"C:\g\picture.png", @"C:\g\numbers.csv", @"C:\g\words.txt"), (vm.ImagePath, vm.DataPath, vm.LogPath));

            files.Answer = @"C:\g\plot.svg";
            await vm.BrowseImageCommand.ExecuteAsync(null);
            Assert.Equal(@"C:\g\plot.svg", vm.ImagePath);   // a suffix chosen is kept
            files.Answer = null;
            await vm.BrowseImageCommand.ExecuteAsync(null);
            Assert.Equal(@"C:\g\plot.svg", vm.ImagePath);   // cancelled
        }

        [Fact]
        public void TheDefaultsAreThePlotsTitleInTheFolderGivenWithItsUnsafeCharactersReplaced()
        {
            Assert.Equal((Path.Combine("f", "Framesync_SNR Plot.png"), Path.Combine("f", "Framesync_SNR Plot.csv"), Path.Combine("f", "Framesync_SNR Plot_log.txt")),
                         ExportViewModel.DefaultPaths("f", PlotViewModel.DefaultTitle));
            Assert.Equal(Path.Combine("f", "a_b_c_d_e_f_g_h_i_j.png"), ExportViewModel.DefaultPaths("f", "a\\b/c:d*e?f\"g<h>i|j").Image);
            Assert.Equal(Path.Combine("f", "plot_log.txt"), ExportViewModel.DefaultPaths("f", "").Log);
        }

        [Fact]
        public void ExportAcceptsAndCancelDoesNot()
        {
            var vm = new ExportViewModel("a.png", "a.csv", "a_log.txt");
            int closed = 0;
            vm.CloseRequested += (_, _) => closed++;
            vm.CancelCommand.Execute(null);
            Assert.Equal((1, false), (closed, vm.Accepted));
            vm.ExportCommand.Execute(null);
            Assert.Equal((2, true), (closed, vm.Accepted));
        }

        // ---- the main window: Import CSV and Export ------------------------------------------------------------------------

        private sealed class FakeExportDialog : IExportDialog
        {
            public ExportViewModel? Shown;
            public Action<ExportViewModel>? Answer;

            public Task ShowAsync(ExportViewModel vm)
            {
                Shown = vm;
                Answer?.Invoke(vm);
                return Task.CompletedTask;
            }
        }

        private sealed class FakeImages : IPlotImageExporter
        {
            public readonly List<string> Asked = new();
            public bool Fail;

            public Task<(string Path, ImageFormat Format)?> ExportAsync(string path)
            {
                Asked.Add(path);
                (string file, ImageFormat format) = ImageFiles.Resolve(path);
                if (Fail) return Task.FromResult<(string, ImageFormat)?>(null);
                File.WriteAllText(file, "image");
                return Task.FromResult<(string, ImageFormat)?>((file, format));
            }
        }

        private static string[] LinesOf(MainWindowViewModel vm, int skip) => vm.Log.Entries.Skip(skip).Select(e => e.Level + " " + e.Text).ToArray();

        [Fact]
        public async Task ImportCsvShowsTheFileTitledByItsNameAndSaysSo()
        {
            string path = WriteCsv(TwoStreams);
            try
            {
                var vm = new MainWindowViewModel();
                int before = vm.Log.Entries.Count;
                await vm.ImportCsvAsync(path);
                string name = Path.GetFileName(path);
                Assert.Equal(new[] { "Info Importing CSV: " + name, "Success Imported CSV: " + name }, LinesOf(vm, before));
                Assert.Equal(5, vm.Plot.Series.Count);
                Assert.Equal(MainWindowViewModel.BaseName(name), vm.Plot.Title);
                Assert.Equal("Imported CSV: " + name, vm.Status);
                Assert.Equal(Path.GetDirectoryName(path), vm.LastFolder);
                Assert.False(vm.IsBusy);
            }
            finally
            {
                TempFiles.Delete(path);
            }
        }

        [Fact]
        public async Task ACsvThatIsNotAnExportIsAnErrorAndLeavesThePlotEmpty()
        {
            string path = WriteCsv("Day,Time,A Lock (%)\r\n001,00:00:00.000,90\r\n");
            try
            {
                var vm = new MainWindowViewModel();
                vm.Plot.SetSeries(new[] { PlotFixtures.Lock(0, "earlier", 1, 0, X2, X2) });
                vm.Plot.Title = "earlier";
                int before = vm.Log.Entries.Count;
                await vm.ImportCsvAsync(path);
                string name = Path.GetFileName(path);
                string error = "ERROR: '" + name + "' is not a recognized exported CSV (expected a 'Time (DOY:HH:MM:SS.mmm)' column with data rows). Nothing was imported.";
                Assert.Equal(new[] { "Info Importing CSV: " + name, "Error " + error }, LinesOf(vm, before));
                Assert.Equal(error, vm.Status);
                Assert.False(vm.Plot.HasData);   // cleared before the file was read, as Qt clears it
                Assert.Equal(PlotViewModel.DefaultTitle, vm.Plot.Title);
            }
            finally
            {
                TempFiles.Delete(path);
            }
        }

        [Fact]
        public async Task ImportCsvAsksInTheLastFolderAndAnImportOfSkippedRowsWarnsBeforeItSucceeds()
        {
            string path = WriteCsv("Time (DOY:HH:MM:SS.mmm),A Lock (%)\r\n001:00:00:00.000,1\r\nbad\r\n001:00:00:01.000,2\r\n");
            try
            {
                var files = new FakeFiles { Answer = path };
                var vm = new MainWindowViewModel(files: files);
                Assert.True(vm.ImportCsvCommand.CanExecute(null));
                Assert.False(new MainWindowViewModel().ImportCsvCommand.CanExecute(null));   // no file dialog
                int before = vm.Log.Entries.Count;
                await vm.ImportCsvCommand.ExecuteAsync(null);
                Assert.Null(files.Folder);   // nothing opened yet
                Assert.Equal(new[]
                {
                    "Info Importing CSV: " + Path.GetFileName(path),
                    "Warning Skipped 1 malformed row(s) while loading the CSV.",
                    "Success Imported CSV: " + Path.GetFileName(path),
                }, LinesOf(vm, before));
                files.Answer = null;
                await vm.ImportCsvCommand.ExecuteAsync(null);
                Assert.Equal(Path.GetDirectoryName(path), files.Folder);
            }
            finally
            {
                TempFiles.Delete(path);
            }
        }

        private sealed class WaitingConfigurator : IStreamConfigurator
        {
            public readonly TaskCompletionSource<RunRequest?> Answer = new();

            public Task<RunRequest?> ConfigureAsync(string path, Ch10File file) => Answer.Task;
        }

        [Fact]
        public async Task WhileBusyNeitherImportNorExportStartsAndBothAreToldWhenItEnds()
        {
            string csv = WriteCsv(TwoStreams);
            try
            {
                // Configure Streams up over a plot that has data: Export must wait too, though there is data to export.
                var configurator = new WaitingConfigurator();
                var vm = new MainWindowViewModel(configurator: configurator, files: new FakeFiles(), exportDialog: new FakeExportDialog());
                vm.Plot.SetSeries(new[] { PlotFixtures.Lock(0, "earlier", 1, 0, X2, X2) });
                int import = 0, export = 0;
                vm.ImportCsvCommand.CanExecuteChanged += (_, _) => import++;
                vm.ExportCommand.CanExecuteChanged += (_, _) => export++;
                string recording = Repo.Recording("prn_testfile, sample.ch10");
                Task open = vm.LoadAsync(recording);
                while (!open.IsCompleted && vm.Log.Entries.All(e => !e.Text.StartsWith("  PCM channels", StringComparison.Ordinal))) await Task.Delay(10, TestContext.Current.CancellationToken);
                Assert.True(vm.IsBusy);
                Assert.Equal(Path.GetDirectoryName(recording), vm.LastFolder);   // where Export and Import CSV will start
                Assert.False(vm.ImportCsvCommand.CanExecute(null));
                Assert.False(vm.ExportCommand.CanExecute(null));
                Assert.True((import, export) is ( > 0, > 0), $"told {import} and {export} times");
                int lines = vm.Log.Entries.Count;
                await vm.ImportCsvAsync(csv);   // ignored while busy
                Assert.Equal(lines, vm.Log.Entries.Count);

                configurator.Answer.SetResult(null);
                await open;
                Assert.True(vm.ImportCsvCommand.CanExecute(null));
                Assert.True(vm.ExportCommand.CanExecute(null));
                Assert.True((import, export) is ( > 1, > 1), $"told {import} and {export} times");

                // A CSV being read keeps both waiting too.
                Task reading = vm.ImportCsvAsync(csv);
                Assert.True(vm.IsBusy);
                Assert.False(vm.ImportCsvCommand.CanExecute(null));
                await reading;
                Assert.True(vm.ImportCsvCommand.CanExecute(null));
            }
            finally
            {
                TempFiles.Delete(csv);
            }
        }

        [Fact]
        public async Task ExportIsOfferedWithDataAndProposesThePlotsTitleInTheLastFolder()
        {
            string csv = WriteCsv(TwoStreams);
            try
            {
                var dialog = new FakeExportDialog();
                var vm = new MainWindowViewModel(exportDialog: dialog, imageExporter: new FakeImages());
                Assert.False(vm.ExportCommand.CanExecute(null));
                int changes = 0;
                vm.ExportCommand.CanExecuteChanged += (_, _) => changes++;
                await vm.ImportCsvAsync(csv);
                Assert.True(vm.ExportCommand.CanExecute(null));
                Assert.True(changes > 0);
                Assert.False(new MainWindowViewModel().ExportCommand.CanExecute(null));
                var noDialog = new MainWindowViewModel();
                noDialog.Plot.SetSeries(new[] { PlotFixtures.Lock(0, "a", 1, 0, X2, X2) });
                Assert.False(noDialog.ExportCommand.CanExecute(null));   // data, but no dialog to ask with

                int before = vm.Log.Entries.Count;
                await vm.ExportCommand.ExecuteAsync(null);   // cancelled
                (string image, string data, string log) = ExportViewModel.DefaultPaths(Path.GetDirectoryName(csv)!, vm.Plot.Title);
                Assert.Equal((image, data, log), (dialog.Shown!.ImagePath, dialog.Shown.DataPath, dialog.Shown.LogPath));
                Assert.Equal(before, vm.Log.Entries.Count);

                var fresh = new MainWindowViewModel(exportDialog: dialog);
                fresh.Plot.SetSeries(new[] { PlotFixtures.Lock(0, "a", 1, 0, X2, X2) });
                await fresh.ExportCommand.ExecuteAsync(null);
                Assert.Equal(ExportViewModel.DefaultPaths(AppContext.BaseDirectory, PlotViewModel.DefaultTitle).Image, dialog.Shown!.ImagePath);
            }
            finally
            {
                TempFiles.Delete(csv);
            }
        }

        [Fact]
        public async Task ExportWritesWhatWasChosenInTheQtOrderAndTheLogSaysWhere()
        {
            string csv = WriteCsv(TwoStreams), folder = TempPath("");
            Directory.CreateDirectory(folder);
            try
            {
                var images = new FakeImages();
                var dialog = new FakeExportDialog
                {
                    Answer = vm =>
                    {
                        vm.ExportData = true;
                        vm.ExportLog = true;
                        vm.ExportCommand.Execute(null);
                    },
                };
                var vm = new MainWindowViewModel(exportDialog: dialog, imageExporter: images);
                await vm.ImportCsvAsync(csv);
                (string image, string data, string log) = ExportViewModel.DefaultPaths(folder, vm.Plot.Title);
                var choice = new ExportViewModel(image, data, log) { ExportData = true, ExportLog = true };
                int before = vm.Log.Entries.Count;
                await vm.ExportAsync(choice);
                Assert.Equal(new[]
                {
                    "Success Data exported to " + data,
                    "Success Plot exported as PNG to " + image,
                    "Success Log exported to " + log,
                }, LinesOf(vm, before));
                Assert.Equal(new[] { image }, images.Asked);

                // The data is the plot's own export.
                string again = TempPath(".csv");
                Assert.True(vm.Plot.ExportCsv(again));
                Assert.Equal(File.ReadAllBytes(again), File.ReadAllBytes(data));
                TempFiles.Delete(again);

                // The log is the sidebar's lines, CR LF, UTF-8 with no byte-order mark, a line end last; it says where the
                // data and the image went, since it is written after them.
                byte[] bytes = File.ReadAllBytes(log);
                Assert.NotEqual(0xEF, bytes[0]);
                string text = Encoding.UTF8.GetString(bytes);
                string[] lines = vm.Log.Entries.Take(vm.Log.Entries.Count - 1).Select(e => e.Line).ToArray();
                Assert.Equal(string.Join("\r\n", lines) + "\r\n", text);
                Assert.DoesNotContain("\n", text.Replace("\r\n", ""));
                Assert.Contains("Plot exported as PNG to " + image, text);
            }
            finally
            {
                TempFiles.Delete(csv);
                TempFiles.DeleteDirectory(folder, recursive: true);
            }
        }

        [Fact]
        public async Task AFailedExportSaysSoAndTheRestStillRun()
        {
            string csv = WriteCsv(TwoStreams), folder = TempPath("");
            Directory.CreateDirectory(folder);
            try
            {
                var vm = new MainWindowViewModel(imageExporter: new FakeImages { Fail = true });
                await vm.ImportCsvAsync(csv);
                string nowhere = Path.Combine(folder, "missing", "x");
                var choice = new ExportViewModel(nowhere + ".bogus", nowhere + ".csv", Path.Combine(folder, "log.txt")) { ExportData = true, ExportLog = true };
                int before = vm.Log.Entries.Count;
                await vm.ExportAsync(choice);
                Assert.Equal(new[]
                {
                    "Error Error: Failed to export data to " + nowhere + ".csv",
                    "Error Error: Failed to export plot to " + nowhere + ".bogus.pdf",
                    "Success Log exported to " + Path.Combine(folder, "log.txt"),
                }, LinesOf(vm, before));

                choice = new ExportViewModel("a.png", "a.csv", nowhere + ".txt") { ExportImage = false, ExportLog = true };
                before = vm.Log.Entries.Count;
                await vm.ExportAsync(choice);
                Assert.Equal(new[] { "Error Error: Failed to export log to " + nowhere + ".txt" }, LinesOf(vm, before));

                // With nothing to draw the image, it is a failure, not a success.
                var noImages = new MainWindowViewModel();
                await noImages.ImportCsvAsync(csv);
                before = noImages.Log.Entries.Count;
                await noImages.ExportAsync(new ExportViewModel(Path.Combine(folder, "n.png"), "n.csv", "n.txt"));
                Assert.Equal(new[] { "Error Error: Failed to export plot to " + Path.Combine(folder, "n.png") }, LinesOf(noImages, before));

                // Without data, nothing is written.
                var empty = new MainWindowViewModel(imageExporter: new FakeImages());
                before = empty.Log.Entries.Count;
                await empty.ExportAsync(new ExportViewModel(Path.Combine(folder, "e.png"), "e.csv", "e.txt"));
                Assert.Equal(before, empty.Log.Entries.Count);
                Assert.False(File.Exists(Path.Combine(folder, "e.png")));
            }
            finally
            {
                TempFiles.Delete(csv);
                TempFiles.DeleteDirectory(folder, recursive: true);
            }
        }

        [Fact]
        public async Task TheExportDialogsAnswerIsCarriedOut()
        {
            string csv = WriteCsv(TwoStreams);
            try
            {
                var images = new FakeImages();
                var dialog = new FakeExportDialog { Answer = vm => vm.ExportCommand.Execute(null) };
                var vm = new MainWindowViewModel(exportDialog: dialog, imageExporter: images);
                await vm.ImportCsvAsync(csv);
                File.SetLastWriteTimeUtc(csv, new DateTime(2020, 1, 1, 0, 0, 0, DateTimeKind.Utc));
                int before = vm.Log.Entries.Count;
                await vm.ExportCommand.ExecuteAsync(null);
                // The dialog as it opens: the image alone. Only the image is written (the data's default name is the
                // imported file's own, which is left as it was).
                Assert.Equal(new[] { dialog.Shown!.ImagePath }, images.Asked);
                Assert.Equal(new[] { "Success Plot exported as PNG to " + dialog.Shown.ImagePath }, LinesOf(vm, before));
                Assert.Equal(csv, dialog.Shown.DataPath);
                Assert.Equal(2020, File.GetLastWriteTimeUtc(csv).Year);
                Assert.False(File.Exists(dialog.Shown.LogPath));
                TempFiles.Delete(ImageFiles.Resolve(dialog.Shown.ImagePath).Path);
            }
            finally
            {
                TempFiles.Delete(csv);
            }
        }

        // ---- the windows ---------------------------------------------------------------------------------------------------

        [AvaloniaFact]
        public void TheExportWindowIsLaidOutAsQtsAndEachRowFollowsItsBox()
        {
            var vm = new ExportViewModel("a.png", "a.csv", "a_log.txt", new FakeFiles());
            var window = new ExportWindow { DataContext = vm };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("Export Data", window.Title);
            Assert.Equal(500, window.MinWidth);
            Assert.Equal(new[] { "Export Plot Image", "Export Data (CSV)", "Export Log (Text)" },
                         window.GetLogicalDescendants().OfType<HeaderedContentControl>().Select(h => (string)h.Header!));
            Assert.Equal(new[] { "Export Image", "Export Data", "Export Log" },
                         window.GetLogicalDescendants().OfType<CheckBox>().Select(c => (string)c.Content!));
            TextBox image = window.FindControl<TextBox>("ImagePathBox")!, data = window.FindControl<TextBox>("DataPathBox")!,
                    log = window.FindControl<TextBox>("LogPathBox")!;
            Button export = window.FindControl<Button>("ExportButton")!;
            Assert.Equal(("a.png", "a.csv", "a_log.txt"), (image.Text, data.Text, log.Text));
            Assert.Equal((true, false, false), (image.IsEnabled, data.IsEnabled, log.IsEnabled));
            Assert.Equal((true, false, false), (window.FindControl<Button>("ImageBrowse")!.IsEffectivelyEnabled,
                                                window.FindControl<Button>("DataBrowse")!.IsEffectivelyEnabled,
                                                window.FindControl<Button>("LogBrowse")!.IsEffectivelyEnabled));
            Assert.True(export.IsDefault);
            Assert.True(window.FindControl<Button>("CancelButton")!.IsCancel);
            Assert.True(export.IsEffectivelyEnabled);

            window.FindControl<CheckBox>("DataBox")!.IsChecked = true;
            window.FindControl<CheckBox>("ImageBox")!.IsChecked = false;
            Dispatcher.UIThread.RunJobs();
            Assert.Equal((false, true), (vm.ExportImage, vm.ExportData));
            Assert.Equal((false, true), (image.IsEnabled, data.IsEnabled));
            // Each Browse is its row's, and follows its box.
            Button imageBrowse = window.FindControl<Button>("ImageBrowse")!, dataBrowse = window.FindControl<Button>("DataBrowse")!;
            Assert.Equal((vm.BrowseImageCommand, vm.BrowseDataCommand, vm.BrowseLogCommand),
                         (imageBrowse.Command, dataBrowse.Command, window.FindControl<Button>("LogBrowse")!.Command));
            Assert.Equal((false, true), (imageBrowse.IsEffectivelyEnabled, dataBrowse.IsEffectivelyEnabled));
            data.Text = "";
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("", vm.DataPath);
            Assert.False(export.IsEffectivelyEnabled);
            window.Close();
        }

        [AvaloniaFact]
        public void TheExportDialogClosesOnExportAndOnCancel()
        {
            var owner = new Window { Width = 400, Height = 300 };
            owner.Show();
            var dialog = new WindowExportDialog(owner);
            var vm = new ExportViewModel("a.png", "a.csv", "a_log.txt");
            Task shown = dialog.ShowAsync(vm);
            Dispatcher.UIThread.RunJobs();
            var window = Assert.IsType<ExportWindow>(Assert.Single(owner.OwnedWindows));
            Assert.True(window.IsDialog);
            vm.ExportCommand.Execute(null);
            Dispatcher.UIThread.RunJobs();
            Assert.True(shown.IsCompleted, "the Export dialog is still open.");
            Assert.True(vm.Accepted);

            var cancelled = new ExportViewModel("a.png", "a.csv", "a_log.txt");
            shown = dialog.ShowAsync(cancelled);
            Dispatcher.UIThread.RunJobs();
            cancelled.CancelCommand.Execute(null);
            Dispatcher.UIThread.RunJobs();
            Assert.True(shown.IsCompleted, "the Export dialog is still open after Cancel.");
            Assert.False(cancelled.Accepted);
            owner.Close();
        }

        [AvaloniaFact]
        public void TheTopBarHasImportCsvAndExportBesideOpen()
        {
            var vm = new MainWindowViewModel(files: new FakeFiles(), exportDialog: new FakeExportDialog());
            var window = new MainWindow { DataContext = vm };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            Button open = window.FindControl<Button>("OpenButton")!, import = window.FindControl<Button>("ImportCsvButton")!,
                   export = window.FindControl<Button>("ExportButton")!;
            Assert.Same(open.Parent, import.Parent);
            Assert.Same(open.Parent, export.Parent);
            Assert.Equal(("Import CSV...", "Export..."), ((string)import.Content!, (string)export.Content!));
            Assert.Same(vm.ImportCsvCommand, import.Command);
            Assert.Same(vm.ExportCommand, export.Command);
            Assert.Equal("Import a previously exported CSV file", ToolTip.GetTip(import));
            Assert.Equal("Export plot data and images", ToolTip.GetTip(export));
            Assert.True(import.IsEffectivelyEnabled);
            Assert.False(export.IsEffectivelyEnabled);   // nothing to export yet
            vm.Plot.SetSeries(new[] { PlotFixtures.Lock(0, "a", 1, 0, X2, X2) });
            Dispatcher.UIThread.RunJobs();
            Assert.True(export.IsEffectivelyEnabled);
            window.Close();
        }

        [AvaloniaFact]
        public async Task LoadingIsShownOverTheChartWhileACsvIsRead()        {
            string path = WriteCsv(TwoStreams);
            try
            {
                var plot = new PlotViewModel();
                (PlotView view, Window window) = Show(plot);
                Assert.False(view.LoadingLabel.IsVisible);
                // Shown as the read starts; the read's end cannot reach the UI thread until its jobs run.
                Task<CsvLoad> load = plot.LoadCsvAsync(path);
                Assert.True(view.LoadingLabel.IsVisible);
                view.UpdateLayout();
                Assert.Equal("Loading...", ((TextBlock)view.LoadingLabel.Child!).Text);
                Assert.False(view.LoadingLabel.IsHitTestVisible);
                Rect label = Frames.InWindow(view.LoadingLabel, window), chart = Frames.InWindow(view, window);
                Assert.Equal(chart.Center.X, label.Center.X, 0);
                Assert.Equal(chart.Center.Y, label.Center.Y, 0);
                Assert.Equal(CsvLoad.Loaded, await load);
                Dispatcher.UIThread.RunJobs();
                Assert.False(view.LoadingLabel.IsVisible);
                window.Close();
            }
            finally
            {
                TempFiles.Delete(path);
            }
        }
    }
}
