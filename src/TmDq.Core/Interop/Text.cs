using System;
using System.Runtime.InteropServices;
using System.Text;

namespace TmDq.Core;

internal static unsafe class Text
{
    /// <summary>A UTF-8 string the library owns, or an empty string for null.</summary>
    public static string Read(byte* utf8) => utf8 is null ? string.Empty : Marshal.PtrToStringUTF8((IntPtr)utf8) ?? string.Empty;

    /// <summary>How many buffers a two-call helper (<see cref="Snprintf"/>, the chart's ticks) tries, each sized to the
    /// last answer, before it gives up on an answer that keeps growing.</summary>
    internal const int BufferAttempts = 4;

    /// <summary>
    /// Text a function writes into a buffer the caller provides, snprintf-style: asked once with no buffer for the
    /// length it needs, then again with a buffer of that size. An answer that no longer fits (the text grew between the
    /// calls; the function wrote what fitted) is asked again with a buffer of its new size, up to
    /// <see cref="BufferAttempts"/> buffers. A negative return is an error.
    /// </summary>
    /// <exception cref="TmdqException"><see cref="Status.BadArgument"/> for a negative return; <see cref="Status.Internal"/>
    /// for text that still did not fit the last buffer.</exception>
    public static string Snprintf(Func<IntPtr, int, int> call)
    {
        int needed = Library.NonNegative(call(IntPtr.Zero, 0));
        if (needed == 0)
        {
            return string.Empty;
        }

        for (int attempt = 0; attempt < BufferAttempts; attempt++)
        {
            var buffer = new byte[checked(needed + 1)];
            fixed (byte* p = buffer)
            {
                int written = Library.NonNegative(call((IntPtr)p, buffer.Length));
                if (written < buffer.Length)
                {
                    return Encoding.UTF8.GetString(buffer, 0, written);
                }
                needed = written;   // truncated: it is longer now than it was when measured
            }
        }
        throw new TmdqException(Status.Internal, $"the text kept growing between the calls: {BufferAttempts} buffers were too small for it");
    }
}
