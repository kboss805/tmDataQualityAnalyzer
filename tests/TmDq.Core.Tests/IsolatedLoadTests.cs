using System;
using System.Diagnostics;
using System.IO;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using TmDq.Core;
using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// The state before anything is loaded can only be seen in a process nothing has been loaded into, and every
/// other test in this executable loads the DLL. So this test runs <see cref="NotLoadedProbe"/> in a child
/// process of this same executable (same UTF-8 manifest, same xunit) and requires it to pass.
/// </summary>
public class IsolatedLoadTests
{
    [Fact]
    public async Task AProcessThatHasLoadedNothingRefusesEveryCallAndCanStillLoadAfterABadFile()
    {
        var start = new ProcessStartInfo(Environment.ProcessPath!)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        start.ArgumentList.Add("-class");
        start.ArgumentList.Add(typeof(NotLoadedProbe).FullName!);
        start.ArgumentList.Add("-noColor");
        start.Environment[NotLoadedProbe.Variable] = "1";
        start.Environment["TMDQ_CORE_DLL"] = Env.DllPath();

        using Process child = Process.Start(start)!;
        CancellationToken token = TestContext.Current.CancellationToken;
        Task<string> output = child.StandardOutput.ReadToEndAsync(token);
        Task<string> errors = child.StandardError.ReadToEndAsync(token);
        using var limit = CancellationTokenSource.CreateLinkedTokenSource(token);
        limit.CancelAfter(TimeSpan.FromMinutes(2));
        try
        {
            await child.WaitForExitAsync(limit.Token);
        }
        catch (OperationCanceledException)
        {
            child.Kill(entireProcessTree: true);
            Assert.Fail("the child process did not finish");
        }
        string text = await output + await errors;

        Assert.True(child.ExitCode == 0, text);
        // And it ran the probe: a filter that matched nothing would also exit quietly.
        Match summary = Regex.Match(text, @"Total: (\d+), Errors: 0, Failed: 0, Skipped: 0");
        Assert.True(summary.Success && summary.Groups[1].Value == "1", text);
    }
}

/// <summary>Runs only inside the child process of <see cref="IsolatedLoadTests"/>; skipped everywhere else.</summary>
public class NotLoadedProbe
{
    public const string Variable = "TMDQ_PROBE";

    [Fact]
    public void Probe()
    {
        Assert.SkipUnless(Environment.GetEnvironmentVariable(Variable) == "1", "runs in a child process, see IsolatedLoadTests");

        string good = Env.DllPath();

        // Nothing is loaded, and every way in says so, in words, instead of failing somewhere inside the runtime.
        Assert.Null(Library.LoadedPath);
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => Library.AbiVersion).Message);
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => Library.AbiRevision).Message);
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => Ch10File.Open(Env.StepCal)).Message);
        var request = new RunRequest(Env.StepCal, 1, new[] { new StreamConfig { PcmChannelId = 26, SyncPattern = "FE6B2840", BitsInMinorFrame = 800 } });
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => ProcessingRun.Start(request)).Message);
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => StepFile.Parse(good)).Message);
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => StepFile.InitialPath(null, null)).Message);
        var calibration = new CalibrationRequest
        {
            FileName = Env.StepCal, TimeChannelId = 1, PcmChannelId = 26, SyncPattern = "FE6B2840", BitsInMinorFrame = 800, Steps = new[] { 0.0, 6.0 },
        };
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => CalibrationRun.Start(calibration)).Message);
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => FrameSyncFile.Read(good)).Message);
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => FrameSyncFile.Write(good, "AB", "", 64, false, false)).Message);
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => TemplateMatcher.Match(new[] { 1 }, new[] { 1 })).Message);
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => CsvFile.Parse(good)).Message);
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => CsvFile.Write(good, new[] { new CsvColumn(MetricType.Snr, "A", 0, new[] { 0.0 }, new[] { 0.0 }) }, 1, 0)).Message);
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => ClockTime.Format(1, 0, 0)).Message);
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => new TimeBase()).Message);
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => new StreamKey(0, "A", 1).IsSameStream(new StreamKey(0, "A", 1))).Message);
        Assert.Contains("Library.Load", Assert.Throws<InvalidOperationException>(() => ImportedStreams.AssignOrders(new[] { new ImportedSeriesInfo(MetricType.Snr, "A", 0, 1) })).Message);
        Assert.Null(Library.LoadedPath);

        // A file that is not a library is refused, and leaves nothing loaded...
        string notALibrary = Path.Combine(Path.GetTempPath(), "tmdq-notalib-" + Guid.NewGuid().ToString("N") + ".dll");
        File.WriteAllText(notALibrary, "this is not a dynamic library");
        try
        {
            InvalidOperationException bad = Assert.Throws<InvalidOperationException>(() => Library.Load(notALibrary));
            Assert.Contains("not a usable tmdq_core.dll", bad.Message);
            Assert.Contains(Path.GetFullPath(notALibrary), bad.Message);
            Assert.Null(Library.LoadedPath);
        }
        finally
        {
            TempFiles.Delete(notALibrary);
        }

        // ...so the real one can still be loaded afterwards, by a path spelled relative to here.
        Library.Load(Path.GetRelativePath(Environment.CurrentDirectory, good));
        Assert.Equal(Path.GetFullPath(good), Library.LoadedPath);
        Assert.Equal(Library.ExpectedVersion, Library.AbiVersion);
        Assert.NotEmpty(Ch10File.Open(Env.StepCal).PcmChannels);
    }
}
