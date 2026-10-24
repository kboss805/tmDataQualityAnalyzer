using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using TmDq.Core;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// The seam between the native and the managed side. A wrong offset reads plausible garbage at the wrong place
/// and fails nowhere else, so the layout is checked two ways: against the sizes the DLL itself reports, and
/// against the field offsets pinned here - the same numbers TestAbi::theStructLayoutsArePinned pins on the
/// C++ side, so a change to either side alone fails one of the two.
/// </summary>
public class LayoutTests
{
    public LayoutTests()
    {
        Env.EnsureLoaded();
    }

    [Fact]
    public void TheDllSpeaksTheVersionThisBindingWasWrittenFor()
    {
        Assert.Equal(2, Library.ExpectedVersion);
        Assert.Equal(2, Library.AbiVersion);
        Assert.Equal(0, Library.AbiRevision);
        Assert.True(Library.AbiRevision >= Library.MinimumRevision);
        Assert.Equal(Path.GetFullPath(Env.DllPath()), Library.LoadedPath, ignoreCase: true);
    }

    [Fact]
    public void EveryMirroredStructHasTheSizeTheDllReports()
    {
        Assert.NotEmpty(Library.Mirrored);
        foreach ((StructId id, Type type) in Library.Mirrored)
        {
            Assert.Equal(Native.tmdq_sizeof((int)id), Marshal.SizeOf(type));
        }
        Assert.Equal(0, Native.tmdq_sizeof(0));
        Assert.Equal(0, Native.tmdq_sizeof(9999));
    }

    public static IEnumerable<object[]> PinnedSizes() => new[]
    {
        new object[] { typeof(ChannelInfoNative), 24 },
        new object[] { typeof(CalPointNative), 16 },
        new object[] { typeof(CalProfileNative), 24 },
        new object[] { typeof(StreamConfigNative), 104 },
        new object[] { typeof(RunRequestNative), 48 },
        new object[] { typeof(CallbacksNative), 56 },
        new object[] { typeof(StreamInfoNative), 64 },
        new object[] { typeof(ChannelSeriesNative), 24 },
        new object[] { typeof(CalibrationRequestNative), 112 },
        new object[] { typeof(CalibrationInfoNative), 48 },
        new object[] { typeof(CalibrationChannelNative), 96 },
        new object[] { typeof(FrameSyncFileInfoNative), 48 },
        new object[] { typeof(MatchInfoNative), 32 },
        new object[] { typeof(CsvInfoNative), 32 },
        new object[] { typeof(CsvSeriesInfoNative), 80 },
        new object[] { typeof(CsvColumnNative), 48 },
        new object[] { typeof(SeriesSetInfoNative), 24 },
        new object[] { typeof(SeriesSpecNative), 72 },
        new object[] { typeof(StreamKeyNative), 24 },
        new object[] { typeof(TimeBaseInfoNative), 24 },
        new object[] { typeof(ImportedSeriesNative), 24 },
    };

    [Theory]
    [MemberData(nameof(PinnedSizes))]
    public void EachStructIsTheSizeTheNativeTestsPin(Type type, int size)
    {
        Assert.Equal(size, Marshal.SizeOf(type));
    }

