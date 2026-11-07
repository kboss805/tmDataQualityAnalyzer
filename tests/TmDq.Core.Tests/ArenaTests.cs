using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using TmDq.Core;
using Xunit;

namespace TmDq.Core.Tests;

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

    [Fact]
    public void ALengthThatChangesBetweenTheCallsIsReadAsFarAsWasWritten()
    {
        // The second call reports more than fits: only what fits is read (the library truncated it).
        Assert.Equal("hello", Text.Snprintf(Writes("hello", reported: n => n == 5 ? 5 : 99)) );
        int call = 0;
        string grew = Text.Snprintf(Writes("hello", reported: n => ++call == 1 ? n : n + 10));
        Assert.Equal("hello", grew);

        // The second call reports less: only that much is read.
        call = 0;
        Assert.Equal("he", Text.Snprintf(Writes("hello", reported: n => ++call == 1 ? n : 2)));
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
