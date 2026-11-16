using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using TmDq.App.ViewModels;
using TmDq.App.ViewModels.Plot;
using TmDq.Core;

namespace TmDq.App;

/// <summary>
/// What the command line asks of the application at start. A recording path opens that recording, as a file
/// association will. With TMDQ_SMOKE_REPORT set as well, the application writes one line saying how the recording
/// went to that file and exits (0 when the stream was processed, 1 when not): the installer's clean-machine test
/// uses it to prove the whole path, native core included, on a machine that has never seen the app.
/// </summary>
public static class Startup
{
    private const string SmokeReportVariable = "TMDQ_SMOKE_REPORT";

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

    /// <summary>The first Frame Sync Lock stream the plot holds, in table order, or null when it holds none.</summary>
    public static PlotSeries? FirstLockSeries(PlotViewModel plot) =>
        plot.Series.Where(s => s.Metric == MetricType.FrameSyncLock).OrderBy(s => s.StreamSequence).FirstOrDefault();

    /// <summary>"ok samples=N mean=M.MMM channel=C | status" when a lock series was plotted, else "failed | status".
    /// The channel is the stream's PCM channel id.</summary>
    public static string SmokeReport(MainWindowViewModel vm) => FirstLockSeries(vm.Plot) is { } s
        ? FormattableString.Invariant($"ok samples={s.Y.Length} mean={(s.Y.Length == 0 ? 0 : s.Y.Average()):F3} channel={s.StreamOrder} | {vm.Status}")
        : "failed | " + vm.Status;

    public static void WriteSmokeReport(string path, MainWindowViewModel vm) =>
        File.WriteAllText(path, SmokeReport(vm) + Environment.NewLine, new UTF8Encoding(false));

    /// <summary>The smoke run: opens <paramref name="recording"/> in <paramref name="vm"/>, writes the report to
    /// <paramref name="reportPath"/> and says how the process should exit: <see cref="ExitProcessed"/> when a Frame
    /// Sync Lock stream was plotted, <see cref="ExitNotProcessed"/> when not, <see cref="ExitNoReport"/> when the
    /// report could not be written (a folder that is not there, a path that is not a file name). The last is a code of
    /// its own, so a bad report path is not mistaken for a recording that failed, and nothing is thrown out of the
    /// window's Opened handler, where it would end the process unreported.</summary>
    public static async Task<int> RunSmokeAsync(MainWindowViewModel vm, string recording, string reportPath)
    {
        await vm.LoadAsync(recording);
        int exit = FirstLockSeries(vm.Plot) is null ? ExitNotProcessed : ExitProcessed;
        try
        {
            WriteSmokeReport(reportPath, vm);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            return ExitNoReport;
        }
        return exit;
    }

    public const int ExitProcessed = 0;
    public const int ExitNotProcessed = 1;
    public const int ExitNoReport = 2;
}
