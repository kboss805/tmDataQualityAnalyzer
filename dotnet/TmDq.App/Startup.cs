using System;
using System.IO;
using System.Text;
using TmDq.App.ViewModels;

namespace TmDq.App
{
    /// <summary>
    /// What the command line asks of the application at start. A recording path opens that recording, as a file
    /// association will. With TMDQ_SMOKE_REPORT set as well, the application writes one line saying how the recording
    /// went to that file and exits (0 when the stream was processed, 1 when not): the installer's clean-machine test
    /// uses it to prove the whole path, native core included, on a machine that has never seen the app.
    /// </summary>
    public static class Startup
    {
        public const string SmokeReportVariable = "TMDQ_SMOKE_REPORT";

        /// <summary>The recording the command line names: its first argument that is not an option, or null.</summary>
        public static string? RecordingFromArguments(string[]? args)
        {
            if (args is null) return null;
            foreach (string a in args)
                if (!string.IsNullOrWhiteSpace(a) && !a.StartsWith('-')) return a;
            return null;
        }

        /// <summary>The report file to write, or null when the variable is unset.</summary>
        public static string? SmokeReportPath() =>
            Environment.GetEnvironmentVariable(SmokeReportVariable) is { Length: > 0 } path ? path : null;

        /// <summary>"ok samples=N mean=M.MMM channel=C | status" when a series was produced, else "failed | status".</summary>
        public static string SmokeReport(MainWindowViewModel vm) => vm.Series is { } s
            ? FormattableString.Invariant($"ok samples={s.LockPercent.Length} mean={s.MeanLock:F3} channel={s.PcmChannelId} | {vm.Status}")
            : "failed | " + vm.Status;

        public static void WriteSmokeReport(string path, MainWindowViewModel vm) =>
            File.WriteAllText(path, SmokeReport(vm) + Environment.NewLine, new UTF8Encoding(false));
    }
}
