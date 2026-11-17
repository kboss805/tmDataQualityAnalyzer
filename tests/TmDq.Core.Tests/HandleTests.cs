using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// The native handles' lifetime (audit S8): each is released once, when it is disposed and the last call using it has
/// returned, so a call that races a Dispose never reaches a freed handle and a call after it is refused. What each
/// wrapper does after its Dispose (throw, or do nothing) is held by the suites of the types themselves.
/// </summary>
[Collection(ProcessMemoryCollection.Name)]
public class HandleTests
{
    private static readonly TimeSpan Timeout = TimeSpan.FromMinutes(3);

    public HandleTests()
    {
        Env.EnsureLoaded();
    }

    [Fact]
    public void ADisposeWhileACallHoldsTheHandleWaitsForTheCall()
    {
        double[] values = Enumerable.Range(0, 5000).Select(i => (double)i).ToArray();
        var index = new SeriesIndex(values);
        SeriesIndexHandle handle = index.Handle;

        bool held = false;
        handle.DangerousAddRef(ref held);   // a call in flight
        Assert.True(held);
        index.Dispose();
        index.Dispose();                    // twice is harmless
        Assert.False(handle.IsClosed);      // not released while a call holds it
        Assert.Equal(values.Length, Native.tmdq_series_index_count(handle));   // and still the index it was

        handle.DangerousRelease();          // the call returns
        Assert.True(handle.IsClosed);
        Assert.Throws<ObjectDisposedException>(() => Native.tmdq_series_index_count(handle));
        Assert.Throws<ObjectDisposedException>(() => index.Handle);
    }

    [Fact]
    public void ADisposedIndexGivesItsNativeMemoryBack()
    {
        // An index is about 1.5 bytes a value: forty over two million values is some 120 MB, if none were released.
        double[] values = Enumerable.Range(0, 2_000_000).Select(i => Math.Sin(i * 0.001)).ToArray();
        using (new SeriesIndex(values))
        {
        }

        long before = Process.GetCurrentProcess().PrivateMemorySize64;
        for (int i = 0; i < 40; i++)
        {
            using var index = new SeriesIndex(values);
            Assert.Equal(values.Length, index.Count);
        }
        long grown = Process.GetCurrentProcess().PrivateMemorySize64 - before;
        Assert.True(grown < 60L * 1024 * 1024, $"private memory grew by {grown / (1024 * 1024)} MiB over forty disposed indexes");
    }

    [Fact]
    public void AnIndexNobodyDisposedIsReleasedByItsHandlesFinalizer()
    {
        double[] values = Enumerable.Range(0, 2_000_000).Select(i => Math.Sin(i * 0.001)).ToArray();
        using (new SeriesIndex(values))
        {
        }

        long before = Process.GetCurrentProcess().PrivateMemorySize64;
        for (int i = 0; i < 40; i++)
        {
            Abandon(values);
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }
        long grown = Process.GetCurrentProcess().PrivateMemorySize64 - before;
        Assert.True(grown < 60L * 1024 * 1024, $"private memory grew by {grown / (1024 * 1024)} MiB over forty abandoned indexes");
    }

    private static void Abandon(double[] values) => _ = new SeriesIndex(values).Count;

    [Fact]
    public async Task DisposingARunWhileAnotherThreadWaitsOnItCancelsItInsteadOfWaitingForItsEnd()
    {
        var collector = new Collector();
        var request = new RunRequest(Env.Prn, 1, new[]
        {
            new StreamConfig { PcmChannelId = 13, Label = "Ch 13", SyncPattern = "334AABBF", BitsInMinorFrame = 32767 },
        })
        {
            AppRoot = Env.RepoRoot,
        };
        ProcessingRun run = ProcessingRun.Start(request, collector);
        Task waiter = Task.Run(run.Wait, TestContext.Current.CancellationToken);
        await Task.Delay(200, TestContext.Current.CancellationToken);   // the waiter is inside the native wait, holding the run open
        Assert.True(run.IsRunning);

        run.Dispose();                      // cancels first: a held handle would otherwise defer the release to the run's end
        await waiter.WaitAsync(Timeout, TestContext.Current.CancellationToken);
        Assert.True(collector.WaitFinished(Timeout));
        Assert.False(collector.Success);
        Assert.Equal("finished:0", collector.Events[^1]);
    }

    [Fact]
    public async Task ARunDisposedFromItsOwnCallbackStillReportsItsEndAndIsReleasedOnceTheCallbackHasReturned()
    {
        // The native free is refused on a thread the run is calling back on (it would wait for itself). The binding used to
        // free the callbacks' context anyway, leaving the run alive and calling back into a freed GCHandle: a crash.
        WeakReference observer = RunThatDisposesItself();
        await AssertReleased(observer);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference RunThatDisposesItself()
    {
        var observer = new SelfDisposingObserver();
        var request = new RunRequest(Env.Prn, 1, new[]
        {
            new StreamConfig { PcmChannelId = 13, Label = "Ch 13", SyncPattern = "334AABBF", BitsInMinorFrame = 32767 },
        })
        {
            AppRoot = Env.RepoRoot,
        };
        ProcessingRun run = ProcessingRun.Start(request, observer);
        observer.Run = run;
        Assert.True(observer.WaitFinished(), "the run did not finish after it was disposed from a callback");
        Assert.NotNull(observer.Success);
        Assert.Null(run.ObserverException);
        return new WeakReference(observer);
    }

    /// <summary>
    /// What a run's callbacks reach (the observer, through the context's GCHandle) is collectible only once that handle is
    /// freed, which the release does after the native free: so an observer that can be collected was released, however late.
    /// </summary>
    internal static async Task AssertReleased(WeakReference observer)
    {
        DateTime deadline = DateTime.UtcNow + Timeout;
        while (observer.IsAlive && DateTime.UtcNow < deadline)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
            await Task.Delay(100, TestContext.Current.CancellationToken);
        }

        Assert.False(observer.IsAlive, "the run's context was never released");
    }
}
