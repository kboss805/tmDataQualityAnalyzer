using System;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;

namespace TmDq.Core;

/// <summary>
/// What a native run (processing or calibration) calls back into: the static, unmanaged-callable functions the
/// library is given, a <see cref="GCHandle"/> to find the observer again, and the first exception the observer
/// threw. An exception must never unwind a native thread, so each callback catches and keeps it.
/// </summary>
[SuppressMessage("Design", "CA1031:Do not catch general exception types",
    Justification = "An exception must never unwind a native thread: each callback catches everything the observer throws and keeps the first (Context.Remember).")]
internal sealed unsafe class ObserverBridge : IDisposable
{
    /// <summary>The context of the callback this thread is inside, if it is inside one: what <see cref="IsDelivering"/> asks.</summary>
    [ThreadStatic]
    private static Context? t_delivering;

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
    /// True on a thread that is inside one of this bridge's callbacks: a native thread of the run is on it, and cannot be
    /// waited for from it. As the library's own check (<c>CallbackEvents::delivering</c>), one for each run.
    /// </summary>
    public bool IsDelivering => ReferenceEquals(t_delivering, _state);

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

    /// <summary>Marks this thread as inside <paramref name="context"/>'s callback; returns what to put back when it returns.</summary>
    private static Context? Enter(Context? context)
    {
        Context? outer = t_delivering;
        t_delivering = context;
        return outer;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void OnLog(void* user, byte* message)
    {
        Context? c = From(user);
        Context? outer = Enter(c);
        try { c?.Observer?.OnLog(Text.Read(message)); } catch (Exception e) { c?.Remember(e); } finally { t_delivering = outer; }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void OnError(void* user, byte* message)
    {
        Context? c = From(user);
        Context? outer = Enter(c);
        try { c?.Observer?.OnError(Text.Read(message)); } catch (Exception e) { c?.Remember(e); } finally { t_delivering = outer; }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void OnProgress(void* user, int percent)
    {
        Context? c = From(user);
        Context? outer = Enter(c);
        try { c?.Observer?.OnProgress(percent); } catch (Exception e) { c?.Remember(e); } finally { t_delivering = outer; }
    }

    [SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope",
        Justification = "The wrapper takes ownership of the native result and passes it to the observer, which disposes it (or its handle's finalizer does); a wrapper that could not be made released the result itself.")]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void OnStreamDone(void* user, void* result)
    {
        Context? c = From(user);
        Context? outer = Enter(c);
        // Ownership of the native result has passed to this callback: it is wrapped first, so it is released
        // whatever happens next (the wrapper releases it itself if it cannot be made).
        try
        {
            var wrapped = new StreamResult((IntPtr)result);
            c?.Observer?.OnStreamCompleted(wrapped);
        }
        catch (Exception e)
        {
            c?.Remember(e);   // a result that could not be wrapped was released by its constructor
        }
        finally
        {
            t_delivering = outer;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void OnFinished(void* user, int success)
    {
        Context? c = From(user);
        Context? outer = Enter(c);
        try { c?.Observer?.OnFinished(success != 0); } catch (Exception e) { c?.Remember(e); } finally { t_delivering = outer; }
    }
}
