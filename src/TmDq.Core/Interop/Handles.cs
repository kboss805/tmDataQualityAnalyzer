using System;
using Microsoft.Win32.SafeHandles;

namespace TmDq.Core;

// One class for each kind of native handle, so that a handle of one kind cannot be passed to a function of another,
// and so that each is released exactly once: when it is disposed (or finalized) and the last native call using it
// has returned. A call that races a Dispose therefore never reaches a freed handle, and a call after it throws
// ObjectDisposedException instead of passing a dangling pointer. Native.cs takes these in place of IntPtr (the
// marshaller holds the handle open for the length of the call); only the free functions, which run in ReleaseHandle,
// take the raw value. The public parameterless constructors are the marshaller's, for a handle a function returns.

/// <summary>What every handle has in common: it is invalid when zero (or -1), and owned.</summary>
internal abstract class NativeHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    protected NativeHandle() : base(ownsHandle: true)
    {
    }

    protected NativeHandle(IntPtr handle) : this() => SetHandle(handle);
}

/// <summary>A processing run in flight. Releasing it cancels and waits, then frees the callbacks' context.</summary>
internal sealed class RunHandle : NativeHandle
{
    public RunHandle()
    {
    }

    /// <summary>What the run calls back into; freed with the run, never before the native threads are gone.</summary>
    internal ObserverBridge? Bridge { get; set; }

    protected override bool ReleaseHandle()
    {
        Native.tmdq_run_free(handle);   // cancels and waits, so no native thread can still hold the context
        Bridge?.Dispose();
        return true;
    }
}

/// <summary>A calibration extraction in flight. Releasing it cancels and waits, then frees the callbacks' context.</summary>
internal sealed class CalibrationRunHandle : NativeHandle
{
    public CalibrationRunHandle()
    {
    }

    /// <summary>What the extraction calls back into; freed with it, never before the native threads are gone.</summary>
    internal ObserverBridge? Bridge { get; set; }

    protected override bool ReleaseHandle()
    {
        Native.tmdq_calibration_free(handle);   // cancels and waits, so no native thread can still call back
        Bridge?.Dispose();
        return true;
    }
}

/// <summary>A stream's processed data.</summary>
internal sealed class StreamResultHandle : NativeHandle
{
    public StreamResultHandle()
    {
    }

    public StreamResultHandle(IntPtr handle) : base(handle)
    {
    }

    protected override bool ReleaseHandle()
    {
        Native.tmdq_stream_result_free(handle);
        return true;
    }
}

/// <summary>A stream's series on the shared time axis.</summary>
internal sealed class SeriesSetHandle : NativeHandle
{
    public SeriesSetHandle()
    {
    }

    protected override bool ReleaseHandle()
    {
        Native.tmdq_series_set_free(handle);
        return true;
    }
}

/// <summary>A series' envelope index.</summary>
internal sealed class SeriesIndexHandle : NativeHandle
{
    /// <summary>No index: what a geometry is asked for when it is to be walked, not looked up.</summary>
    internal static readonly SeriesIndexHandle None = new();

    public SeriesIndexHandle()
    {
    }

    protected override bool ReleaseHandle()
    {
        Native.tmdq_series_index_free(handle);
        return true;
    }
}

/// <summary>The clock the shared elapsed axis hangs from.</summary>
internal sealed class TimeBaseHandle : NativeHandle
{
    public TimeBaseHandle()
    {
    }

    protected override bool ReleaseHandle()
    {
        Native.tmdq_time_base_free(handle);
        return true;
    }
}
