using System;
using System.Threading;
using Microsoft.Win32.SafeHandles;

namespace TmDq.Core;

// One class for each kind of native handle, so that a handle of one kind cannot be passed to a function of another,
// and so that each is released exactly once: when it is disposed (or finalized) and the last hold on it has gone.
// Native.cs takes these in place of IntPtr, and the marshaller holds the handle open for the length of the call, so a
// call that races a Dispose never passes a freed handle, and a call after it throws ObjectDisposedException instead.
// That hold ends when the call returns, though, and several calls return pointers into the native object (a label, a
// result's samples) that are read only afterwards: a Dispose on another thread in between would free them under the
// read. Such a call and its copy are made under a Lease, which holds the handle open until the copy is done. A span the
// binding hands out over native memory is no copy, and no lease can cover it: it is valid until the Dispose.
// Only the free functions, which run in ReleaseHandle, take the raw value. The public parameterless constructors are
// the marshaller's, for a handle a function returns.

/// <summary>What every handle has in common: it is invalid when zero (or -1), and owned.</summary>
internal abstract class NativeHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    protected NativeHandle() : base(ownsHandle: true)
    {
    }

    protected NativeHandle(IntPtr handle) : this() => SetHandle(handle);

    /// <summary>
    /// Holds the handle open until the lease is disposed, as the marshaller does for the length of one call: a Dispose
    /// meanwhile is carried out when the lease ends. For a call that returns pointers into the native object together
    /// with the copy of what they point at, both of which must happen before it can be freed.
    /// </summary>
    /// <exception cref="ObjectDisposedException">The handle has been released.</exception>
    public HandleLease Lease() => new(this);
}

/// <summary>A hold on a <see cref="NativeHandle"/>, taken by <see cref="NativeHandle.Lease"/> and given back by
/// <see cref="Dispose"/> (a <c>using</c>). A ref struct, so that it cannot be stored, boxed or carried across an await.</summary>
internal readonly ref struct HandleLease
{
    private readonly NativeHandle? _handle;

    internal HandleLease(NativeHandle handle)
    {
        bool added = false;
        handle.DangerousAddRef(ref added);   // throws ObjectDisposedException once the handle is released
        _handle = added ? handle : null;
    }

    public void Dispose() => _handle?.DangerousRelease();
}

/// <summary>
/// What a run and an extraction have in common: they call back from native threads, so releasing one cancels and waits
/// for those threads, and only then frees what they call back into. The native library refuses that wait on a thread it
/// is calling back on (the thread would wait for itself), so a release asked for from one of the observer's own
/// callbacks, which the observer may well ask for from OnFinished, is carried out from the thread pool, once the
/// callback has returned. Freeing the context at once instead would leave the run alive, calling back into a freed
/// <see cref="System.Runtime.InteropServices.GCHandle"/>: a crash, in a native thread no one can catch it on.
/// </summary>
internal abstract class CallbackRunHandle : NativeHandle
{
    protected CallbackRunHandle()
    {
    }

    /// <summary>What the run calls back into; freed with the run, never before the native threads are gone.</summary>
    internal ObserverBridge? Bridge { get; set; }

    /// <summary>The native free: cancels, waits for every thread, and releases the run.</summary>
    protected abstract void Free(IntPtr native);

    protected override bool ReleaseHandle()
    {
        IntPtr native = handle;
        ObserverBridge? bridge = Bridge;
        if (bridge is { IsDelivering: true })
        {
            ThreadPool.UnsafeQueueUserWorkItem(_ => Release(native, bridge), null);
            return true;
        }

        Release(native, bridge);
        return true;
    }

    private void Release(IntPtr native, ObserverBridge? bridge)
    {
        Free(native);       // so that no native thread can still hold the context...
        bridge?.Dispose();  // ...which is freed only now
    }
}

/// <summary>A processing run in flight. Releasing it cancels and waits, then frees the callbacks' context.</summary>
internal sealed class RunHandle : CallbackRunHandle
{
    public RunHandle()
    {
    }

    protected override void Free(IntPtr native) => Native.tmdq_run_free(native);
}

/// <summary>A calibration extraction in flight. Releasing it cancels and waits, then frees the callbacks' context.</summary>
internal sealed class CalibrationRunHandle : CallbackRunHandle
{
    public CalibrationRunHandle()
    {
    }

    protected override void Free(IntPtr native) => Native.tmdq_calibration_free(native);
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
