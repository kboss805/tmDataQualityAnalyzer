using System;
using System.Diagnostics.CodeAnalysis;
using System.Threading;
using TmDq.App.Models.Logging;

namespace TmDq.App;

/// <summary>
/// What happens when the application is about to close on an exception nobody handled: a report is written
/// (<see cref="CrashReporter"/>) and the user is told once, in the operating system's own message box, where it is.
/// Before this the process simply vanished, and a crash in the field left nothing to go on.
/// Only <c>Program.Main</c> installs it: the tests build the application's types without it, so they hang no handler on
/// the process. The installer's smoke test installs it with no message, because it has no one to dismiss a box.
/// </summary>
public sealed class CrashHandling
{
    private readonly CrashReporter _reporter;
    private readonly Action<string>? _tell;
    private int _told;

    /// <param name="reporter">Writes the report.</param>
    /// <param name="tell">Shows the user the message (<see cref="Message"/>), or null to tell no one.</param>
    public CrashHandling(CrashReporter reporter, Action<string>? tell)
    {
        _reporter = reporter;
        _tell = tell;
    }

    /// <summary>The handling installed by <see cref="Install"/>, or null when none is (the tests, and before Main has run).</summary>
    public static CrashHandling? Current { get; private set; }

    /// <summary>Hangs the handling on the process: an exception raised on any thread and left unhandled is reported, as the
    /// runtime ends the process. The exceptions of the UI thread reach the same place through
    /// <see cref="Raised"/>, which <c>Program</c> calls from the dispatcher's own event. An exception a task never observes
    /// does not end the process in .NET, so it is not a crash and is not reported.</summary>
    public static CrashHandling Install(CrashReporter reporter, Action<string>? tell)
    {
        var handling = new CrashHandling(reporter, tell);
        Current = handling;
        AppDomain.CurrentDomain.UnhandledException += (_, e) => handling.Raised("an unhandled exception", e.ExceptionObject);
        return handling;
    }

    /// <summary>Says where to read the session's log from when a report is written.</summary>
    public void AttachLog(Func<string> snapshot) => _reporter.AttachLog(snapshot);

    /// <summary>An exception reached the top and the process is ending because of it. It is reported once (the same one arrives
    /// from the dispatcher and again from the process), and the user is told once per process, whichever hook it came through.</summary>
    /// <param name="source">Where it was raised, in words for the report.</param>
    /// <param name="error">The exception, or the object that was raised.</param>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types",
        Justification = "This runs as the process fails; a message box that cannot be shown must not replace the crash it reports.")]
    public void Raised(string source, object error)
    {
        if (!_reporter.TryWrite(source, error, out string? path)) return;
        if (_tell is null || Interlocked.Exchange(ref _told, 1) != 0) return;
        try
        {
            _tell(Message(path, _reporter.Folder));
        }
        catch (Exception)
        {
            // Nothing more can be done: the report is written, and the process is ending.
        }
    }

    /// <summary>The words the user sees: what happened, where the report is (or that it could not be saved, and where it
    /// would have gone), and what to do with it.</summary>
    public static string Message(string? reportPath, string folder) =>
        "tmDataQualityAnalyzer hit an unexpected error and has to close.\n\n" +
        (reportPath is null
            ? "The report could not be saved: the folder " + folder + " cannot be written to.\n\n"
            : "A report was saved to:\n" + reportPath + "\n\n") +
        "Please send that file, with a note of what you were doing, to the person who supports this copy of the application.";
}
