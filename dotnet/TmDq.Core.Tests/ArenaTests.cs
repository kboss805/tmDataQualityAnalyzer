using System;
using System.Diagnostics;
using System.Text;
using TmDq.Core;
using Xunit;

namespace TmDq.Core.Tests;

public unsafe class ArenaTests
{
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
