using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Threading;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Streams;
using TmDq.App.Models.Templates;
using TmDq.App.ViewModels.Plot;
using TmDq.App.Views;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// Templates and batch (plan step 4.7, US1.1). The template's oracle is v2.14.2 (the Qt application): the three
    /// files under Templates/ were written by its own template writer (a throwaway case in its suite, since removed),
    /// so the app must read them as written and write their very bytes. The batch is v2.14.2's batch loop, its
    /// messages word for word, run over copies of the PRN sample.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class TemplateTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "tmdq-templates-" + Guid.NewGuid().ToString("N"));

        public TemplateTests()
        {
            NativeCore.EnsureLoaded();
            Directory.CreateDirectory(_dir);
        }

        public void Dispose() => TempFiles.DeleteDirectory(_dir, recursive: true);

        private static string Fixture(string name) => Path.Combine(Repo.Root, "tests", "TmDq.App.Tests", "Templates", name);

        private static string Prn => Repo.Recording("prn_testfile, sample.ch10");

        private static ProcessingTemplate Load(string path)
        {
            Assert.Equal(TemplateLoad.Loaded, TemplateFile.Read(path, out ProcessingTemplate? template, out _));
            return template!;
        }

        private static ProcessingTemplate Parse(string json)
        {
            Assert.Equal(TemplateLoad.Loaded, TemplateFile.Parse(json, out ProcessingTemplate? template, out _));
            return template!;
        }

        // ---- the file, against v2.14.2's ----------------------------------------------------------------------------------

        [Fact]
        public void TheV214TemplatesReadAsTheyWereWritten()
        {
            ProcessingTemplate prn = Load(Fixture("prn-sample.json"));
            Assert.Equal(("2.14.2", "prn_testfile, sample", 0, true, 1), (prn.AppVersion, prn.Name, prn.TimeChannelIndex, prn.SwapBytes, prn.SchemaVersion));
            Assert.Equal(new[] { 13, 14, 15, 16 }, prn.Entries.Select(e => e.Config.PcmChannelId));
            Assert.Equal(new[] { true, false, true, false }, prn.Entries.Select(e => e.Config.Process));
            StreamSetup first = prn.Entries[0].Config;
            Assert.Equal(("CH-01 PRN 15 5M", StreamMode.FrameSyncLock, "334AABBF", "FFFFFFFF", 32767, 5.0),
                         (first.Label, first.Mode, first.SyncPattern, first.SyncMask, first.BitsInMinorFrame, first.TmatsDataRateMbps));
            Assert.Equal((SamplePeriod.OneSecond, 0, 3, 20.0, 16, 3, ""),
                         (first.SamplePeriod, first.PolarityIndex, first.SlopeIndex, first.ScaleDdBPerV, first.NumReceivers, first.ReceiverChannels, first.ReceiverParamsToml));
            Assert.Equal(("", "", 0.0, 0.0), (first.CalCh10Path, first.StepTomlPath, first.ClipStartSec, first.ClipEndSec));
            Assert.Equal(new[]
            {
                new SeriesAppearance(MetricType.FrameSyncLock, 0, 0, "Fifteen", Color.Parse("#ff6a0dad")),
                new SeriesAppearance(MetricType.AccumulatedMissedFrames, 0, 0, "Fifteen", Color.Parse("#ff6a0dad")),
            }, prn.Entries[0].Appearance);
            Assert.Empty(prn.Entries[1].Appearance);
            Assert.Equal(StreamMode.ReceiverChannelInfo, prn.Entries[3].Config.Mode);
            Assert.All(prn.Entries, e => Assert.True(e.Config.Configured));
            Assert.All(prn.Entries, e => Assert.Equal(e.Config.Mode, e.Config.SettingsMode));   // the settings are the mode's

            ProcessingTemplate cal = Load(Fixture("calibrated.json"));
            Assert.Equal(("step_cal, example", 1, false), (cal.Name, cal.TimeChannelIndex, cal.SwapBytes));
            StreamSetup c = Assert.Single(cal.Entries).Config;
            Assert.Equal((26, StreamMode.ReceiverChannelInfo, "FE6B2840", "FFFF0000", 800, true, true, 0.8),
                         (c.PcmChannelId, c.Mode, c.SyncPattern, c.SyncMask, c.BitsInMinorFrame, c.Randomized, c.Inverted, c.DataRateMbps));
            Assert.Equal((SamplePeriod.HundredMilliseconds, 0.8, 1, 2, 7.25, 12, 4),
                         (c.SamplePeriod, c.TmatsDataRateMbps, c.PolarityIndex, c.SlopeIndex, c.ScaleDdBPerV, c.NumReceivers, c.ReceiverChannels));
            Assert.Equal((@"C:\settings\receiver_params\RASA.toml", "D:\\cal\\café Ω.ch10", "settings/rcvr_cals/default.toml", 1.5, 2.25),
                         (c.ReceiverParamsToml, c.CalCh10Path, c.StepTomlPath, c.ClipStartSec, c.ClipEndSec));
            Assert.Empty(c.Calibration);   // profiles are never kept
            Assert.Equal(new[]
            {
                new SeriesAppearance(MetricType.Snr, 1, 0, "Left \"one\"\tΩ/ü\\", Color.FromArgb(0x80, 0xFF, 0, 0)),
                new SeriesAppearance(MetricType.Snr, 3, 2, "C3", Color.FromRgb(0, 0xFF, 0)),
            }, cal.Entries[0].Appearance);

            Assert.Empty(Load(Fixture("empty.json")).Entries);
        }

        [Theory]
        [InlineData("prn-sample.json")]
        [InlineData("calibrated.json")]
        [InlineData("empty.json")]
        public void ATemplateIsWrittenAsV214WroteIt(string name)
        {
            string path = Path.Combine(_dir, name);
            TemplateFile.Write(path, Load(Fixture(name)));
            Assert.Equal(File.ReadAllBytes(Fixture(name)), File.ReadAllBytes(path));
        }

        [Theory]
        [InlineData(20.0, "20")]
        [InlineData(0.0, "0")]
        [InlineData(0.8, "0.8")]
        [InlineData(7.25, "7.25")]
        [InlineData(-0.5, "-0.5")]
        [InlineData(1e-7, "1e-07")]
        [InlineData(1e21, "1e+21")]
        public void ADoubleIsWrittenAsTheShortestTextThatReadsBack(double value, string text) => Assert.Equal(text, TemplateFile.Number(value));

        [Fact]
        public void WhatATemplateLeavesOutIsLeftOutAndAControlCharacterIsEscaped()
        {
            var row = new StreamSetup { PcmChannelId = 1, Label = "a\u0001b\u001f" };
            string json = TemplateFile.ToJson(new ProcessingTemplate("v", "n", 0, true, new[] { new TemplateEntry(row, Array.Empty<SeriesAppearance>()) }));
            Assert.DoesNotContain("appearance", json);
            Assert.DoesNotContain("calibration", json);
            Assert.Contains("\"label\": \"a\\u0001b\\u001f\"", json);   // \u and four lower-case hex digits, up to 0x1f
            // A template read at an older schema version is written at the current one.
            Assert.Contains("\"schemaVersion\": 1", TemplateFile.ToJson(Parse("{\"schemaVersion\": 0, \"entries\": []}")));
            Assert.DoesNotContain("\r", json);
            Assert.EndsWith("}\n", json);
            // A calibration is kept by what it was extracted from, only when there was one.
            string withCal = TemplateFile.ToJson(new ProcessingTemplate("v", "n", 0, true, new[]
            {
                new TemplateEntry(row with { CalCh10Path = "x.ch10", Calibration = new[] { new CalibrationProfile(0, true, Array.Empty<CalibrationPoint>()) } },
                                  Array.Empty<SeriesAppearance>()),
            }));
            Assert.Contains("\"calCh10Path\": \"x.ch10\"", withCal);
            Assert.DoesNotContain("Points", withCal);
        }

        // ---- the reader's rules, v2.14.2's schema's -------------------------------------------------------------------------

        [Theory]
        [InlineData("not json", TemplateLoad.InvalidJson)]
        [InlineData("[]", TemplateLoad.InvalidFormat)]
        [InlineData("{\"entries\": []}", TemplateLoad.UnsupportedSchemaVersion)]
        [InlineData("{\"schemaVersion\": \"1\", \"entries\": []}", TemplateLoad.UnsupportedSchemaVersion)]
        [InlineData("{\"schemaVersion\": 2, \"entries\": []}", TemplateLoad.UnsupportedSchemaVersion)]
        [InlineData("{\"schemaVersion\": 1}", TemplateLoad.InvalidFormat)]
        [InlineData("{\"schemaVersion\": 1, \"entries\": {}}", TemplateLoad.InvalidFormat)]
        [InlineData("{\"schemaVersion\": 1, \"entries\": []}", TemplateLoad.Loaded)]
        [InlineData("{\"schemaVersion\": 0, \"entries\": []}", TemplateLoad.Loaded)]
        public void ATemplateIsRefusedForWhatV214Refused(string json, TemplateLoad expected)
        {
            Assert.Equal(expected, TemplateFile.Parse(json, out ProcessingTemplate? template, out string error));
            Assert.Equal(expected == TemplateLoad.Loaded, template is not null);
            Assert.Equal(expected == TemplateLoad.InvalidJson, error.Length > 0);
        }

        [Fact]
        public void AMissingKeyKeepsItsDefaultAndAnEntryNeedsAChannelAndALabel()
        {
            ProcessingTemplate t = Parse("""
                {"schemaVersion": 1, "entries": [
                  {"config": {"pcmChannelId": 5, "label": "x"}},
                  {"config": {"label": "no id"}},
                  {"config": {"pcmChannelId": 6}},
                  {"other": {}},
                  {"config": "not an object"},
                  7
                ]}
                """);
            Assert.Equal(("", "", 0, true), (t.AppVersion, t.Name, t.TimeChannelIndex, t.SwapBytes));
            StreamSetup s = Assert.Single(t.Entries).Config;
            Assert.Equal((5, "x", false, StreamMode.FrameSyncLock), (s.PcmChannelId, s.Label, s.Process, s.Mode));
            Assert.Equal((FrameSyncDefaults.Pattern, "FFFFFFFF", FrameSyncDefaults.BitsInMinorFrame, false, false, 0.0),
                         (s.SyncPattern, s.SyncMask, s.BitsInMinorFrame, s.Randomized, s.Inverted, s.DataRateMbps));
            Assert.Equal((SamplePeriod.OneSecond, 0.0, 0, 3, 20.0, 16, 3, ""),
                         (s.SamplePeriod, s.TmatsDataRateMbps, s.PolarityIndex, s.SlopeIndex, s.ScaleDdBPerV, s.NumReceivers, s.ReceiverChannels, s.ReceiverParamsToml));
            Assert.Equal(("", "", 0.0, 0.0), (s.CalCh10Path, s.StepTomlPath, s.ClipStartSec, s.ClipEndSec));
            Assert.Empty(t.Entries[0].Appearance);
        }

        [Fact]
        public void AValueOfTheWrongTypeKeepsItsDefaultAndIndicesAreClamped()
        {
            ProcessingTemplate t = Parse("""
                {"schemaVersion": 1, "name": 3, "swapBytes": "no", "timeChannelIndex": 1.5, "entries": [
                  {"config": {"pcmChannelId": "5", "label": "x", "process": "yes", "mode": "Other",
                              "sync": {"bitsInMinorFrame": 800.5, "randomized": 1, "pattern": 7},
                              "samplePeriodIndex": 7, "slopeIndex": -1, "polarityIndex": 5, "scaleDdBPerV": "7",
                              "calibration": "not an object"}},
                  {"config": {"pcmChannelId": 8, "label": "y", "samplePeriodIndex": -3, "slopeIndex": 9, "sync": "not an object",
                              "scaleDdBPerV": true, "calibration": {"calCh10Path": "c.ch10", "clipEndSec": 3, "clipStartSec": null}}}
                ]}
                """);
            Assert.Equal(("", true, 0), (t.Name, t.SwapBytes, t.TimeChannelIndex));
            StreamSetup a = t.Entries[0].Config, b = t.Entries[1].Config;
            Assert.Equal((-1, false, StreamMode.ReceiverChannelInfo), (a.PcmChannelId, a.Process, a.Mode));   // an unknown mode is Receiver SNR
            Assert.Equal((FrameSyncDefaults.BitsInMinorFrame, false, FrameSyncDefaults.Pattern), (a.BitsInMinorFrame, a.Randomized, a.SyncPattern));
            Assert.Equal((SamplePeriod.TenMilliseconds, 0, 5, 20.0), (a.SamplePeriod, a.SlopeIndex, a.PolarityIndex, a.ScaleDdBPerV));
            Assert.Equal("", a.CalCh10Path);
            Assert.Equal((SamplePeriod.OneSecond, 3, 20.0), (b.SamplePeriod, b.SlopeIndex, b.ScaleDdBPerV));
            Assert.Equal(("c.ch10", "", 0.0, 3.0), (b.CalCh10Path, b.StepTomlPath, b.ClipStartSec, b.ClipEndSec));
        }

        [Fact]
        public void AnAppearanceReadsItsMetricAndColourAsV214ReadThem()
        {
            ProcessingTemplate t = Parse("""
                {"schemaVersion": 1, "entries": [
                  {"config": {"pcmChannelId": 5, "label": "x"}, "appearance": [
                    {"metricType": "AccumulatedMissedFrames", "receiverIndex": 2, "channelIndex": 1, "name": "a", "color": "#123456"},
                    {"metricType": "Weird", "color": "red"},
                    {"color": "nope"},
                    "skipped"
                  ]},
                  {"config": {"pcmChannelId": 6, "label": "y"}, "appearance": {"not": "an array"}}
                ]}
                """);
            IReadOnlyList<SeriesAppearance> looks = t.Entries[0].Appearance;
            Assert.Equal(new SeriesAppearance(MetricType.AccumulatedMissedFrames, 2, 1, "a", Color.FromRgb(0x12, 0x34, 0x56)), looks[0]);
            Assert.Equal(new SeriesAppearance(MetricType.Snr, 0, 0, "", Colors.Red), looks[1]);
            Assert.Equal(Colors.Black, looks[2].Color);
            Assert.Equal(3, looks.Count);
            Assert.Empty(t.Entries[1].Appearance);
        }

        // ---- the batch's rules ----------------------------------------------------------------------------------------------

        [Fact]
        public void AFileMatchesOnItsChannelIdsAloneAndAMismatchIsSpelledOut()
        {
            ProcessingTemplate prn = Load(Fixture("prn-sample.json"));
            Assert.True(TemplateBatch.Match(prn, new[] { 16, 15, 14, 13 }).Ok);
            Assert.Equal("missing channel(s): 15, 16", TemplateBatch.Describe(TemplateBatch.Match(prn, new[] { 13, 14 })));
            Assert.Equal("missing channel(s): 16", TemplateBatch.Describe(TemplateBatch.Match(prn, new[] { 13, 14, 15 })));
            Assert.Equal("extra channel(s): 17", TemplateBatch.Describe(TemplateBatch.Match(prn, new[] { 13, 14, 15, 16, 17 })));
            Assert.Equal("missing channel(s): 13, 14, 15, 16; extra channel(s): 26", TemplateBatch.Describe(TemplateBatch.Match(prn, new[] { 26 })));

            string notRecording = Path.Combine(_dir, "not.ch10");
            File.WriteAllText(notRecording, "no");
            IReadOnlyList<FileCheck> checks = TemplateBatch.Validate(prn, new[] { Prn, Repo.Recording("step_cal, example.ch10"), notRecording });
            Assert.Equal(new[]
            {
                new FileCheck(Prn, true, ""),
                new FileCheck(Repo.Recording("step_cal, example.ch10"), false, "missing channel(s): 13, 14, 15, 16; extra channel(s): 26"),
                new FileCheck(notRecording, false, "could not read channels"),
            }, checks);
        }

        private static readonly double[] X2 = { 0.0, 1.0 };

        [Fact]
        public void ATemplateIsCapturedFromOneRecordingAndItsSeriesAsTheyLook()
        {
            var plot = new PlotViewModel();
            plot.SetSeries(new[]
            {
                PlotFixtures.Make(0, "Ch 13", MetricType.FrameSyncLock, 13, 0, X2, X2, source: 1),
                PlotFixtures.Make(1, "Ch 13", MetricType.AccumulatedMissedFrames, 13, 0, X2, X2, source: 1),
                PlotFixtures.Make(2, "Ch 13", MetricType.FrameSyncLock, 13, 1, X2, X2, source: 2),   // another recording's
                PlotFixtures.Make(3, "Rx", MetricType.Snr, 26, 2, X2, X2, receiver: 2, channel: 1, source: 1),
            });
            plot.RecolorSeries(0, Colors.Teal);
            plot.RenameSeries(3, "Two right");
            var rows = new[]
            {
                new StreamSetup { PcmChannelId = 13, Label = "Ch 13", Process = true },
                new StreamSetup { PcmChannelId = 14, Label = "Ch 14" },
                new StreamSetup { PcmChannelId = 26, Label = "Rx", Mode = StreamMode.ReceiverChannelInfo, Process = true },
            };
            ProcessingTemplate t = PlotTemplates.Build(new ProcessedSource(1, @"C:\data\flight.12.ch10", 2, false, rows), plot);
            Assert.Equal((AppInfo.Version, "flight", 2, false), (t.AppVersion, t.Name, t.TimeChannelIndex, t.SwapBytes));
            Assert.Equal(rows, t.Entries.Select(e => e.Config));
            Assert.Equal(new[]
            {
                new SeriesAppearance(MetricType.FrameSyncLock, 0, 0, "Ch 13", Colors.Teal),
                new SeriesAppearance(MetricType.AccumulatedMissedFrames, 0, 0, "Ch 13", Colors.Teal),
            }, t.Entries[0].Appearance);
            Assert.Empty(t.Entries[1].Appearance);
            Assert.Equal(new[] { new SeriesAppearance(MetricType.Snr, 2, 1, "Two right", plot.SeriesById(3)!.Color) }, t.Entries[2].Appearance);
        }

        [Fact]
        public void ATemplatesLooksGoBackOnOneRecordingsMatchingSeriesInOneRefresh()
        {
            var plot = new PlotViewModel();
            // Each decoy comes first, differing from the series a look is for in one thing only.
            plot.SetSeries(new[]
            {
                PlotFixtures.Make(1, "Ch 13", MetricType.FrameSyncLock, 13, 1, X2, X2, source: 2),   // another recording
                PlotFixtures.Make(0, "Ch 13", MetricType.FrameSyncLock, 13, 0, X2, X2, source: 1),
                PlotFixtures.Make(4, "Rx", MetricType.Snr, 26, 2, X2, X2, receiver: 2, channel: 0, source: 1),   // another receiver
                PlotFixtures.Make(3, "Rx", MetricType.Snr, 26, 2, X2, X2, receiver: 1, channel: 1, source: 1),   // another channel
                PlotFixtures.Make(2, "Rx", MetricType.Snr, 26, 2, X2, X2, receiver: 1, channel: 0, source: 1),
            });
            Color before1 = plot.SeriesById(1)!.Color, before3 = plot.SeriesById(3)!.Color, before4 = plot.SeriesById(4)!.Color;
            var template = new ProcessingTemplate("v", "n", 0, true, new[]
            {
                new TemplateEntry(new StreamSetup { PcmChannelId = 13, Label = "Ch 13" },
                                  new[] { new SeriesAppearance(MetricType.FrameSyncLock, 0, 0, "Thirteen", Colors.Olive) }),
                new TemplateEntry(new StreamSetup { PcmChannelId = 26, Label = "Rx" },
                                  new[]
                                  {
                                      new SeriesAppearance(MetricType.Snr, 1, 0, "Left", Colors.Navy),
                                      new SeriesAppearance(MetricType.Snr, 9, 9, "Nowhere", Colors.Red),   // no such series
                                  }),
            });
            int refreshes = 0;
            plot.SeriesChanged += (_, _) => refreshes++;
            PlotTemplates.ApplyAppearance(template, 1, plot);
            Assert.Equal(1, refreshes);
            Assert.Equal(("Thirteen", Colors.Olive), (plot.SeriesById(0)!.Name, plot.SeriesById(0)!.Color));
            Assert.Equal(("Ch 13", before1), (plot.SeriesById(1)!.Name, plot.SeriesById(1)!.Color));   // the other recording's
            Assert.Equal(("Left", Colors.Navy), (plot.SeriesById(2)!.Name, plot.SeriesById(2)!.Color));
            Assert.Equal(before3, plot.SeriesById(3)!.Color);   // the other channel
            Assert.Equal(("Rx", before4), (plot.SeriesById(4)!.Name, plot.SeriesById(4)!.Color));   // the other receiver

            // Put back on the second recording, a look goes on its series, not the first recording's.
            PlotTemplates.ApplyAppearance(new ProcessingTemplate("v", "n", 0, true, new[]
            {
                new TemplateEntry(new StreamSetup { PcmChannelId = 13, Label = "Ch 13" },
                                  new[] { new SeriesAppearance(MetricType.FrameSyncLock, 0, 0, "Second", Colors.Teal) }),
            }), 2, plot);
            Assert.Equal(("Second", "Thirteen"), (plot.SeriesById(1)!.Name, plot.SeriesById(0)!.Name));
        }

        [Fact]
        public void ARecordingsRunIsTheTemplatesProcessedRowsOverItsTimeChannelInItsByteOrder()
        {
            ProcessingTemplate prn = Load(Fixture("prn-sample.json"));
            Ch10File file = StreamsRun.Open(Prn);
            RunRequest r = TemplateBatch.ToRunRequest(prn, Prn, file, 4, Repo.Root);
            Assert.Equal((Prn, file.TimeChannels[0].Id, 4, true, Repo.Root), (r.FileName, r.TimeChannelId, r.SourceId, r.SwapBytes, r.AppRoot));
            Assert.Equal(new[] { 13, 15 }, r.Streams.Select(s => s.PcmChannelId));
            Assert.Equal(new[] { "334AABBF", "A345CA5C" }, r.Streams.Select(s => s.SyncPattern));
            // A place past the recording's list is its default time channel; the template's byte order is the run's.
            RunRequest other = TemplateBatch.ToRunRequest(prn with { TimeChannelIndex = 5, SwapBytes = false }, Prn, file, 0, null);
            Assert.Equal((file.DefaultTimeChannel, false), (other.TimeChannelId, other.SwapBytes));
            foreach (int place in new[] { -1, file.TimeChannels.Count })   // just outside the list, at either end
                Assert.Equal(file.DefaultTimeChannel, TemplateBatch.ToRunRequest(prn with { TimeChannelIndex = place }, Prn, file, 0, null).TimeChannelId);

            // A place inside the list is that time channel: the PRN sample with CH-04 retyped as a time channel (16).
            string twoTime = Recordings.WriteTwoTimeChannels(Path.Combine(_dir, "two-time.ch10"));
            Ch10File two = StreamsRun.Open(twoTime);
            Assert.Equal(16, TemplateBatch.ToRunRequest(prn with { TimeChannelIndex = 1 }, twoTime, two, 0, null).TimeChannelId);
            Assert.Equal("flight", TemplateBatch.BaseName("flight"));
        }

        // ---- the plot: several recordings, and Plot File -------------------------------------------------------------------

        [Fact]
        public void ThePlotListsItsRecordingsAndShowsOneOrAll()
        {
            var plot = new PlotViewModel();
            plot.SetSeries(new[]
            {
                PlotFixtures.Make(0, "a", MetricType.Snr, 1, 0, new[] { 0.0, 1.0 }, new[] { 10.0, 20.0 }, source: 3),
                PlotFixtures.Make(1, "b", MetricType.Snr, 1, 1, new[] { 5.0, 9.0 }, new[] { 40.0, 60.0 }, source: 1),
                PlotFixtures.Make(2, "c", MetricType.Snr, 2, 2, new[] { 0.0, 2.0 }, new[] { 12.0, 14.0 }, source: 3),
            });
            plot.SetSourceLabel(3, "first");
            Assert.Equal(new[] { (3, "first"), (1, "Source 1") }, plot.SourceList());
            Assert.Equal(-1, plot.VisibleSource);
            Assert.True(plot.Series.All(plot.EffectiveVisible));

            int refreshes = 0;
            plot.SeriesChanged += (_, _) => refreshes++;
            plot.SetVisibleSource(1);
            Assert.Equal(1, refreshes);
            Assert.Equal(new[] { false, true, false }, plot.Series.Select(plot.EffectiveVisible));
            Assert.Equal((5.0, 9.0), (plot.XViewMin, plot.XViewMax));   // the window is that recording's span
            Assert.Equal((40.0, 60.0), (plot.RightMin, plot.RightMax));  // and the axis its range

            plot.SetVisibleSource(3);
            Assert.Equal((0.0, 2.0), (plot.XViewMin, plot.XViewMax));
            plot.Series[2].Visible = false;   // a series hidden stays hidden whichever recording is shown
            plot.SetVisibleSource(-1);
            Assert.Equal(new[] { true, true, false }, plot.Series.Select(plot.EffectiveVisible));
            Assert.Equal((0.0, 9.0), (plot.XViewMin, plot.XViewMax));

            // The CSV is the data: every recording's series, whichever is shown.
            plot.SetVisibleSource(1);
            string csv = Path.Combine(_dir, "all.csv");
            Assert.True(plot.ExportCsv(csv));
            Assert.Equal(3, File.ReadAllLines(csv)[0].Split(',').Length - 1);

            plot.Clear();
            Assert.Equal(-1, plot.VisibleSource);
            plot.SetSeries(new[] { PlotFixtures.Make(0, "a", MetricType.Snr, 1, 0, X2, X2, source: 3) });
            Assert.Equal(new[] { (3, "Source 3") }, plot.SourceList());   // the labels went with it

            // Source 0 is a recording like any other; one of a single sample is shown at its instant; each choice is told.
            plot.SetSeries(new[]
            {
                PlotFixtures.Make(0, "a", MetricType.Snr, 1, 0, X2, X2, source: 0),
                PlotFixtures.Make(1, "b", MetricType.Snr, 1, 1, new[] { 7.0 }, new[] { 30.0 }, source: 5),
            });
            int told = 0;
            plot.PropertyChanged += (_, e) => told += e.PropertyName == nameof(PlotViewModel.VisibleSource) ? 1 : 0;
            plot.SetVisibleSource(0);
            Assert.Equal(new[] { true, false }, plot.Series.Select(plot.EffectiveVisible));
            plot.SetVisibleSource(5);
            Assert.Equal((7.0, 7.0), (plot.XViewMin, plot.XViewMax));
            Assert.Equal(2, told);
        }

        [AvaloniaFact]
        public void PlotFileListsTheRecordingsAndShowsTheOneChosen()
        {
            var plot = new PlotViewModel();
            plot.SetSeries(new[]
            {
                PlotFixtures.Make(0, "a", MetricType.Snr, 1, 0, X2, X2, source: 0),
                PlotFixtures.Make(1, "b", MetricType.Snr, 1, 1, X2, X2, source: 1),
            });
            plot.SetSourceLabel(0, "flight 1");
            plot.SetSourceLabel(1, "flight 2");
            var view = new PlotView { Width = 600, Height = 400, Plot = plot };
            var window = new Window { Content = view };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            MenuItem plotFile = Menus.Item(Menus.Open(view), "Plot File");
            Assert.True(plotFile.IsEnabled);
            Assert.Equal(new[] { "All files (overlaid)", "-", "flight 1", "flight 2" }, Menus.Headers(plotFile));
            var entries = Menus.Entries(plotFile).Cast<MenuItem>().ToArray();
            Assert.True(entries[0].IsChecked);
            Assert.All(entries, e => Assert.Equal(MenuItemToggleType.Radio, e.ToggleType));
            Assert.Equal(new[] { false, true, false, false }, entries.Select(e => e.Classes.Contains(":separator")));   // drawn as a line
            entries[3].Command!.Execute(entries[3].CommandParameter);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(1, plot.VisibleSource);
            Assert.Equal(new[] { false, true }, new[] { entries[0].IsChecked, entries[3].IsChecked });
            entries[0].Command!.Execute(entries[0].CommandParameter);
            Assert.Equal(-1, plot.VisibleSource);   // all of them again

            // One recording has nothing to choose between.
            plot.SetSeries(new[] { PlotFixtures.Make(0, "a", MetricType.Snr, 1, 0, X2, X2) });
            Dispatcher.UIThread.RunJobs();
            Assert.False(Menus.Item(Menus.Open(view), "Plot File").IsEnabled);
            window.Close();
        }
    }
}
