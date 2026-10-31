using System;
using System.IO;
using TmDq.App.Processing;
using TmDq.App.Streams;
using TmDq.Core;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// What reaches a row from files (plan step 4.1a): the defaults a new row and a Receiver SNR row start with, and the
    /// setup dialogs' Load and Save of the frame-sync pattern file and the receiver-parameters file. The files are read
    /// and written by the core; these hold the app to applying them as the Qt dialogs do (US1.0, US5.0, US5.1). The
    /// flag cases port tst_streamconfigdialog.cpp's tomlFrameSyncRoundTripsStreamFormatFlags and
    /// tomlFrameSyncOmitsPerSessionFields.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class StreamFilesTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "tmdq-streamfiles-" + Guid.NewGuid().ToString("N"));

        public StreamFilesTests()
        {
            NativeCore.EnsureLoaded();
            Directory.CreateDirectory(_dir);
        }

        public void Dispose() => TempFiles.DeleteDirectory(_dir, recursive: true);

        private string Write(string name, string text)
        {
            string path = Path.Combine(_dir, name);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, text);
            return path;
        }

        private static StreamSetup Row() => new() { PcmChannelId = 1, Label = "Ch 01" };

        // --- defaults ---

        [Fact]
        public void ANewRowTakesTheChannelAndTheReceiverDefaults()
        {
            StreamSetup row = StreamDefaults.NewStream(new ChannelInfo(13, "CH-01 PRN 15 5M", 5_000_000));
            Assert.Equal((13, "CH-01 PRN 15 5M", 5.0), (row.PcmChannelId, row.Label, row.TmatsDataRateMbps));
            Assert.Equal(ReceiverParamsFile.Defaults(), StreamFiles.ReceiverParamsOf(row));
            Assert.Equal(new ReceiverParams(0, 3, 20.0, 16, 3), StreamFiles.ReceiverParamsOf(row));
            Assert.Equal(StreamFiles.ReceiverParamsOf(Row()), StreamFiles.ReceiverParamsOf(row));
            Assert.Equal(0.0, StreamDefaults.NewStream(new ChannelInfo(2, "NO RATE", 0)).TmatsDataRateMbps);
        }

        [Fact]
        public void TheShippedReceiverSnrDefaultsAreTheConstants()
        {
            Assert.Equal((ReceiverSnrDefaults.Pattern, FrameSyncDefaults.Mask, ReceiverSnrDefaults.BitsInMinorFrame),
                         StreamDefaults.FrameSyncFor(StreamMode.ReceiverChannelInfo, Repo.Root));
        }

        private string AppRootWith(string text)
        {
            string root = Path.Combine(_dir, "app");
            Write(Path.Combine("app", "settings", "framesync_patterns", ReceiverSnrDefaults.FileName), text);
            return root;
        }

        [Fact]
        public void TheReceiverSnrDefaultsFileIsReadFieldByField()
        {
            string root = AppRootWith("[Frame]\nFrameSync = \"abcd1234\"\nFrameSyncMask = \"ffff0000\"\nBitsPerFrame = 1600\nRandomized = true\nInverted = true\n");
            Assert.Equal(("ABCD1234", "FFFF0000", 1600), StreamDefaults.FrameSyncFor(StreamMode.ReceiverChannelInfo, root));
        }

        [Fact]
        public void WhatTheReceiverSnrDefaultsFileDoesNotSayIsTheConstants()
        {
            Assert.Equal(("ABCD1234", "FFFFFFFF", 800),
                         StreamDefaults.FrameSyncFor(StreamMode.ReceiverChannelInfo, AppRootWith("[Frame]\nFrameSync = \"abcd1234\"\n")));
        }

        [Fact]
        public void AFrameLengthFromTheOlderKeyIsNotADefault()
        {
            Assert.Equal(("FE6B2840", "FFFFFFFF", 800),
                         StreamDefaults.FrameSyncFor(StreamMode.ReceiverChannelInfo, AppRootWith("[Frame]\nWordsInMinorFrame = 64\n")));
        }

        [Fact]
        public void NoReceiverSnrDefaultsFileIsTheConstants()
        {
            Assert.Equal(("FE6B2840", "FFFFFFFF", 800), StreamDefaults.FrameSyncFor(StreamMode.ReceiverChannelInfo, Path.Combine(_dir, "nowhere")));
            Assert.Equal(("FE6B2840", "FFFFFFFF", 800), StreamDefaults.FrameSyncFor(StreamMode.ReceiverChannelInfo, null));
            Assert.Equal(("FE6B2840", "FFFFFFFF", 800), StreamDefaults.FrameSyncFor(StreamMode.ReceiverChannelInfo, ""));
        }

        [Fact]
        public void FrameSyncLockIgnoresTheReceiverSnrFile()
        {
            string root = AppRootWith("[Frame]\nFrameSync = \"abcd1234\"\nBitsPerFrame = 1600\n");
            Assert.Equal(("334AABBF", "FFFFFFFF", 32767), StreamDefaults.FrameSyncFor(StreamMode.FrameSyncLock, root));
        }

        // --- the frame-sync pattern file ---

        [Theory]
        [InlineData(true, true)]
        [InlineData(true, false)]
        [InlineData(false, true)]
        public void APatternFileRoundTripsTheFrameSyncAndBothFlags(bool inverted, bool randomized)
        {
            StreamSetup saved = Row() with { SyncPattern = "FE6B2840", SyncMask = "FFFF0000", BitsInMinorFrame = 800, Inverted = inverted, Randomized = randomized };
            string path = StreamFiles.SaveFrameSync(saved, Path.Combine(_dir, "rcvr.toml"));
            StreamSetup loaded = StreamFiles.LoadFrameSync(Row() with { Inverted = !inverted, Randomized = !randomized }, path);
            Assert.Equal(("FE6B2840", "FFFF0000", 800, inverted, randomized),
                         (loaded.SyncPattern, loaded.SyncMask, loaded.BitsInMinorFrame, loaded.Inverted, loaded.Randomized));
        }

        [Fact]
        public void APatternFileIsWrittenTrimmedAndUpperCased()
        {
            string path = StreamFiles.SaveFrameSync(Row() with { SyncPattern = " fe6b2840 ", SyncMask = " ffff0000 " }, Path.Combine(_dir, "case.toml"));
            FrameSyncFileData written = FrameSyncFile.Read(path);   // as written: the reader does not fold case
            Assert.Equal(("FE6B2840", "FFFF0000"), (written.Pattern, written.Mask));
        }

        [Fact]
        public void APatternFileInLowerCaseLoadsUpperCased()
        {
            string path = Write("lower.toml", "[Frame]\nFrameSync = \"fe6b2840\"\nFrameSyncMask = \"ffff0000\"\n");
            StreamSetup loaded = StreamFiles.LoadFrameSync(Row(), path);
            Assert.Equal(("FE6B2840", "FFFF0000"), (loaded.SyncPattern, loaded.SyncMask));
        }

        [Fact]
        public void ThePatternFileLeavesOutThePerSessionFields()
        {
            StreamSetup saved = Row() with { DataRateMbps = 5, SamplePeriod = SamplePeriod.TenMilliseconds };
            string text = File.ReadAllText(StreamFiles.SaveFrameSync(saved, Path.Combine(_dir, "scope.toml")));
            Assert.DoesNotContain("DataRate", text, StringComparison.Ordinal);
            Assert.DoesNotContain("SampleRate", text, StringComparison.Ordinal);

            StreamSetup shown = Row() with { DataRateMbps = 7, SamplePeriod = SamplePeriod.HundredMilliseconds };
            StreamSetup loaded = StreamFiles.LoadFrameSync(shown, Path.Combine(_dir, "scope.toml"));
            Assert.Equal((7.0, SamplePeriod.HundredMilliseconds), (loaded.DataRateMbps, loaded.SamplePeriod));
        }

        [Fact]
        public void AnOlderPatternFileLeavesDerandomizeAloneAndClearsInvert()
        {
            string legacy = Write("legacy.toml", "[Frame]\nFrameSync = \"334AABBF\"\nBitsPerFrame = 32767\n");
            StreamSetup shown = Row() with { SyncPattern = "FE6B2840", BitsInMinorFrame = 800, Randomized = true, Inverted = true };
            StreamSetup loaded = StreamFiles.LoadFrameSync(shown, legacy);
            Assert.Equal(("334AABBF", 32767, true, false), (loaded.SyncPattern, loaded.BitsInMinorFrame, loaded.Randomized, loaded.Inverted));
        }

        [Fact]
        public void APatternFileWithoutAPatternOrMaskKeepsThoseShown()
        {
            StreamSetup shown = Row() with { SyncPattern = "FE6B2840", SyncMask = "FFFF0000" };
            StreamSetup loaded = StreamFiles.LoadFrameSync(shown, Write("bits.toml", "[Frame]\nBitsPerFrame = 1024\n"));
            Assert.Equal(("FE6B2840", "FFFF0000", 1024), (loaded.SyncPattern, loaded.SyncMask, loaded.BitsInMinorFrame));
        }

        [Fact]
        public void AFrameLengthOutsideTheDialogsRangeIsHeldToIt()
        {
            Assert.Equal(65536, StreamFiles.LoadFrameSync(Row(), Write("long.toml", "[Frame]\nBitsPerFrame = 100000\n")).BitsInMinorFrame);
            Assert.Equal(64, StreamFiles.ApplyFrameSyncFile(Row(), new FrameSyncFileData("", "", 8, false, false, null)).BitsInMinorFrame);
        }

        [Fact]
        public void AMissingPatternFileClearsOnlyInvert()
        {
            StreamSetup shown = Row() with { SyncPattern = "FE6B2840", Inverted = true, Randomized = true };
            Assert.Equal(shown with { Inverted = false }, StreamFiles.LoadFrameSync(shown, Path.Combine(_dir, "nowhere.toml")));
        }

        [Fact]
        public void APatternFileSavedWithoutAnExtensionGetsToml()
        {
            string path = StreamFiles.SaveFrameSync(Row(), Path.Combine(_dir, "plain"));
            Assert.Equal(Path.Combine(_dir, "plain.toml"), path);
            Assert.True(File.Exists(path));
        }

        [Fact]
        public void APatternFileThatCannotBeWrittenSaysSo()
        {
            Assert.Throws<TmdqException>(() => StreamFiles.SaveFrameSync(Row(), Path.Combine(_dir, "no-such-folder", "x.toml")));
        }

        // --- the receiver-parameters file ---

        [Fact]
        public void LoadingReceiverParametersSetsWhatTheFileSaysAndMakesItTheWordMap()
        {
            string path = Write("rx.toml", "[Parameters]\nScale=33\n[Receivers]\nCount=9\n");
            StreamSetup shown = Row() with { PolarityIndex = 1, SlopeIndex = 2, ScaleDdBPerV = 41.5, NumReceivers = 7, ReceiverChannels = 5, ReceiverParamsToml = "old.toml" };
            StreamSetup loaded = StreamFiles.LoadReceiverParams(shown, path);
            Assert.Equal(shown with { ScaleDdBPerV = 33, NumReceivers = 9, ReceiverParamsToml = path }, loaded);
        }

        [Fact]
        public void SavedReceiverParametersReadBackAsTheRowHasThem()
        {
            StreamSetup row = Row() with
            {
                Mode = StreamMode.ReceiverChannelInfo,
                BitsInMinorFrame = 800,
                PolarityIndex = 1,
                SlopeIndex = 2,
                ScaleDdBPerV = 41.5,
                NumReceivers = 4,
                ReceiverChannels = 3,
            };
            string path = StreamFiles.SaveReceiverParams(row, Path.Combine(_dir, "saved"));
            Assert.Equal(Path.Combine(_dir, "saved.toml"), path);
            Assert.Equal(StreamFiles.ReceiverParamsOf(row), ReceiverParamsFile.Read(path));
            // Loading what was saved gives the row back, now pointing at the file.
            Assert.Equal(row with { ReceiverParamsToml = path }, StreamFiles.LoadReceiverParams(row with { ScaleDdBPerV = 1 }, path));
        }

        [Fact]
        public void SavingKeepsTheWordMapTheRowWouldProcessWith()
        {
            // Two receivers on screen, but the row's file is RASA's twelve: the saved map is RASA's.
            string rasa = Path.Combine(Repo.Root, "settings", "receiver_params", "RASA.toml");
            StreamSetup row = Row() with { BitsInMinorFrame = 800, NumReceivers = 2, ReceiverChannels = 3, ReceiverParamsToml = rasa };
            string text = File.ReadAllText(StreamFiles.SaveReceiverParams(row, Path.Combine(_dir, "rasa-map.toml")));
            Assert.Contains("[C_RCVR12]", text, StringComparison.Ordinal);

            string own = File.ReadAllText(StreamFiles.SaveReceiverParams(row with { ReceiverParamsToml = "" }, Path.Combine(_dir, "own-map.toml")));
            Assert.Contains("[C_RCVR2]", own, StringComparison.Ordinal);
            Assert.DoesNotContain("RCVR3]", own, StringComparison.Ordinal);
        }

        [Fact]
        public void TheRowsFrameLengthBoundsTheSavedMap()
        {
            // The default 16 x 3 map needs 48 words beyond the sync; a 160-bit frame has 10.
            StreamSetup row = Row() with { BitsInMinorFrame = 160 };
            Assert.Throws<TmdqException>(() => StreamFiles.SaveReceiverParams(row, Path.Combine(_dir, "short.toml")));
            Assert.True(File.Exists(StreamFiles.SaveReceiverParams(row with { BitsInMinorFrame = 800 }, Path.Combine(_dir, "long.toml"))));
        }

        [Fact]
        public void ReceiverParametersThatCannotBeWrittenSaySo()
        {
            StreamSetup row = Row() with { BitsInMinorFrame = 800 };
            TmdqException e = Assert.Throws<TmdqException>(() => StreamFiles.SaveReceiverParams(row, Path.Combine(_dir, "no-such-folder", "x.toml")));
            Assert.Contains("x.toml", e.Message, StringComparison.Ordinal);
        }

        [Theory]
        [InlineData(@"C:\a\b", @"C:\a\b.toml")]
        [InlineData(@"C:\a\b.toml", @"C:\a\b.toml")]
        [InlineData(@"C:\a\b.txt", @"C:\a\b.txt")]
        [InlineData(@"C:\a.d\b", @"C:\a.d\b.toml")]
        public void ASaveWithoutAnExtensionGetsToml(string given, string written)
        {
            Assert.Equal(written, StreamFiles.WithTomlExtension(given));
        }
    }
}
