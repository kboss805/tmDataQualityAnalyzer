using System.Runtime.InteropServices;

namespace TmDq.Core;

// The ABI in tmdq_abi.h, declared for the CLR. Everything here is internal: the public surface is the
// wrappers beside it. Each struct mirrors its C twin field for field with Pack = 8, and Library.Load checks
// every size against the DLL's own tmdq_sizeof, because a wrong offset reads plausible garbage rather than
// failing. TmDq.Core.Tests pins every offset to the numbers the native tests pin.

/// <summary>The numbers of tmdq_struct_id, for tmdq_sizeof.</summary>
internal enum StructId
{
    ChannelInfo = 1,
    CalPoint = 2,
    CalProfile = 3,
    StreamConfig = 4,
    RunRequest = 5,
    Callbacks = 6,
    StreamInfo = 7,
    ChannelSeries = 8,
    CalibrationRequest = 9,
    CalibrationInfo = 10,
    CalibrationChannel = 11,
    FrameSyncFileInfo = 12,
    MatchInfo = 13,
    CsvInfo = 14,
    CsvSeriesInfo = 15,
    CsvColumn = 16,
    SeriesSetInfo = 17,
    SeriesSpec = 18,
    StreamKey = 19,
    TimeBaseInfo = 20,
    ImportedSeries = 21,
    ReceiverParams = 22,
    ChartView = 23,
    GeometryInfo = 24,
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct ChannelInfoNative
{
    public uint StructSize;
    public int ChannelId;
    public double TmatsDataRateBps;
    public byte* NameUtf8;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct CalPointNative
{
    public double RawAvg;
    public double TrueDb;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct CalProfileNative
{
    public CalPointNative* Points;
    public int WordIndex;
    public int PointCount;
    public int Valid;
    public int Reserved;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct StreamConfigNative
{
    public uint StructSize;
    public int PcmChannelId;
    public byte* LabelUtf8;
    public byte* SyncPatternUtf8;
    public byte* SyncMaskUtf8;
    public byte* ReceiverParamsTomlUtf8;
    public CalProfileNative* Calibration;
    public double DataRateMbps;
    public double ScaleDdBPerV;
    public int Mode;
    public int BitsInMinorFrame;
    public int Randomized;
    public int Inverted;
    public int SamplePeriodIndex;
    public int PolarityIndex;
    public int SlopeIndex;
    public int NumReceivers;
    public int ReceiverChannels;
    public int CalibrationCount;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct RunRequestNative
{
    public uint StructSize;
    public int TimeChannelId;
    public byte* FileNameUtf8;
    public byte* AppRootUtf8;
    public StreamConfigNative** Streams;
    public int StreamCount;
    public int SourceId;
    public int SwapBytes;
    public int Reserved;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct CallbacksNative
{
    public uint StructSize;
    public void* User;
    public delegate* unmanaged[Cdecl]<void*, byte*, void> Log;
    public delegate* unmanaged[Cdecl]<void*, byte*, void> Error;
    public delegate* unmanaged[Cdecl]<void*, int, void> Progress;
    public delegate* unmanaged[Cdecl]<void*, void*, void> StreamDone;
    public delegate* unmanaged[Cdecl]<void*, int, void> Finished;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct StreamInfoNative
{
    public uint StructSize;
    public int PcmChannelId;
    public byte* LabelUtf8;
    public double* TimesSec;
    public double* LockPercent;
    public double* MissedFrames;
    public int JobIndex;
    public int SourceId;
    public int Mode;
    public int SampleCount;
    public int ChannelCount;
    public int Reserved;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct ChannelSeriesNative
{
    public uint StructSize;
    public int WordIndex;
    public byte* NameUtf8;
    public double* Values;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct CalibrationRequestNative
{
    public uint StructSize;
    public int TimeChannelId;
    public byte* CalFileNameUtf8;
    public byte* AppRootUtf8;
    public byte* SyncPatternUtf8;
    public byte* SyncMaskUtf8;
    public byte* ReceiverParamsTomlUtf8;
    public double* StepDb;
    public double DataRateMbps;
    public double ClipStartSec;
    public double ClipEndSec;
    public int PcmChannelId;
    public int BitsInMinorFrame;
    public int Randomized;
    public int Inverted;
    public int SwapBytes;
    public int NumReceivers;
    public int ReceiverChannels;
    public int StepCount;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct CalibrationInfoNative
{
    public uint StructSize;
    public int ChannelCount;
    public byte* SummaryUtf8;
    public byte* ErrorUtf8;
    public byte* ReportUtf8;
    public double RecordingSeconds;
    public int ClipIgnored;
    public int Reserved;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct CalibrationChannelNative
{
    public uint StructSize;
    public int WordIndex;
    public byte* NameUtf8;
    public CalPointNative* Points;
    public double* PlateauLevels;
    public double* UnresolvedDb;
    public double EdgeThreshold;
    public int PointCount;
    public int PlateauLevelCount;
    public int UnresolvedCount;
    public int Valid;
    public int HadData;
    public int Outcome;
    public int DetectedPlateaus;
    public int ExtraPlateaus;
    public int ThresholdRelaxed;
    public int Saturated;
    public int SweepLevels;
    public int Reserved;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct FrameSyncFileInfoNative
{
    public uint StructSize;
    public int HasBitsPerFrame;
    public int BitsPerFrame;
    public int BitsFromLegacyKey;
    public int Inverted;
    public int HasRandomized;
    public int Randomized;
    public int Reserved;
    public byte* PatternUtf8;
    public byte* MaskUtf8;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct ReceiverParamsNative
{
    public uint StructSize;
    public int PolarityIndex;
    public int SlopeIndex;
    public int NumReceivers;
    public int ReceiverChannels;
    public int Reserved;
    public double ScaleDdBPerV;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct ChartViewNative
{
    public uint StructSize;
    public int Reserved;
    public double XLower;
    public double XUpper;
    public double YLower;
    public double YUpper;
    public double Left;
    public double Top;
    public double Width;
    public double Height;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct GeometryInfoNative
{
    public uint StructSize;
    public int SegmentCount;
    public double* Segments;
    public int BandCount;
    public int Decimated;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct MatchInfoNative
{
    public uint StructSize;
    public int Ok;
    public int MissingCount;
    public int ExtraCount;
    public int* Missing;
    public int* Extra;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct CsvInfoNative
{
    public uint StructSize;
    public int SeriesCount;
    public int BaseDay;
    public int SkippedRows;
    public double BaseTimeOffset;
    public double XMax;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct CsvSeriesInfoNative
{
    public uint StructSize;
    public int MetricType;
    public byte* NameUtf8;
    public byte* StreamLabelUtf8;
    public double* XValues;
    public double* YValues;
    public double YMin;
    public double YMax;
    public int ReceiverIndex;
    public int ChannelIndex;
    public int StreamOrder;
    public int SourceId;
    public int PointCount;
    public int Reserved;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct CsvColumnNative
{
    public uint StructSize;
    public int MetricType;
    public byte* NameUtf8;
    public double* XValues;
    public double* YValues;
    public int SourceId;
    public int XCount;
    public int YCount;
    public int Reserved;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct SeriesSetInfoNative
{
    public uint StructSize;
    public int SeriesCount;
    public int ElapsedCount;
    public int Reserved;
    public double* Elapsed;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct SeriesSpecNative
{
    public uint StructSize;
    public int MetricType;
    public byte* NameUtf8;
    public byte* StreamLabelUtf8;
    public double* YValues;
    public double YMin;
    public double YMax;
    public int ReceiverIndex;
    public int ChannelIndex;
    public int StreamOrder;
    public int StreamSequence;
    public int SourceId;
    public int YCount;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct StreamKeyNative
{
    public uint StructSize;
    public int SourceId;
    public byte* LabelUtf8;
    public int Channel;
    public int Reserved;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct TimeBaseInfoNative
{
    public uint StructSize;
    public int BaseDay;
    public double BaseAbsSeconds;
    public double BaseTimeOffset;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct ImportedSeriesNative
{
    public uint StructSize;
    public int MetricType;
    public byte* StreamLabelUtf8;
    public int SourceId;
    public int StreamOrder;
}
