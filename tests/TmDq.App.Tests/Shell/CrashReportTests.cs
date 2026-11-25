using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using TmDq.App.Models.Logging;
using TmDq.App.Models.Settings;
using TmDq.Core.Tests;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// A crash leaves a report (<see cref="CrashReporter"/>) and tells the user once (<see cref="CrashHandling"/>). Before this the
/// process simply vanished. The process-wide hooks are not installed here (only <c>Program.Main</c> does that): these tests call
/// what the hooks call.
/// </summary>
public class CrashReportTests : IDisposable
{
    private readonly string _folder = TempFiles.NewPath("crash-reports");
    private readonly FixedClock _clock = new(new DateTimeOffset(2026, 11, 24, 6, 12, 5, 21, TimeSpan.Zero));

    public void Dispose()
    {
        TempFiles.TryDeleteDirectory(_folder);
        GC.SuppressFinalize(this);
    }

    private sealed class FixedClock : TimeProvider
    {
        public FixedClock(DateTimeOffset now) => Now = now;

        public DateTimeOffset Now { get; set; }

        public override DateTimeOffset GetUtcNow() => Now;

        public override TimeZoneInfo LocalTimeZone => TimeZoneInfo.Utc;
    }

    private CrashReporter Reporter() => new(_folder, "9.8.7", _clock);

    /// <summary>An exception that has been thrown, so that it has a stack trace as one the runtime reports does.</summary>
    private static InvalidOperationException Thrown(string message, Exception? inner = null)
    {
        try
        {
            throw new InvalidOperationException(message, inner);
        }
        catch (InvalidOperationException e)
        {
            return e;
        }
    }

    private static IOException ThrownInner()
    {
        try
        {
            throw new IOException("the inner failure");
        }
        catch (IOException e)
        {
            return e;
        }
    }

    private string[] Reports() =>
        Directory.Exists(_folder) ? Directory.GetFiles(_folder, "crash-*.txt").OrderBy(f => f, StringComparer.Ordinal).ToArray() : Array.Empty<string>();

    [Fact]
    public void AReportNamesTheVersionTheTimeThePlaceAndTheExceptionWithItsInnerOne()
    {
        InvalidOperationException error = Thrown("the outer failure", ThrownInner());

        Assert.True(Reporter().TryWrite("an unhandled exception on the UI thread", error, out string? path));

        Assert.Equal(Path.Combine(_folder, "crash-20261124-061205-021.txt"), path);
        string text = File.ReadAllText(path!);
        Assert.Contains("Version:    9.8.7", text);
        Assert.Contains("Time:       2026-11-24T06:12:05.0210000+00:00", text);
        Assert.Contains("Raised in:  an unhandled exception on the UI thread", text);
        Assert.Contains("System.InvalidOperationException: the outer failure", text);
        Assert.Contains("System.IO.IOException: the inner failure", text);
        Assert.Contains("CrashReportTests.Thrown", text);   // the stack trace
        Assert.DoesNotContain("\n", text.Replace("\r\n", "", StringComparison.Ordinal), StringComparison.Ordinal);   // CR LF throughout
    }

    [Fact]
    public void EveryFrameOfTheExceptionAndOfItsInnerOneIsGivenWithItsILOffsetForTheReleasesSymbols()
    {
        Assert.True(Reporter().TryWrite("x", Thrown("the outer failure", ThrownInner()), out string? path));

        string text = File.ReadAllText(path!);
        Assert.Contains("Frames with IL offsets", text);
        // The release carries no symbols, so a method and an offset are what its symbols ZIP turns into a line: one per frame,
        // four hex digits, for the outer exception's frame and for the inner one's.
        Assert.Matches(@"  TmDq\.App\.Tests\.CrashReportTests\.Thrown IL_[0-9a-f]{4}\r\n", text);
        Assert.Matches(@"  TmDq\.App\.Tests\.CrashReportTests\.ThrownInner IL_[0-9a-f]{4}\r\n", text);
    }

    [Fact]
    public void ANonExceptionObjectIsReportedByItsTextWithNoOffsets()
    {
        // AppDomain.UnhandledException hands over an object, which is not always an exception.
        Assert.True(Reporter().TryWrite("an unhandled exception", "a string was thrown", out string? path));

        string text = File.ReadAllText(path!);
        Assert.Contains("a string was thrown", text);
        Assert.DoesNotContain("Frames with IL offsets", text);
    }

    [Fact]
    public void TheReportCarriesTheLastTwoHundredLinesOfTheSessionLogAndNoMore()
    {
        CrashReporter reporter = Reporter();
        reporter.AttachLog(() => string.Join("\n", Enumerable.Range(1, 250).Select(i => "log line " + i)));

        Assert.True(reporter.TryWrite("x", Thrown("boom"), out string? path));

        string text = File.ReadAllText(path!);
        Assert.Contains("log line 250", text);
        Assert.Contains("log line 51\r\n", text);       // the 200th from the end
        Assert.DoesNotContain("log line 50\r\n", text);  // the 201st
    }

    [Fact]
    public void ALogThatCannotBeReadIsNamedInTheReportAndDoesNotStopIt()
    {
        CrashReporter reporter = Reporter();
        reporter.AttachLog(() => throw new InvalidOperationException("the collection changed"));

        Assert.True(reporter.TryWrite("x", Thrown("boom"), out string? path));

        string text = File.ReadAllText(path!);
        Assert.Contains("the session log could not be read: InvalidOperationException", text);
        Assert.Contains("System.InvalidOperationException: boom", text);
    }

    [Fact]
    public void WithNoLogAttachedTheReportSaysSo()
    {
        Assert.True(Reporter().TryWrite("x", Thrown("boom"), out string? path));
        Assert.Contains("no session log was attached", File.ReadAllText(path!));
    }

