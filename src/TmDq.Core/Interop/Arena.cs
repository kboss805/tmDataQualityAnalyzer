using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace TmDq.Core;

/// <summary>
/// Native memory for the arguments of one call: UTF-8 strings and arrays, freed together. The library copies
/// what it keeps before a call returns, so an arena lives exactly as long as the call that uses it.
/// </summary>
internal sealed unsafe class Arena : IDisposable
{
    /// <summary>UTF-8 that throws on a lone surrogate instead of writing U+FFFD for it.</summary>
    private static readonly UTF8Encoding s_strictUtf8 = new(encoderShouldEmitUTF8Identifier: false, throwOnInvalidBytes: true);

    private readonly List<IntPtr> _blocks = new();

    /// <summary>A zeroed block of <paramref name="bytes"/> bytes.</summary>
    public void* Alloc(nuint bytes)
    {
        void* p = NativeMemory.AllocZeroed(bytes == 0 ? 1 : bytes);
        _blocks.Add((IntPtr)p);
        return p;
    }

    /// <summary>An array of <paramref name="count"/> zeroed <typeparamref name="T"/>.</summary>
    /// <exception cref="OverflowException">The array would be larger than the address space.</exception>
    /// <exception cref="OutOfMemoryException">There is no block that large.</exception>
    public T* Alloc<T>(int count) where T : unmanaged => (T*)Alloc(Bytes(count, (nuint)sizeof(T)));

    /// <summary>A native copy of <paramref name="values"/>, or null for none (the library reads nothing from a pointer
    /// whose count is 0).</summary>
    public T* Copy<T>(IReadOnlyList<T> values) where T : unmanaged
    {
        if (values.Count == 0)
        {
            return null;
        }

        T* copy = Alloc<T>(values.Count);
        if (values is T[] array)
        {
            array.CopyTo(new Span<T>(copy, array.Length));   // the plot's samples: one block copy, not an indexer call a value
            return copy;
        }

        for (int i = 0; i < values.Count; i++)
        {
            copy[i] = values[i];
        }
        return copy;
    }

    /// <summary>An array of <paramref name="count"/> zeroed pointers to <typeparamref name="T"/>, for the arrays of
    /// structs the library takes by pointer.</summary>
    /// <exception cref="OverflowException">The array would be larger than the address space.</exception>
    public T** AllocPointers<T>(int count) where T : unmanaged => (T**)Alloc(Bytes(count, (nuint)sizeof(void*)));

    /// <summary>
    /// The bytes of <paramref name="count"/> elements of <paramref name="size"/> bytes each, none for a negative count. In
    /// pointer-sized arithmetic, checked: in 32 bits, 536,870,912 doubles came to 0 bytes, and the copy then wrote 4 GB
    /// into a block of one.
    /// </summary>
    /// <exception cref="OverflowException">The product does not fit in the address space.</exception>
    internal static nuint Bytes(int count, nuint size) => checked((nuint)Math.Max(count, 0) * size);

    /// <summary>A native block holding <paramref name="value"/>, and the pointer to it.</summary>
    public T* Box<T>(T value) where T : unmanaged
    {
        T* one = Alloc<T>(1);
        *one = value;
        return one;
    }

    /// <summary>
    /// A NUL-terminated UTF-8 copy of <paramref name="text"/>, or null for a null string. Text UTF-8 cannot carry as it
    /// is, is refused rather than changed: a lone UTF-16 surrogate (which a Windows file name may hold) would become
    /// U+FFFD, and a NUL would end the text early, so the core would be handed another path or name than the one asked for.
    /// </summary>
    /// <exception cref="TmdqException"><see cref="Status.BadArgument"/>, saying why, for such text (the callers' own
    /// failures are TmdqException too, so a host reports it as it reports a file the core cannot open).</exception>
    public byte* Utf8(string? text)
    {
        if (text is null)
        {
            return null;
        }

        if (text.Contains('\0', StringComparison.Ordinal))
        {
            throw new TmdqException(Status.BadArgument, "the name or path holds a NUL character, which would cut it short");
        }

        int length;
        try
        {
            length = s_strictUtf8.GetByteCount(text);
        }
        catch (EncoderFallbackException e)
        {
            throw new TmdqException(Status.BadArgument,
                $"the name or path holds a character that is not valid Unicode (a lone surrogate, U+{(int)e.CharUnknown:X4})");
        }

        byte* p = (byte*)Alloc((nuint)length + 1);
        s_strictUtf8.GetBytes(text, new Span<byte>(p, length));
        p[length] = 0;
        return p;
    }

    public void Dispose()
    {
        foreach (IntPtr block in _blocks)
        {
            NativeMemory.Free((void*)block);
        }
        _blocks.Clear();
    }
}
