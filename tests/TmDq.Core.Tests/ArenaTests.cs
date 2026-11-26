using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using Xunit;

namespace TmDq.Core.Tests;

[Collection(ProcessMemoryCollection.Name)]
public unsafe class ArenaTests
{
    public ArenaTests()
    {
        Env.EnsureLoaded();     // Text.Snprintf asks the library for its last error
    }

    [Fact]
    public void ANullStringIsNullAndAnEmptyOneIsJustATerminator()
    {
        using var arena = new Arena();
        Assert.True(arena.Utf8(null) == null);

        byte* empty = arena.Utf8("");
        Assert.True(empty != null);
        Assert.Equal(0, empty[0]);
    }

    [Fact]
    public void AStringIsCopiedAsUtf8WithATerminatorAfterTheLastByte()
    {
        const string text = "café Ω 🙂";
        byte[] expected = Encoding.UTF8.GetBytes(text);
        Assert.True(expected.Length > text.Length);        // the point: bytes, not characters

        using var arena = new Arena();
        byte* p = arena.Utf8(text);
        for (int i = 0; i < expected.Length; i++)
        {
            Assert.Equal(expected[i], p[i]);
        }
        Assert.Equal(0, p[expected.Length]);
        Assert.Equal(text, Text.Read(p));
    }

    [Fact]
    public void AnAllocationIsZeroedAndASizeOfNothingStillGivesAPointer()
    {
        using var arena = new Arena();
        byte* block = (byte*)arena.Alloc(64);
        for (int i = 0; i < 64; i++)
        {
            Assert.Equal(0, block[i]);
        }

        Assert.True(arena.Alloc(0) != null);
        Assert.True(arena.Alloc<long>(0) != null);
        Assert.True(arena.Alloc<long>(-5) != null);          // a nonsense count is an empty array, not a huge allocation
        long* longs = arena.Alloc<long>(3);
        Assert.Equal(0L, longs[2]);
    }

    [Fact]
    public void NothingReadIsMadeUpForAMissingString()
    {
        Assert.Equal(string.Empty, Text.Read(null));
    }

    // --- text a function writes into a buffer the caller provides, snprintf-style

