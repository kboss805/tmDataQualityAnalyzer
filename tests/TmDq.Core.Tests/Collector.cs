using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using TmDq.Core;

namespace TmDq.Core.Tests;

/// <summary>An observer that remembers everything a run says, with managed copies of the results.</summary>
internal sealed class Collector : IRunObserver
{
    private readonly object _lock = new();
    private readonly List<string> _events = new();
    private readonly List<ProcessedStream> _results = new();
    private readonly List<int> _progress = new();
    private readonly List<int> _progressThreads = new();
    private readonly ManualResetEventSlim _finished = new();
    private int _finishedCount;

    public bool Success { get; private set; }

    public int FinishedCount => Volatile.Read(ref _finishedCount);

    /// <summary>Throws from every method when set, to prove the run survives an observer that fails.</summary>
    public bool Throwing { get; init; }

    public IReadOnlyList<string> Events
    {
        get
        {
            lock (_lock)
            {
                return _events.ToArray();
            }
        }
    }

    public IReadOnlyList<ProcessedStream> Results
    {
        get
        {
            lock (_lock)
            {
                return _results.OrderBy(r => r.JobIndex).ToArray();
            }
        }
    }

    public IReadOnlyList<int> Progress
    {
        get
        {
            lock (_lock)
            {
                return _progress.ToArray();
            }
        }
    }

    public int DistinctProgressThreads
    {
        get
        {
            lock (_lock)
            {
                return _progressThreads.Distinct().Count();
            }
        }
    }

    public bool WaitFinished(TimeSpan timeout) => _finished.Wait(timeout);

    public void OnLog(string message)
    {
        Add("log:" + message);
        Fail();
    }

    public void OnError(string message)
    {
        Add("error:" + message);
        Fail();
    }

    public void OnProgress(int percent)
    {
        lock (_lock)
        {
            _events.Add("progress:" + percent);
            _progress.Add(percent);
            _progressThreads.Add(Environment.CurrentManagedThreadId);
        }
        Fail();
    }

    public void OnStreamCompleted(StreamResult result)
    {
        using (result)
        {
            ProcessedStream copy = result.ToProcessed();
            lock (_lock)
            {
                _events.Add("stream:" + result.JobIndex);
                _results.Add(copy);
            }
        }
        Fail();
    }

    public void OnFinished(bool success)
    {
        lock (_lock)
        {
            _events.Add("finished:" + (success ? 1 : 0));
            Success = success;
        }
        Interlocked.Increment(ref _finishedCount);
        _finished.Set();
        Fail();
    }

    private void Add(string text)
    {
        lock (_lock)
        {
            _events.Add(text);
        }
    }

    private void Fail()
    {
        if (Throwing)
        {
            throw new InvalidOperationException("the observer failed on purpose");
        }
    }
}
