using System;
using System.Diagnostics.CodeAnalysis;
using System.Threading;

namespace TmDq.Core.Tests;

/// <summary>
/// An observer that disposes its own run from inside a callback: from the first progress report, and from the end if
/// the run had none. What an application's OnFinished handler may do ("I am done with this run"), and what the native
/// library refuses a free of from that thread; the run must still be released, and must still report to the end.
/// </summary>
[SuppressMessage("Design", "CA1001:Types that own disposable fields should be disposable",
    Justification = "The event's wait handle is made only if a caller asks for it, and none does: it is collected with the observer.")]
internal sealed class SelfDisposingObserver : IRunObserver
{
    private static readonly TimeSpan Timeout = TimeSpan.FromMinutes(3);

    private readonly ManualResetEventSlim _assigned = new();
    private readonly ManualResetEventSlim _finished = new();
    private IDisposable? _run;
    private int _disposals;
    private int _finishedAfterDispose;
    private int _success = -1;

    /// <summary>The run this observer disposes. Assigned once the run has started, so a callback may have to wait for it.</summary>
    public IDisposable Run
    {
        set
        {
            _run = value;
            _assigned.Set();
        }
    }

    /// <summary>1 when the end was reported after the run had been disposed, which the old release made a crash.</summary>
    public int FinishedAfterDispose => Volatile.Read(ref _finishedAfterDispose);

    /// <summary>What the run's end said: null before it.</summary>
    public bool? Success => Volatile.Read(ref _success) switch { -1 => null, 0 => false, _ => true };

    public bool WaitFinished() => _finished.Wait(Timeout);

    public void OnProgress(int percent) => DisposeOnce();

    public void OnFinished(bool success)
    {
        if (Volatile.Read(ref _disposals) > 0)
        {
            Interlocked.Increment(ref _finishedAfterDispose);
        }

        Volatile.Write(ref _success, success ? 1 : 0);
        _finished.Set();
        DisposeOnce();
    }

    private void DisposeOnce()
    {
        if (Interlocked.Increment(ref _disposals) != 1)
        {
            return;
        }

        if (!_assigned.Wait(Timeout))
        {
            throw new TimeoutException("the run was never given to the observer");
        }

        _run!.Dispose();
    }
}