    public static IEnumerable<object[]> PinnedOffsets()
    {
        object[] Row(Type t, string field, int offset) => new object[] { t, field, offset };

        var rows = new List<object[]>
        {
            Row(typeof(ChannelInfoNative), "StructSize", 0), Row(typeof(ChannelInfoNative), "ChannelId", 4),
            Row(typeof(ChannelInfoNative), "TmatsDataRateBps", 8), Row(typeof(ChannelInfoNative), "NameUtf8", 16),

            Row(typeof(CalPointNative), "TrueDb", 8),

            Row(typeof(CalProfileNative), "WordIndex", 8), Row(typeof(CalProfileNative), "PointCount", 12),
            Row(typeof(CalProfileNative), "Valid", 16),

            Row(typeof(StreamConfigNative), "PcmChannelId", 4), Row(typeof(StreamConfigNative), "LabelUtf8", 8),
            Row(typeof(StreamConfigNative), "SyncPatternUtf8", 16), Row(typeof(StreamConfigNative), "SyncMaskUtf8", 24),
            Row(typeof(StreamConfigNative), "ReceiverParamsTomlUtf8", 32), Row(typeof(StreamConfigNative), "Calibration", 40),
            Row(typeof(StreamConfigNative), "DataRateMbps", 48), Row(typeof(StreamConfigNative), "ScaleDdBPerV", 56),
            Row(typeof(StreamConfigNative), "Mode", 64), Row(typeof(StreamConfigNative), "BitsInMinorFrame", 68),
            Row(typeof(StreamConfigNative), "Randomized", 72), Row(typeof(StreamConfigNative), "Inverted", 76),
            Row(typeof(StreamConfigNative), "SamplePeriodIndex", 80), Row(typeof(StreamConfigNative), "PolarityIndex", 84),
            Row(typeof(StreamConfigNative), "SlopeIndex", 88), Row(typeof(StreamConfigNative), "NumReceivers", 92),
            Row(typeof(StreamConfigNative), "ReceiverChannels", 96), Row(typeof(StreamConfigNative), "CalibrationCount", 100),

            Row(typeof(RunRequestNative), "TimeChannelId", 4), Row(typeof(RunRequestNative), "FileNameUtf8", 8),
            Row(typeof(RunRequestNative), "AppRootUtf8", 16), Row(typeof(RunRequestNative), "Streams", 24),
            Row(typeof(RunRequestNative), "StreamCount", 32), Row(typeof(RunRequestNative), "SourceId", 36),
            Row(typeof(RunRequestNative), "SwapBytes", 40),

            Row(typeof(CallbacksNative), "User", 8), Row(typeof(CallbacksNative), "Log", 16),
            Row(typeof(CallbacksNative), "Error", 24), Row(typeof(CallbacksNative), "Progress", 32),
            Row(typeof(CallbacksNative), "StreamDone", 40), Row(typeof(CallbacksNative), "Finished", 48),

            Row(typeof(StreamInfoNative), "PcmChannelId", 4), Row(typeof(StreamInfoNative), "LabelUtf8", 8),
            Row(typeof(StreamInfoNative), "TimesSec", 16), Row(typeof(StreamInfoNative), "LockPercent", 24),
            Row(typeof(StreamInfoNative), "MissedFrames", 32), Row(typeof(StreamInfoNative), "JobIndex", 40),
            Row(typeof(StreamInfoNative), "SourceId", 44), Row(typeof(StreamInfoNative), "Mode", 48),
            Row(typeof(StreamInfoNative), "SampleCount", 52), Row(typeof(StreamInfoNative), "ChannelCount", 56),

            Row(typeof(ChannelSeriesNative), "WordIndex", 4), Row(typeof(ChannelSeriesNative), "NameUtf8", 8),
            Row(typeof(ChannelSeriesNative), "Values", 16),

            Row(typeof(CalibrationRequestNative), "TimeChannelId", 4), Row(typeof(CalibrationRequestNative), "CalFileNameUtf8", 8),
            Row(typeof(CalibrationRequestNative), "AppRootUtf8", 16), Row(typeof(CalibrationRequestNative), "SyncPatternUtf8", 24),
            Row(typeof(CalibrationRequestNative), "SyncMaskUtf8", 32), Row(typeof(CalibrationRequestNative), "ReceiverParamsTomlUtf8", 40),
            Row(typeof(CalibrationRequestNative), "StepDb", 48), Row(typeof(CalibrationRequestNative), "DataRateMbps", 56),
            Row(typeof(CalibrationRequestNative), "ClipStartSec", 64), Row(typeof(CalibrationRequestNative), "ClipEndSec", 72),
            Row(typeof(CalibrationRequestNative), "PcmChannelId", 80), Row(typeof(CalibrationRequestNative), "BitsInMinorFrame", 84),
            Row(typeof(CalibrationRequestNative), "Randomized", 88), Row(typeof(CalibrationRequestNative), "Inverted", 92),
            Row(typeof(CalibrationRequestNative), "SwapBytes", 96), Row(typeof(CalibrationRequestNative), "NumReceivers", 100),
            Row(typeof(CalibrationRequestNative), "ReceiverChannels", 104), Row(typeof(CalibrationRequestNative), "StepCount", 108),

            Row(typeof(CalibrationInfoNative), "ChannelCount", 4), Row(typeof(CalibrationInfoNative), "SummaryUtf8", 8),
            Row(typeof(CalibrationInfoNative), "ErrorUtf8", 16), Row(typeof(CalibrationInfoNative), "ReportUtf8", 24),
            Row(typeof(CalibrationInfoNative), "RecordingSeconds", 32), Row(typeof(CalibrationInfoNative), "ClipIgnored", 40),

            Row(typeof(CalibrationChannelNative), "WordIndex", 4), Row(typeof(CalibrationChannelNative), "NameUtf8", 8),
            Row(typeof(CalibrationChannelNative), "Points", 16), Row(typeof(CalibrationChannelNative), "PlateauLevels", 24),
            Row(typeof(CalibrationChannelNative), "UnresolvedDb", 32), Row(typeof(CalibrationChannelNative), "EdgeThreshold", 40),
            Row(typeof(CalibrationChannelNative), "PointCount", 48), Row(typeof(CalibrationChannelNative), "PlateauLevelCount", 52),
            Row(typeof(CalibrationChannelNative), "UnresolvedCount", 56), Row(typeof(CalibrationChannelNative), "Valid", 60),
            Row(typeof(CalibrationChannelNative), "HadData", 64), Row(typeof(CalibrationChannelNative), "Outcome", 68),
            Row(typeof(CalibrationChannelNative), "DetectedPlateaus", 72), Row(typeof(CalibrationChannelNative), "ExtraPlateaus", 76),
            Row(typeof(CalibrationChannelNative), "ThresholdRelaxed", 80), Row(typeof(CalibrationChannelNative), "Saturated", 84),
            Row(typeof(CalibrationChannelNative), "SweepLevels", 88),

            Row(typeof(FrameSyncFileInfoNative), "HasBitsPerFrame", 4), Row(typeof(FrameSyncFileInfoNative), "BitsPerFrame", 8),
            Row(typeof(FrameSyncFileInfoNative), "BitsFromLegacyKey", 12), Row(typeof(FrameSyncFileInfoNative), "Inverted", 16),
            Row(typeof(FrameSyncFileInfoNative), "HasRandomized", 20), Row(typeof(FrameSyncFileInfoNative), "Randomized", 24),
            Row(typeof(FrameSyncFileInfoNative), "PatternUtf8", 32), Row(typeof(FrameSyncFileInfoNative), "MaskUtf8", 40),

            Row(typeof(MatchInfoNative), "Ok", 4), Row(typeof(MatchInfoNative), "MissingCount", 8),
            Row(typeof(MatchInfoNative), "ExtraCount", 12), Row(typeof(MatchInfoNative), "Missing", 16),
            Row(typeof(MatchInfoNative), "Extra", 24),

            Row(typeof(CsvInfoNative), "SeriesCount", 4), Row(typeof(CsvInfoNative), "BaseDay", 8),
            Row(typeof(CsvInfoNative), "SkippedRows", 12), Row(typeof(CsvInfoNative), "BaseTimeOffset", 16),
            Row(typeof(CsvInfoNative), "XMax", 24),

            Row(typeof(CsvSeriesInfoNative), "MetricType", 4), Row(typeof(CsvSeriesInfoNative), "NameUtf8", 8),
            Row(typeof(CsvSeriesInfoNative), "StreamLabelUtf8", 16), Row(typeof(CsvSeriesInfoNative), "XValues", 24),
            Row(typeof(CsvSeriesInfoNative), "YValues", 32), Row(typeof(CsvSeriesInfoNative), "YMin", 40),
            Row(typeof(CsvSeriesInfoNative), "YMax", 48), Row(typeof(CsvSeriesInfoNative), "ReceiverIndex", 56),
            Row(typeof(CsvSeriesInfoNative), "ChannelIndex", 60), Row(typeof(CsvSeriesInfoNative), "StreamOrder", 64),
            Row(typeof(CsvSeriesInfoNative), "SourceId", 68), Row(typeof(CsvSeriesInfoNative), "PointCount", 72),

            Row(typeof(CsvColumnNative), "MetricType", 4), Row(typeof(CsvColumnNative), "NameUtf8", 8),
            Row(typeof(CsvColumnNative), "XValues", 16), Row(typeof(CsvColumnNative), "YValues", 24),
            Row(typeof(CsvColumnNative), "SourceId", 32), Row(typeof(CsvColumnNative), "XCount", 36),
            Row(typeof(CsvColumnNative), "YCount", 40),

            Row(typeof(SeriesSetInfoNative), "SeriesCount", 4), Row(typeof(SeriesSetInfoNative), "ElapsedCount", 8),
            Row(typeof(SeriesSetInfoNative), "Elapsed", 16),

            Row(typeof(SeriesSpecNative), "MetricType", 4), Row(typeof(SeriesSpecNative), "NameUtf8", 8),
            Row(typeof(SeriesSpecNative), "StreamLabelUtf8", 16), Row(typeof(SeriesSpecNative), "YValues", 24),
            Row(typeof(SeriesSpecNative), "YMin", 32), Row(typeof(SeriesSpecNative), "YMax", 40),
            Row(typeof(SeriesSpecNative), "ReceiverIndex", 48), Row(typeof(SeriesSpecNative), "ChannelIndex", 52),
            Row(typeof(SeriesSpecNative), "StreamOrder", 56), Row(typeof(SeriesSpecNative), "StreamSequence", 60),
            Row(typeof(SeriesSpecNative), "SourceId", 64), Row(typeof(SeriesSpecNative), "YCount", 68),

            Row(typeof(StreamKeyNative), "SourceId", 4), Row(typeof(StreamKeyNative), "LabelUtf8", 8),
            Row(typeof(StreamKeyNative), "Channel", 16),

            Row(typeof(TimeBaseInfoNative), "BaseDay", 4), Row(typeof(TimeBaseInfoNative), "BaseAbsSeconds", 8),
            Row(typeof(TimeBaseInfoNative), "BaseTimeOffset", 16),

            Row(typeof(ImportedSeriesNative), "MetricType", 4), Row(typeof(ImportedSeriesNative), "StreamLabelUtf8", 8),
            Row(typeof(ImportedSeriesNative), "SourceId", 16), Row(typeof(ImportedSeriesNative), "StreamOrder", 20),
        };
        return rows;
    }

