using System;
using System.Collections.Generic;

namespace TmDq.App.Models.Processing;

/// <summary>
/// Passes a run's log on in v2.x's order, one line at a time. While the run is being started the lines are held:
/// those written on the starting thread are the job builder's, which come before the summary; anything the run's
/// own threads say in that moment follows the summary.
/// </summary>
public sealed class RunLogOrder : IRunLog
{
    private readonly IRunLog? _log;
    private readonly object _gate = new();
    private readonly List<(bool Error, string Text)> _jobLines = new();
    private List<(bool Error, string Text)>? _held;
    private int _startingThread;

    public RunLogOrder(IRunLog? log) => _log = log;

    /// <summary>Called on the thread about to start the run.</summary>
    public void Starting()
    {
        lock (_gate)
        {
            _startingThread = Environment.CurrentManagedThreadId;
            _held = new List<(bool, string)>();
        }
    }

    /// <summary>The run started: the job builder's lines, then <paramref name="between"/>, then whatever the run's
    /// threads have said meanwhile; from here on, every line as it comes.</summary>
    public void Started(IEnumerable<string> between)
    {
        lock (_gate)
        {
            foreach ((bool error, string text) in _jobLines) Emit(error, text);
            foreach (string line in between) Emit(false, line);
            foreach ((bool error, string text) in _held ?? new()) Emit(error, text);
            _held = null;
        }
    }

    /// <summary>The run did not start: the job builder's lines for the streams it built before the one refused.</summary>
    public void StartFailed() => Started(Array.Empty<string>());

    public void Line(string message) => Write(false, message);

    public void Error(string message) => Write(true, message);

    private void Write(bool error, string text)
    {
        lock (_gate)
        {
            if (_held is not null)
            {
                (Environment.CurrentManagedThreadId == _startingThread ? _jobLines : _held).Add((error, text));
                return;
            }
            Emit(error, text);
        }
    }

    private void Emit(bool error, string text)
    {
        if (_log is null) return;
        if (error) _log.Error(text);
        else _log.Line(text);
    }
}
