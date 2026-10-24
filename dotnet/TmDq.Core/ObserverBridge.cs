using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;

namespace TmDq.Core;

/// <summary>
/// What a native run (processing or calibration) calls back into: the static, unmanaged-callable functions the
/// library is given, a <see cref="GCHandle"/> to find the observer again, and the first exception the observer
/// threw. An exception must never unwind a native thread, so each callback catches and keeps it.
/// </summary>
internal sealed unsafe class ObserverBridge : IDisposable
{
    private readonly Context _state;
    private GCHandle _handle;

    public ObserverBridge(IRunObserver? observer)
    {
        _state = new Context(observer);
        _handle = GCHandle.Alloc(_state);
    }

    /// <summary>The first exception an observer method threw, or null.</summary>
    public Exception? ObserverException => _state.ObserverException;

    /// <summary>
    /// Fills every field of <paramref name="callbacks"/> and returns a pointer to it, or null when there is no
    /// observer (the library then reports to nobody). The struct must stay where it is until the native call has
    /// returned: the library copies it.
    /// </summary>
    public CallbacksNative* Fill(CallbacksNative* callbacks, bool hasObserver)
    {
        if (!hasObserver)
        {
            return null;
        }

        callbacks->StructSize = (uint)sizeof(CallbacksNative);
        callbacks->User = (void*)GCHandle.ToIntPtr(_handle);
        callbacks->Log = &OnLog;
        callbacks->Error = &OnError;
        callbacks->Progress = &OnProgress;
        callbacks->StreamDone = &OnStreamDone;
        callbacks->Finished = &OnFinished;
        return callbacks;
    }

    /// <summary>Releases the handle. Only once no native thread can call back (the run was freed).</summary>
    public void Dispose()
    {
        if (_handle.IsAllocated)
        {
            _handle.Free();
        }
    }

    private sealed class Context
    {
        private Exception? _observerException;

        public Context(IRunObserver? observer)
        {
            Observer = observer;
        }

        public IRunObserver? Observer { get; }

        public Exception? ObserverException => Volatile.Read(ref _observerException);

        public void Remember(Exception e) => Interlocked.CompareExchange(ref _observerException, e, null);
    }

    private static Context? From(void* user) => user is null ? null : GCHandle.FromIntPtr((IntPtr)user).Target as Context;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void OnLog(void* user, byte* message)
    {
        Context? c = From(user);
        try { c?.Observer?.OnLog(Text.Read(message)); } catch (Exception e) { c?.Remember(e); }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void OnError(void* user, byte* message)
    {
        Context? c = From(user);
        try { c?.Observer?.OnError(Text.Read(message)); } catch (Exception e) { c?.Remember(e); }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void OnProgress(void* user, int percent)
    {
        Context? c = From(user);
        try { c?.Observer?.OnProgress(percent); } catch (Exception e) { c?.Remember(e); }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void OnStreamDone(void* user, void* result)
    {
        Context? c = From(user);
        // Ownership of the native result has passed to this callback: it is wrapped first, so it is released
        // whatever happens next.
        StreamResult? wrapped = null;
        try
        {
            wrapped = new StreamResult((IntPtr)result);
            c?.Observer?.OnStreamCompleted(wrapped);
        }
        catch (Exception e)
        {
            c?.Remember(e);
            if (wrapped is null)
            {
                Native.tmdq_stream_result_free((IntPtr)result);
            }
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void OnFinished(void* user, int success)
    {
        Context? c = From(user);
        try { c?.Observer?.OnFinished(success != 0); } catch (Exception e) { c?.Remember(e); }
    }
}
