using System;
using System.IO;
using TmDq.Core.Tests;

namespace TmDq.App.Tests;

/// <summary>
/// Two short recordings made once for a test class (<c>IClassFixture</c>): the first <see cref="Bytes"/> of the PRN sample, cut
/// where a packet starts (<see cref="Recordings.WritePrnPrefix"/>), as <c>prn short.ch10</c> and as <c>flight 2.ch10</c>. They process
/// as the sample does, the same streams and clock, in a fraction of its time: for tests that need a recording that processes, not
/// the sample's numbers. Deleted, with their folder, when the class is done.
/// </summary>
public sealed class ShortRecordings : IDisposable
{
    /// <summary>How much of the sample is kept: enough for every stream the defaults run to reach the plot.</summary>
    public const long Bytes = 8L * 1024 * 1024;

    private readonly string _dir = TempFiles.NewPath("short-recordings");

    public ShortRecordings()
    {
        Directory.CreateDirectory(_dir);
        First = Recordings.WritePrnPrefix(Path.Combine(_dir, "prn short.ch10"), Bytes);
        Second = Path.Combine(_dir, "flight 2.ch10");
        File.Copy(First, Second);
    }

    public string First { get; }

    public string Second { get; }

    public void Dispose() => TempFiles.DeleteDirectory(_dir, recursive: true);
}
