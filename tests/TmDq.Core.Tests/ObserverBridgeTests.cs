using System;
using System.Collections.Generic;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// <see cref="ObserverBridge.IsDelivering"/>: the binding's knowledge of which thread is inside a run's callback, which a
/// release asked for from there is deferred by (a free the native library would refuse). The callbacks are called here as
/// the library calls them, through the function pointers it is given.
/// </summary>
public unsafe class ObserverBridgeTests
{
    private sealed class Probe : IRunObserver
    {
        public ObserverBridge Bridge { get; set; } = null!;

        /// <summary>Another bridge, whose answer is recorded too.</summary>
        public ObserverBridge? Other { get; set; }

        /// <summary>Run inside the callback, between the two readings.</summary>
        public Action? Inside { get; set; }

        public bool Throwing { get; init; }

        public List<string> Seen { get; } = new();

        public void OnLog(string message) => See("log");

        public void OnError(string message) => See("error");

        public void OnProgress(int percent) => See("progress");

        public void OnFinished(bool success) => See("finished");

        private void See(string what)
        {
            Seen.Add($"{what}:{Bridge.IsDelivering}{(Other is null ? "" : "/other:" + Other.IsDelivering)}");
            Inside?.Invoke();
            Seen.Add($"{what}-after:{Bridge.IsDelivering}");
            if (Throwing)
            {
                throw new InvalidOperationException(what);
            }
        }
    }

    private static ObserverBridge BridgeOf(Probe probe, out CallbacksNative callbacks)
    {
        var bridge = new ObserverBridge(probe);
        probe.Bridge = bridge;
        CallbacksNative filled;
        bridge.Fill(&filled, true);
        callbacks = filled;
        return bridge;
    }

    private static void CallEvery(CallbacksNative c)
    {
        fixed (byte* text = "text\0"u8)
        {
            c.Log(c.User, text);
            c.Error(c.User, text);
        }

        c.Progress(c.User, 50);
        c.StreamDone(c.User, null);   // no result to hand over: the observer is not reached, the marker still is
        c.Finished(c.User, 1);
    }

    [Fact]
    public void ACallbacksThreadIsDeliveringInsideItAndNotAfter()
    {
        var probe = new Probe();
        using ObserverBridge bridge = BridgeOf(probe, out CallbacksNative callbacks);
        Assert.False(bridge.IsDelivering);

        CallEvery(callbacks);

        Assert.Equal(new[] { "log:True", "log-after:True", "error:True", "error-after:True",
                             "progress:True", "progress-after:True", "finished:True", "finished-after:True" }, probe.Seen);
        Assert.False(bridge.IsDelivering);
    }

    [Fact]
    public void AnObserverThatThrowsDoesNotLeaveTheThreadMarked()
    {
        var probe = new Probe { Throwing = true };
        using ObserverBridge bridge = BridgeOf(probe, out CallbacksNative callbacks);

        CallEvery(callbacks);

        Assert.False(bridge.IsDelivering);
        Assert.IsType<InvalidOperationException>(bridge.ObserverException);
    }

    [Fact]
    public void ACallbackInsideAnotherRunsCallbackIsDeliveringForThatRunAloneAndTheOuterOneIsRestored()
    {
        var outer = new Probe();
        var inner = new Probe();
        using ObserverBridge outerBridge = BridgeOf(outer, out CallbacksNative outerCallbacks);
        using ObserverBridge innerBridge = BridgeOf(inner, out CallbacksNative innerCallbacks);
        outer.Other = innerBridge;
        inner.Other = outerBridge;
        CallbacksNative innerOnly = innerCallbacks;
        outer.Inside = () => innerOnly.Progress(innerOnly.User, 1);

        outerCallbacks.Finished(outerCallbacks.User, 1);

        Assert.Equal(new[] { "finished:True/other:False", "finished-after:True" }, outer.Seen);
        Assert.Equal(new[] { "progress:True/other:False", "progress-after:True" }, inner.Seen);
        Assert.False(outerBridge.IsDelivering);
        Assert.False(innerBridge.IsDelivering);
    }
}
