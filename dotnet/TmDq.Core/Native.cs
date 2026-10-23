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
}