    [Fact]
    public void TheSameExceptionIsReportedOnceAndAnotherOneIsReportedToo()
    {
        CrashReporter reporter = Reporter();
        InvalidOperationException error = Thrown("boom");

        Assert.True(reporter.TryWrite("the dispatcher", error, out string? first));
        Assert.False(reporter.TryWrite("the process", error, out string? again));   // it reaches the process hook as well
        Assert.NotNull(first);
        Assert.Null(again);
        Assert.Single(Reports());

        Assert.True(reporter.TryWrite("the process", Thrown("a different one"), out string? other));
        Assert.NotEqual(first, other);   // the same millisecond, the clock being stopped: a second name, not an overwrite
        Assert.Equal(2, Reports().Length);
        Assert.Contains("boom", File.ReadAllText(first!));
    }

    [Fact]
    public void OnlyTheNewestTenReportsStayAndAnotherFileInTheFolderIsLeftAlone()
    {
        Directory.CreateDirectory(_folder);
        string notes = Path.Combine(_folder, "notes.txt");
        File.WriteAllText(notes, "mine");
        CrashReporter reporter = Reporter();
        var written = new List<string>();
        for (int i = 0; i < CrashReporter.KeptReports + 2; i++)
        {
            _clock.Now = _clock.Now.AddSeconds(1);
            Assert.True(reporter.TryWrite("x", Thrown("failure " + i), out string? path));
            written.Add(path!);
        }

        Assert.Equal(CrashReporter.KeptReports, Reports().Length);
        Assert.Equal(written.Skip(2).OrderBy(p => p, StringComparer.Ordinal), Reports());   // the two oldest are gone
        Assert.True(File.Exists(notes));
    }

    [Fact]
    public void AFolderThatCannotBeWrittenIsNoCrashOfItsOwn()
    {
        string blocker = TempFiles.NewPath("crash-blocker", ".txt");
        File.WriteAllText(blocker, "a file where the folder should be");
        try
        {
            var reporter = new CrashReporter(Path.Combine(blocker, "reports"), "9.8.7", _clock);

            Assert.True(reporter.TryWrite("x", Thrown("boom"), out string? path));   // handled, though nothing could be written

            Assert.Null(path);
        }
        finally
        {
            TempFiles.Delete(blocker);
        }
    }

    [Fact]
    public void TheReportsGoBesideThePreferencesForTheFolderTheyAreInAndBesideAPortableCopy()
    {
        string root = TempFiles.NewPath("crash-root");
        string appData = TempFiles.NewPath("crash-appdata");
        Directory.CreateDirectory(root);
        try
        {
            string installed = CrashReporter.FolderFor(root, appData);
            Assert.Equal(Path.Combine(PreferencesStore.FolderFor(root, appData), "crash-reports"), installed);
            Assert.StartsWith(appData, installed, StringComparison.Ordinal);

            File.WriteAllText(Path.Combine(root, PreferencesStore.PortableMarker), "");
            string portable = CrashReporter.FolderFor(root, appData);
            Assert.StartsWith(root, portable, StringComparison.Ordinal);
            Assert.EndsWith("crash-reports", portable, StringComparison.Ordinal);
        }
        finally
        {
            TempFiles.TryDeleteDirectory(root);
        }
    }

    [Fact]
    public void TheUserIsToldOnceWhereTheReportIsWhateverMoreArrives()
    {
        var told = new List<string>();
        var handling = new CrashHandling(Reporter(), told.Add);

        handling.Raised("the dispatcher", Thrown("first"));
        handling.Raised("the process", Thrown("second"));   // a second exception while the process ends

        Assert.Equal(2, Reports().Length);   // both reported ...
        string message = Assert.Single(told);   // ... one box, naming the first report
        Assert.Contains(Path.Combine(_folder, "crash-20261124-061205-021.txt"), message, StringComparison.Ordinal);
    }

    [Fact]
    public void TheSameExceptionFromTheDispatcherAndThenTheProcessIsOneReportAndOneMessage()
    {
        var told = new List<string>();
        var handling = new CrashHandling(Reporter(), told.Add);
        InvalidOperationException error = Thrown("boom");

        handling.Raised("the dispatcher", error);
        handling.Raised("the process", error);

        Assert.Single(Reports());
        Assert.Single(told);
    }

    [Fact]
    public void WithNoOneToTellTheReportIsStillWritten()
    {
        new CrashHandling(Reporter(), null).Raised("the process", Thrown("boom"));   // the installer's smoke run: no box

        Assert.Single(Reports());
    }

    [Fact]
    public void AMessageBoxThatFailsDoesNotReplaceTheCrash()
    {
        var handling = new CrashHandling(Reporter(), _ => throw new InvalidOperationException("no desktop"));

        handling.Raised("the process", Thrown("boom"));   // returns: the process is ending and the report is there

        Assert.Single(Reports());
    }

    [Fact]
    public void TheMessageSaysWhereTheReportIsOrThatItCouldNotBeSavedAndWhere()
    {
        string saved = CrashHandling.Message(@"C:\Users\x\crash-1.txt", @"C:\Users\x");
        Assert.Contains("hit an unexpected error and has to close", saved);
        Assert.Contains("A report was saved to:\nC:\\Users\\x\\crash-1.txt", saved);
        Assert.Contains("send that file", saved);

        string failed = CrashHandling.Message(null, @"C:\Users\x");
        Assert.Contains("The report could not be saved: the folder C:\\Users\\x cannot be written to.", failed);
        Assert.DoesNotContain("A report was saved", failed);
    }
}
