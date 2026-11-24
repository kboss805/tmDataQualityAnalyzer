using System;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using TmDq.App.Models.Settings;

namespace TmDq.App.Models.Logging;

/// <summary>
/// Writes a report file when the application is about to close on an exception nobody handled, so that a crash in
/// the field leaves something to send: the version, when and where it was raised, the exception with its inner ones,
/// and the end of the session's log (what the user was doing). The report is a text file in a folder beside the
/// preferences (<see cref="FolderFor"/>); the newest <see cref="KeptReports"/> are kept.
/// Writing never throws: the process is already failing, and a report that cannot be written must not hide the crash
/// that asked for it.
/// </summary>
public sealed class CrashReporter
{
    /// <summary>How many reports stay in the folder; older ones are deleted when a new one is written.</summary>
    public const int KeptReports = 10;

    /// <summary>How many of the session log's last lines go into a report.</summary>
    public const int LogLinesKept = 200;

    private const string Prefix = "crash-";
    private const string Extension = ".txt";

    private readonly string _folder;
    private readonly string _version;
    private readonly TimeProvider _clock;
    private readonly object _gate = new();
    private Func<string>? _log;
    private object? _last;

    public CrashReporter(string folder, string version, TimeProvider? clock = null)
    {
        _folder = folder;
        _version = version;
        _clock = clock ?? TimeProvider.System;
    }

    /// <summary>Where an application in <paramref name="appRoot"/> keeps its reports: a folder of its own inside the one that
    /// holds its preferences (beside the executable for a portable copy, else under <paramref name="appData"/>).</summary>
    public static string FolderFor(string appRoot, string appData) =>
        Path.Combine(PreferencesStore.FolderFor(appRoot, appData), "crash-reports");

    /// <summary>The folder the reports go in.</summary>
    public string Folder => _folder;

    /// <summary>Says where to read the session's log from when a report is written (its whole text, one line per entry).
    /// Called on whatever thread raised the exception, so a reader that fails is noted in the report, not thrown.</summary>
    public void AttachLog(Func<string> snapshot)
    {
        lock (_gate) _log = snapshot;
    }

    /// <summary>Writes the report for <paramref name="error"/>, the exception the runtime raised (or the object it raised,
    /// which is not always an exception). False, with nothing written, when this very object was reported before: the same
    /// exception reaches more than one hook on its way out of the process. True otherwise, whether or not the file could be
    /// written; <paramref name="path"/> is the file, or null when it could not be.</summary>
    /// <param name="source">Where it was raised, in words a reader of the report can use.</param>
    /// <param name="error">The exception object.</param>
    /// <param name="path">The report written, or null.</param>
    public bool TryWrite(string source, object error, out string? path)
    {
        lock (_gate)
        {
            path = null;
            if (ReferenceEquals(_last, error)) return false;
            _last = error;

            DateTimeOffset now = _clock.GetLocalNow();
            string stem = Path.Combine(_folder, Prefix + now.ToString("yyyyMMdd-HHmmss-fff", CultureInfo.InvariantCulture));
            try
            {
                Directory.CreateDirectory(_folder);
                // Two different exceptions in the same millisecond are two reports: the second takes a numbered name.
                string file = stem + Extension;
                for (int n = 2; File.Exists(file); n++) file = stem + "-" + n.ToString(CultureInfo.InvariantCulture) + Extension;
                File.WriteAllText(file, Text(source, error, now), new UTF8Encoding(false));
                path = file;
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
            {
                return true;
            }
            Prune();
            return true;
        }
    }

    private string Text(string source, object error, DateTimeOffset now)
    {
        var text = new StringBuilder();
        text.Append("tmDataQualityAnalyzer crash report\n");
        text.Append("Version:    ").Append(_version).Append('\n');
        text.Append("Time:       ").Append(now.ToString("o", CultureInfo.InvariantCulture)).Append('\n');
        text.Append("Raised in:  ").Append(source).Append('\n');
        text.Append("Windows:    ").Append(RuntimeInformation.OSDescription).Append('\n');
        text.Append(".NET:       ").Append(RuntimeInformation.FrameworkDescription).Append(" (")
            .Append(RuntimeInformation.ProcessArchitecture).Append(")\n\n");
        text.Append(error.ToString()).Append("\n\n");
        text.Append(Offsets(error));
        text.Append("Session log (last ").Append(LogLinesKept.ToString(CultureInfo.InvariantCulture)).Append(" lines):\n");
        text.Append(LogTail()).Append('\n');
        return text.ToString().Replace("\n", "\r\n", StringComparison.Ordinal);
    }

    /// <summary>Each frame of the exception and of its inner ones with the IL offset it was at. The release has no symbols (they
    /// are kept in the release's symbols ZIP, not shipped), so the stack trace above has method names and no line numbers; a
    /// method and an offset are what that ZIP's <c>.pdb</c> turns into a source line. Empty when the object is not an exception.</summary>
    private static string Offsets(object error)
    {
        var frames = new StringBuilder();
        for (Exception? e = error as Exception; e is not null; e = e.InnerException)
        {
            foreach (StackFrame frame in new StackTrace(e, false).GetFrames())
            {
                if (frame.GetMethod() is not { } method) continue;
                int offset = frame.GetILOffset();
                frames.Append("  ").Append(method.DeclaringType?.FullName).Append('.').Append(method.Name).Append(" IL_")
                      .Append(offset < 0 ? "????" : offset.ToString("x4", CultureInfo.InvariantCulture)).Append('\n');
            }
        }
        return frames.Length == 0 ? "" : "Frames with IL offsets (to read with the release's symbols ZIP):\n" + frames + "\n";
    }

    /// <summary>The end of the session's log. The log is a collection the UI thread owns, and this can run on another one,
    /// so reading it can throw; the report is written anyway, saying so.</summary>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types",
        Justification = "A crash report must be written whatever state the log is in; the failure is named in the report.")]
    private string LogTail()
    {
        if (_log is null) return "(no session log was attached)";
        try
        {
            string[] lines = _log().Replace("\r\n", "\n", StringComparison.Ordinal).Split('\n');
            return string.Join("\n", lines.Skip(Math.Max(0, lines.Length - LogLinesKept)));
        }
        catch (Exception e)
        {
            return "(the session log could not be read: " + e.GetType().Name + ")";
        }
    }

    /// <summary>Deletes all but the newest <see cref="KeptReports"/> reports. Their names sort by time. A file that will not
    /// go is left: tidying is no reason to fail.</summary>
    private void Prune()
    {
        try
        {
            foreach (string old in Directory.EnumerateFiles(_folder, Prefix + "*" + Extension)
                                            .OrderDescending(StringComparer.Ordinal).Skip(KeptReports))
                File.Delete(old);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            // Left for the next report to try again.
        }
    }
}