    /// <summary>A function that writes <paramref name="text"/> the way the native ones do, reporting <paramref name="reported"/> as its length.</summary>
    private static Func<IntPtr, int, int> Writes(string text, Func<int, int>? reported = null, List<(IntPtr, int)>? calls = null)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(text);
        return (buffer, capacity) =>
        {
            calls?.Add((buffer, capacity));
            if (buffer != IntPtr.Zero)
            {
                int n = Math.Min(bytes.Length, capacity - 1);
                System.Runtime.InteropServices.Marshal.Copy(bytes, 0, buffer, n);
                System.Runtime.InteropServices.Marshal.WriteByte(buffer, n, 0);
            }
            return reported is null ? bytes.Length : reported(bytes.Length);
        };
    }

    [Fact]
    public void TextIsMeasuredThenWrittenIntoABufferOfExactlyThatSize()
    {
        var calls = new List<(IntPtr, int)>();
        Assert.Equal("café Ω", Text.Snprintf(Writes("café Ω", calls: calls)));

        int bytes = Encoding.UTF8.GetByteCount("café Ω");
        Assert.Equal(2, calls.Count);
        Assert.Equal((IntPtr.Zero, 0), calls[0]);                  // first: no buffer, how long is it?
        Assert.NotEqual(IntPtr.Zero, calls[1].Item1);
        Assert.Equal(bytes + 1, calls[1].Item2);                   // then: room for the text and its terminator
    }

    [Fact]
    public void NothingToWriteIsAskedOnceAndIsAnEmptyString()
    {
        var calls = new List<(IntPtr, int)>();
        Assert.Equal(string.Empty, Text.Snprintf(Writes("", calls: calls)));
        Assert.Single(calls);
    }

    /// <summary>A function whose text is <paramref name="texts"/>[k] on its k'th call (the last one from then on), written
    /// the way the native ones write: what fits, and the whole length returned.</summary>
    private static Func<IntPtr, int, int> Grows(List<(IntPtr, int)> calls, params string[] texts) => (buffer, capacity) =>
    {
        string text = texts[Math.Min(calls.Count, texts.Length - 1)];
        return Writes(text, calls: calls)(buffer, capacity);
    };

    [Fact]
    public void TextThatGrewBetweenTheCallsIsAskedForAgainWithRoomForAllOfIt()
    {
        // Measured at 5 bytes, then 11 when written (the library wrote what fitted, and said how long it is now).
        var calls = new List<(IntPtr, int)>();
        Assert.Equal("hello world", Text.Snprintf(Grows(calls, "hello", "hello world")));
        Assert.Equal(new[] { 0, 6, 12 }, calls.Select(c => c.Item2));

        // It grows once more on the third call: a fourth buffer.
        calls.Clear();
        Assert.Equal("hello there world", Text.Snprintf(Grows(calls, "hello", "hello world", "hello there world")));
        Assert.Equal(new[] { 0, 6, 12, 18 }, calls.Select(c => c.Item2));

        // One byte longer: it fills the buffer, terminator's place included, so it was cut short and is asked again.
        calls.Clear();
        Assert.Equal("abcd", Text.Snprintf(Grows(calls, "abc", "abcd")));
        Assert.Equal(new[] { 0, 4, 5 }, calls.Select(c => c.Item2));
    }

    [Fact]
    public void TextThatNeverStopsGrowingIsAnErrorAfterFourBuffersNotATruncatedString()
    {
        int call = 0;
        Func<IntPtr, int, int> longer = (buffer, capacity) => Writes(new string('x', 10 * ++call))(buffer, capacity);

        TmdqException e = Assert.Throws<TmdqException>(() => Text.Snprintf(longer));
        Assert.Equal(Status.Internal, e.Status);
        Assert.Equal(5, call);                                      // measured once, then four buffers
        Assert.Equal(4, Text.BufferAttempts);
    }

    [Fact]
    public void TextThatShrankOrStayedTheSameIsReadAsFarAsWasWritten()
    {
        Assert.Equal("hello", Text.Snprintf(Writes("hello", reported: n => n == 5 ? 5 : 99)));
        int call = 0;
        Assert.Equal("he", Text.Snprintf(Writes("hello", reported: n => ++call == 1 ? n : 2)));   // the second call reports less

        // A shorter text on the second call is read whole, with no third.
        var calls = new List<(IntPtr, int)>();
        Assert.Equal("abc", Text.Snprintf(Grows(calls, "abcde", "abc")));
        Assert.Equal(2, calls.Count);
    }

    // --- sizes, and the text the core cannot be handed

    [Fact]
    public void AnArraysSizeIsWorkedOutInPointerSizedArithmeticAndAnImpossibleOneThrows()
    {
        Assert.True(Environment.Is64BitProcess);
        // 2^29 doubles are 4 GiB: in 32-bit arithmetic that was 0 bytes, a block of one, and the copy wrote past it.
        Assert.Equal(4_294_967_296UL, (ulong)Arena.Bytes(536_870_912, sizeof(double)));
        Assert.Equal(17_179_869_176UL, (ulong)Arena.Bytes(int.MaxValue, 8));
        Assert.Equal((nuint)24, Arena.Bytes(3, 8));
        Assert.Equal((nuint)0, Arena.Bytes(-5, 8));                    // a nonsense count is an empty array
        Assert.Throws<OverflowException>(() => Arena.Bytes(4, nuint.MaxValue / 2));
    }

    [Fact]
    public void ALargeArrayIsCopiedWholeAndAListLikeIt()
    {
        double[] values = Enumerable.Range(0, 100_000).Select(i => i * 0.5 - 7.0).ToArray();
        using var arena = new Arena();
        double* fromArray = arena.Copy(values);
        double* fromList = arena.Copy(new List<double>(values));
        Assert.Equal(values, new ReadOnlySpan<double>(fromArray, values.Length).ToArray());
        Assert.Equal(values, new ReadOnlySpan<double>(fromList, values.Length).ToArray());
    }

    // The odd character is passed as a number: an attribute's string is stored as UTF-8, so a lone surrogate written in one
    // reaches the test as U+FFFD, which is the point being tested.
    [Theory]
    [InlineData("C:\\data\\run", 0xD800, ".ch10", "not valid Unicode")]   // a lone high surrogate, which a Windows file name may hold
    [InlineData("label", 0xDC00, "", "not valid Unicode")]                 // a lone low one
    [InlineData("C:\\data\\run.ch10", 0, ".csv", "NUL")]                   // a NUL, which would end the path early
    public void TextUtf8CannotCarryAsItIsIsRefusedRatherThanChanged(string before, int odd, string after, string why)
    {
        string text = before + (char)odd + after;
        using var arena = new Arena();
        TmdqException e = Assert.Throws<TmdqException>(() => arena.Utf8(text));
        Assert.Equal(Status.BadArgument, e.Status);
        Assert.Contains(why, e.Message);
    }

    [Fact]
    public void APathThatCannotBeHandedOverIsRefusedBeforeTheCoreIsAsked()
    {
        // The core would have been asked for "run?.ch10" (U+FFFD), another file: refused here, as a TmdqException, which is
        // what the app already reports a recording that will not open with.
        TmdqException open = Assert.Throws<TmdqException>(() => Ch10File.Open(Env.Prn + "\uD800"));
        Assert.Equal(Status.BadArgument, open.Status);
        Assert.Contains("not valid Unicode", open.Message);

        TmdqException write = Assert.Throws<TmdqException>(() => CsvFile.Write("out\0.csv",
            new[] { new CsvColumn(MetricType.FrameSyncLock, "A", 0, new[] { 0.0 }, new[] { 1.0 }) }, 1, 0));
        Assert.Equal(Status.BadArgument, write.Status);
        Assert.Contains("NUL", write.Message);
    }

    [Fact]
    public void AListIsCopiedAndNothingIsNull()
    {
        using var arena = new Arena();
        Assert.True(arena.Copy(new List<double>()) == null);          // the library reads nothing from a count of 0
        Assert.True(arena.Copy(Array.Empty<int>()) == null);

        double* doubles = arena.Copy(new List<double> { 1.5, -2.0, 7.25 });
        Assert.Equal(new[] { 1.5, -2.0, 7.25 }, new[] { doubles[0], doubles[1], doubles[2] });
        int* ints = arena.Copy(new[] { 4, 5 });
        Assert.Equal(new[] { 4, 5 }, new[] { ints[0], ints[1] });
    }

    [Fact]
    public void AnArrayOfPointersIsZeroedAndABoxedValueIsReadBackThroughItsPointer()
    {
        using var arena = new Arena();
        long** pointers = arena.AllocPointers<long>(3);
        Assert.True(pointers[0] == null && pointers[1] == null && pointers[2] == null);
        Assert.True(arena.AllocPointers<long>(0) != null);
        Assert.True(arena.AllocPointers<long>(-4) != null);

        pointers[1] = arena.Box(123456789012L);
        pointers[2] = arena.Box(-7L);
        Assert.Equal((123456789012L, -7L), (*pointers[1], *pointers[2]));
        Assert.True(pointers[1] != pointers[2]);                      // a block each, not one shared
    }

    [Fact]
    public void ANegativeResultIsABadArgumentAndAnythingElseIsReturnedAsItIs()
    {
        Assert.Equal((0, 7), (Library.NonNegative(0), Library.NonNegative(7)));
        TmdqException e = Assert.Throws<TmdqException>(() => Library.NonNegative(-1));
        Assert.Equal(Status.BadArgument, e.Status);
    }

    [Fact]
    public void AnErrorFromEitherCallIsAnException()
    {
        TmdqException first = Assert.Throws<TmdqException>(() => Text.Snprintf((buffer, capacity) => -1));
        Assert.Equal(Status.BadArgument, first.Status);

        int call = 0;
        TmdqException second = Assert.Throws<TmdqException>(() => Text.Snprintf((buffer, capacity) => ++call == 1 ? 3 : -1));
        Assert.Equal(Status.BadArgument, second.Status);
    }

    [Fact]
    public void DisposingGivesTheMemoryBack()
    {
        // 32 arenas of 16 MiB each would be 512 MiB of private memory if they were never freed.
        long before = Process.GetCurrentProcess().PrivateMemorySize64;
        for (int i = 0; i < 32; i++)
        {
            using var arena = new Arena();
            arena.Alloc(16u * 1024 * 1024);
            arena.Alloc(16u * 1024 * 1024);
        }
        long grown = Process.GetCurrentProcess().PrivateMemorySize64 - before;
        Assert.True(grown < 256L * 1024 * 1024, $"private memory grew by {grown / (1024 * 1024)} MiB");
    }
}