    [Theory]
    [MemberData(nameof(PinnedOffsets))]
    public void EachFieldIsWhereTheNativeTestsPinIt(Type type, string field, int offset)
    {
        Assert.Equal(offset, (int)Marshal.OffsetOf(type, field));
    }

    [Fact]
    public void TheStatusNumbersAreTheHeadersNumbers()
    {
        Assert.Equal(0, (int)Status.Ok);
        Assert.Equal(1, (int)Status.BadArgument);
        Assert.Equal(2, (int)Status.Prepare);
        Assert.Equal(3, (int)Status.Process);
        Assert.Equal(4, (int)Status.NoSamples);
        Assert.Equal(5, (int)Status.Internal);
        Assert.Equal(6, (int)Status.Job);
        Assert.Equal(7, (int)Status.State);
        Assert.Equal(8, (int)Status.Io);
    }

    [Fact]
    public void TheMetricNumbersAreTheCoresNumbers()
    {
        Assert.Equal(0, (int)MetricType.Snr);
        Assert.Equal(1, (int)MetricType.FrameSyncLock);
        Assert.Equal(2, (int)MetricType.AccumulatedMissedFrames);
    }

    [Fact]
    public void TheOutcomeNumbersAreTheCoresNumbers()
    {
        Assert.Equal(0, (int)CalibrationOutcome.Calibrated);
        Assert.Equal(1, (int)CalibrationOutcome.NoData);
        Assert.Equal(2, (int)CalibrationOutcome.FlatNoSignal);
        Assert.Equal(3, (int)CalibrationOutcome.TooFewPlateaus);
        Assert.Equal(4, (int)CalibrationOutcome.NoMonotonicSweep);
    }

