using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

namespace TmDq.Core;

/// <summary>A call into tmdq_core.dll failed. Carries the library's own status and its message.</summary>
public sealed class TmdqException : Exception
{
    public TmdqException(Status status, string message) : base(message)
    {
        Status = status;
    }

    /// <summary>What the library returned.</summary>
    public Status Status { get; }
}

/// <summary>
/// Loads tmdq_core.dll and checks that it is the version this binding was written for. Nothing else in
/// TmDq.Core works until <see cref="Load"/> has succeeded.
/// </summary>
public static unsafe class Library
{
    /// <summary>The ABI version this binding speaks (TMDQ_ABI_VERSION).</summary>
    public const int ExpectedVersion = 2;

    /// <summary>The lowest revision of that version this binding needs (TMDQ_ABI_REVISION).</summary>
    public const int MinimumRevision = 0;

    private static readonly object s_lock = new();
    private static string? s_path;
    private static string? s_candidate;
    private static bool s_resolverInstalled;

    /// <summary>The full path of the DLL that is loaded, or null if none is.</summary>
    public static string? LoadedPath
    {
        get
        {
            lock (s_lock)
            {
                return s_path;
            }
        }
    }

    /// <summary>The DLL's ABI version. Loads nothing: <see cref="Load"/> must have succeeded.</summary>
    public static int AbiVersion
    {
        get
        {
            RequireLoaded();
            return Native.tmdq_abi_version();
        }
    }

    /// <summary>The DLL's ABI revision within its version.</summary>
    public static int AbiRevision
    {
        get
        {
            RequireLoaded();
            return Native.tmdq_abi_revision();
        }
    }

    /// <summary>
    /// Loads the DLL at <paramref name="dllPath"/> and verifies it: the ABI version and revision, and that
    /// every struct this binding mirrors has the size the DLL reports. A second call with the same path does
    /// nothing; with another path it throws, because P/Invoke binds to one module for the life of the process.
    /// A call that fails because the file is not a loadable library leaves nothing behind, so another path may
    /// be tried; one that fails because the DLL loads but does not match this binding cannot be undone (the
    /// runtime keeps the module it bound), and the process should report it and exit.
    /// </summary>
    /// <exception cref="FileNotFoundException">There is no file at the path.</exception>
    /// <exception cref="InvalidOperationException">A different DLL is already loaded, or the DLL does not match this binding.</exception>
    public static void Load(string dllPath)
    {
        ArgumentException.ThrowIfNullOrEmpty(dllPath);
        string full = Path.GetFullPath(dllPath);
        if (!File.Exists(full))
        {
            throw new FileNotFoundException("tmdq_core.dll was not found", full);
        }

        lock (s_lock)
        {
            if (s_path is not null)
            {
                if (!string.Equals(s_path, full, StringComparison.OrdinalIgnoreCase))
                {
                    throw new InvalidOperationException(
                        $"tmdq_core.dll is already loaded from '{s_path}'; it cannot also be loaded from '{full}'");
                }
                return;
            }

            // DllImport("tmdq_core") resolves by bare name through the OS search order, which need not include the
            // folder the DLL is in. Resolving it to the full path is both the fix and the point: one module, known.
            // The runtime allows one resolver per assembly, so it is installed once and reads the candidate path.
            s_candidate = full;
            if (!s_resolverInstalled)
            {
                NativeLibrary.SetDllImportResolver(typeof(Library).Assembly,
                    (name, _, _) => name == "tmdq_core" && s_candidate is not null ? NativeLibrary.Load(s_candidate) : IntPtr.Zero);
                s_resolverInstalled = true;
            }

            Verify(full);
            s_path = full;
        }
    }

    private static void Verify(string full)
    {
        int version;
        int revision;
        try
        {
            version = Native.tmdq_abi_version();
            revision = Native.tmdq_abi_revision();
        }
        catch (Exception e) when (e is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException)
        {
            throw new InvalidOperationException($"'{full}' is not a usable tmdq_core.dll: {e.Message}", e);
        }

        CheckVersion(full, version, revision);
        CheckLayouts(Mirrored, Native.tmdq_sizeof);
    }

    /// <summary>Throws unless a DLL that reports this version and revision is one this binding can speak to.</summary>
    internal static void CheckVersion(string full, int version, int revision)
    {
        if (version != ExpectedVersion || revision < MinimumRevision)
        {
            throw new InvalidOperationException(
                $"'{full}' speaks ABI version {version}.{revision}; this binding needs version {ExpectedVersion}, revision {MinimumRevision} or later");
        }
    }

    /// <summary>Throws unless every mirrored struct is exactly as large as the native side says its twin is.</summary>
    internal static void CheckLayouts(IEnumerable<(StructId Id, Type Type)> mirrored, Func<int, int> nativeSize)
    {
        foreach ((StructId id, Type type) in mirrored)
        {
            int managed = Marshal.SizeOf(type);
            int native = nativeSize((int)id);
            if (managed != native)
            {
                throw new InvalidOperationException(
                    $"struct layout mismatch for {id}: managed {managed} bytes, native {native} bytes");
            }
        }
    }

    /// <summary>The structs this binding mirrors, with the id the DLL knows each by.</summary>
    internal static readonly (StructId Id, Type Type)[] Mirrored =
    {
        (StructId.ChannelInfo, typeof(ChannelInfoNative)),
        (StructId.CalPoint, typeof(CalPointNative)),
        (StructId.CalProfile, typeof(CalProfileNative)),
        (StructId.StreamConfig, typeof(StreamConfigNative)),
        (StructId.RunRequest, typeof(RunRequestNative)),
        (StructId.Callbacks, typeof(CallbacksNative)),
        (StructId.StreamInfo, typeof(StreamInfoNative)),
        (StructId.ChannelSeries, typeof(ChannelSeriesNative)),
        (StructId.CalibrationRequest, typeof(CalibrationRequestNative)),
        (StructId.CalibrationInfo, typeof(CalibrationInfoNative)),
        (StructId.CalibrationChannel, typeof(CalibrationChannelNative)),
        (StructId.FrameSyncFileInfo, typeof(FrameSyncFileInfoNative)),
        (StructId.MatchInfo, typeof(MatchInfoNative)),
        (StructId.CsvInfo, typeof(CsvInfoNative)),
        (StructId.CsvSeriesInfo, typeof(CsvSeriesInfoNative)),
        (StructId.CsvColumn, typeof(CsvColumnNative)),
        (StructId.SeriesSetInfo, typeof(SeriesSetInfoNative)),
        (StructId.SeriesSpec, typeof(SeriesSpecNative)),
        (StructId.StreamKey, typeof(StreamKeyNative)),
        (StructId.TimeBaseInfo, typeof(TimeBaseInfoNative)),
        (StructId.ImportedSeries, typeof(ImportedSeriesNative)),
    };

    internal static void RequireLoaded()
    {
        if (LoadedPath is null)
        {
            throw new InvalidOperationException("tmdq_core.dll is not loaded: call Library.Load(path) first");
        }
    }

    /// <summary>The calling thread's last native error, or an empty string.</summary>
    internal static string LastError()
    {
        byte* p = Native.tmdq_last_error();
        return p is null ? string.Empty : Marshal.PtrToStringUTF8((IntPtr)p) ?? string.Empty;
    }

    /// <summary>Turns a non-OK status into a <see cref="TmdqException"/> carrying the library's message.</summary>
    internal static void Check(int status)
    {
        if (status != (int)Status.Ok)
        {
            throw new TmdqException((Status)status, LastError());
        }
    }
}
