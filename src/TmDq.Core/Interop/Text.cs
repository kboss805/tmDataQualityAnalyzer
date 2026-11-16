using System;
using System.Runtime.InteropServices;
using System.Text;

namespace TmDq.Core;

internal static unsafe class Text
{
    /// <summary>A UTF-8 string the library owns, or an empty string for null.</summary>
    public static string Read(byte* utf8) => utf8 is null ? string.Empty : Marshal.PtrToStringUTF8((IntPtr)utf8) ?? string.Empty;

    /// <summary>
    /// Text a function writes into a buffer the caller provides, snprintf-style: asked once with no buffer for the
    /// length it needs, then again with a buffer of that size. A negative return is an error.
    /// </summary>
    public static string Snprintf(Func<IntPtr, int, int> call)
    {
        int needed = call(IntPtr.Zero, 0);
        if (needed < 0)
        {
            throw new TmdqException(Status.BadArgument, Library.LastError());
        }
        if (needed == 0)
        {
            return string.Empty;
        }

        var buffer = new byte[needed + 1];
        fixed (byte* p = buffer)
        {
            int written = call((IntPtr)p, buffer.Length);
            if (written < 0)
            {
                throw new TmdqException(Status.BadArgument, Library.LastError());
            }
            return Encoding.UTF8.GetString(buffer, 0, Math.Min(written, buffer.Length - 1));
        }
    }
}