    [Fact]
    public void TheModeAndPeriodNumbersAreTheCoresNumbers()
    {
        Assert.Equal(0, (int)StreamMode.ReceiverChannelInfo);
        Assert.Equal(1, (int)StreamMode.FrameSyncLock);
        Assert.Equal(0, (int)SamplePeriod.OneSecond);
        Assert.Equal(1, (int)SamplePeriod.HundredMilliseconds);
        Assert.Equal(2, (int)SamplePeriod.TenMilliseconds);
    }

    [Fact]
    public void LoadingTheSameDllAgainIsHarmlessAndAnotherIsRefused()
    {
        Library.Load(Env.DllPath());   // the same path again: nothing happens

        string copy = Path.Combine(Path.GetTempPath(), "tmdq-load-" + Guid.NewGuid().ToString("N") + ".dll");
        File.Copy(Env.DllPath(), copy);
        try
        {
            InvalidOperationException e = Assert.Throws<InvalidOperationException>(() => Library.Load(copy));
            Assert.Contains("already loaded", e.Message);
        }
        finally
        {
            File.Delete(copy);
        }
    }

    [Fact]
    public void LoadingAFileThatIsNotThereIsAClearError()
    {
        string missing = Path.Combine(Path.GetTempPath(), "tmdq-no-such-" + Guid.NewGuid().ToString("N") + ".dll");
        Assert.Throws<FileNotFoundException>(() => Library.Load(missing));
        Assert.Throws<ArgumentException>(() => Library.Load(""));
    }

    [Fact]
    public void AnErrorFromTheDllBecomesAnExceptionCarryingItsMessage()
    {
        TmdqException e = Assert.Throws<TmdqException>(() => Ch10File.Open(Path.Combine(Path.GetTempPath(), "tmdq-no-such.ch10")));
        Assert.Equal(Status.Prepare, e.Status);
        Assert.False(string.IsNullOrWhiteSpace(e.Message));
    }
}
