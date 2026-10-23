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
    private readonly List<IntPtr> _blocks = new();

    /// <summary>A zeroed block of <paramref name="bytes"/> bytes.</summary>
    public void* Alloc(nuint bytes)
    {
        void* p = NativeMemory.AllocZeroed(bytes == 0 ? 1 : bytes);
        _blocks.Add((IntPtr)p);
        return p;
    }

    /// <summary>An array of <paramref name="count"/> zeroed <typeparamref name="T"/>.</summary>
    public T* Alloc<T>(int count) where T : unmanaged => (T*)Alloc((nuint)(Math.Max(count, 0) * sizeof(T)));

    /// <summary>A NUL-terminated UTF-8 copy of <paramref name="text"/>, or null for a null string.</summary>
    public byte* Utf8(string? text)
    {
        if (text is null)
        {
            return null;
        }

        int length = Encoding.UTF8.GetByteCount(text);
        byte* p = (byte*)Alloc((nuint)length + 1);
        Encoding.UTF8.GetBytes(text, new Span<byte>(p, length));
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

internal static unsafe class Text
{
    /// <summary>A UTF-8 string the library owns, or an empty string for null.</summary>
    public static string Read(byte* utf8) => utf8 is null ? string.Empty : Marshal.PtrToStringUTF8((IntPtr)utf8) ?? string.Empty;
}
