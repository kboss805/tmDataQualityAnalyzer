using System;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Linq;

namespace TmDq.App.Logging
{
    /// <summary>How a log line is shown: its colour in the sidebar.</summary>
    public enum LogLevel
    {
        Info,
        Success,
        Warning,
        Error,
    }

    /// <summary>One line of the log: the clock time it was added and its text, which may span several lines.</summary>
    public sealed record LogEntry(string Time, string Text, LogLevel Level)
    {
        /// <summary>The line as the sidebar shows it, and as an export writes it.</summary>
        public string Line => Time + "  " + Text;
    }

    /// <summary>
    /// The session's log, the sidebar's contents (US3.0 to US3.2 report through it). A line's level is given by whoever
    /// knows it (an error from the core is an error whatever it says); a plain line is classified by its text as the Qt
    /// application's <c>MainViewModel::classifyLogMessage</c> does. Used on the UI thread only.
    /// </summary>
    public sealed class SessionLog
    {
        private readonly TimeProvider _clock;

        public SessionLog(TimeProvider? clock = null) => _clock = clock ?? TimeProvider.System;

        /// <summary>Every line, oldest first.</summary>
        public ObservableCollection<LogEntry> Entries { get; } = new();

        /// <summary>The level a plain line's text implies, highest first: ERROR, then WARNING, then the two lines that
        /// report success, else information. Case matters, as it did in Qt.</summary>
        public static LogLevel Classify(string message)
        {
            if (message.Contains("ERROR", StringComparison.Ordinal)) return LogLevel.Error;
            if (message.Contains("WARNING", StringComparison.Ordinal)) return LogLevel.Warning;
            if (message.StartsWith("Pre-scan result:", StringComparison.Ordinal) ||
                message.StartsWith("Processing complete", StringComparison.Ordinal))
                return LogLevel.Success;
            return LogLevel.Info;
        }

        /// <summary>Adds a line, classified by its text.</summary>
        public void Add(string message) => Add(message, Classify(message));

        public void Add(string message, LogLevel level) =>
            Entries.Add(new LogEntry(_clock.GetLocalNow().ToString("HH:mm:ss", CultureInfo.InvariantCulture), message, level));

        public void Error(string message) => Add(message, LogLevel.Error);

        public void Warning(string message) => Add(message, LogLevel.Warning);

        public void Success(string message) => Add(message, LogLevel.Success);

        /// <summary>The whole log as text, a line per entry (an entry's own line breaks kept).</summary>
        public string PlainText => string.Join("\n", Entries.Select(e => e.Line));
    }
}
