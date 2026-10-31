using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace TmDq.Core;

// The ABI in tmdq_abi.h, declared for the CLR. Everything here is internal: the public surface is the
// wrappers beside it. Each struct mirrors its C twin field for field with Pack = 8, and Library.Load checks
// every size against the DLL's own tmdq_sizeof, because a wrong offset reads plausible garbage rather than
// failing. TmDq.Core.Tests pins every offset to the numbers the native tests pin.

/// <summary>The numbers of tmdq_status.</summary>
public enum Status
{
    Ok = 0,
    BadArgument = 1,
    Prepare = 2,
    Process = 3,
    NoSamples = 4,
    Internal = 5,
    Job = 6,
    State = 7,
    Io = 8,
}

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
    public double ScaleDbPerV;
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

internal static unsafe partial class Native
{
    private const string Dll = "tmdq_core";

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_abi_version();

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_abi_revision();

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_sizeof(int which);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial byte* tmdq_last_error();

    // --- the file
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_file_open(byte* pathUtf8, out IntPtr file);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_file_close(IntPtr file);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_file_time_channel_count(IntPtr file);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_file_pcm_channel_count(IntPtr file);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_file_default_time_channel(IntPtr file);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_file_time_channel(IntPtr file, int index, ChannelInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_file_pcm_channel(IntPtr file, int index, ChannelInfoNative* info);

    // --- runs
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_run_start(RunRequestNative* request, CallbacksNative* callbacks, out IntPtr run);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_run_cancel(IntPtr run);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_run_running(IntPtr run);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_run_wait(IntPtr run);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_run_free(IntPtr run);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_stream_result_info(IntPtr result, StreamInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_stream_result_channel(IntPtr result, int index, ChannelSeriesNative* series);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_stream_result_free(IntPtr result);

    // --- step files and calibration
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_steps_parse(byte* pathUtf8, out IntPtr steps);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_steps_count(IntPtr steps);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial double* tmdq_steps_db(IntPtr steps);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_steps_free(IntPtr steps);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_step_config_initial_path(byte* appRootUtf8, byte* preferredUtf8, byte* buffer, int capacity);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_calibration_start(CalibrationRequestNative* request, CallbacksNative* callbacks, out IntPtr calibration);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_calibration_cancel(IntPtr calibration);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_calibration_running(IntPtr calibration);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_calibration_wait(IntPtr calibration);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_calibration_free(IntPtr calibration);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_calibration_result(IntPtr calibration, CalibrationInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_calibration_channel_result(IntPtr calibration, int index, CalibrationChannelNative* channel);

    // --- the formats
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_frame_sync_file_read(byte* pathUtf8, out IntPtr file);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_frame_sync_file_info_get(IntPtr file, FrameSyncFileInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_frame_sync_file_free(IntPtr file);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_frame_sync_file_write(byte* pathUtf8, byte* patternUtf8, byte* maskUtf8, int bitsPerFrame, int inverted, int randomized);
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_receiver_params_read(byte* pathUtf8, ReceiverParamsNative* defaults, ReceiverParamsNative* result);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_receiver_params_save(byte* pathUtf8, ReceiverParamsNative* parameters, byte* wordMapSourceUtf8, int numWordsInMinorFrame);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_words_in_minor_frame(int bitsInMinorFrame);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_template_match(int* templateIds, int templateCount, int* fileIds, int fileCount, out IntPtr match);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_match_info_get(IntPtr match, MatchInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_match_free(IntPtr match);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_csv_parse(byte* pathUtf8, out IntPtr csv);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_csv_info_get(IntPtr csv, CsvInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_csv_series_get(IntPtr csv, int index, CsvSeriesInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_csv_free(IntPtr csv);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_csv_write(byte* pathUtf8, CsvColumnNative** columns, int count, int baseDay, double baseTimeOffset);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_clock_time_format(int baseDay, double baseTimeOffset, double elapsed, int includeMilliseconds, byte* buffer, int capacity);

    // --- series construction
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_stream_result_series(IntPtr result, double baseAbsSeconds, out IntPtr set);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_series_set_info_get(IntPtr set, SeriesSetInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_series_set_spec(IntPtr set, int index, SeriesSpecNative* spec);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_series_set_free(IntPtr set);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_stream_result_key(IntPtr result, StreamKeyNative* key);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_stream_key_equal(StreamKeyNative* a, StreamKeyNative* b);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_time_base_create(out IntPtr timeBase);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_time_base_free(IntPtr timeBase);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_time_base_info_get(IntPtr timeBase, TimeBaseInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_time_base_reset(IntPtr timeBase);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_time_base_set_clock(IntPtr timeBase, int day, double offset);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_time_base_overlaps(IntPtr timeBase, double newMinAbs, double newMaxAbs, double existingMaxElapsed, out int overlaps);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_time_base_adopt(IntPtr timeBase, double firstAbsSeconds, int hasSeries, out double shift);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_time_base_advance(IntPtr timeBase, int hasEarliest, double earliestElapsed, out double shift);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_assign_imported_stream_orders(ImportedSeriesNative** series, int count);
}
