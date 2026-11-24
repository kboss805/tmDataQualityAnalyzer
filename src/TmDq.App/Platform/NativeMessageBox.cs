using System;
using System.Runtime.InteropServices;

namespace TmDq.App.Platform;

/// <summary>
/// The operating system's own message box, for the one message that cannot wait for Avalonia: the application is closing
/// on an unhandled exception, and its windows may be the thing that failed. Blocks until the user dismisses it.
/// </summary>
public static class NativeMessageBox
{
    private const uint IconError = 0x00000010;
    private const uint TopMost = 0x00040000;

    [DllImport("user32.dll", EntryPoint = "MessageBoxW", CharSet = CharSet.Unicode, ExactSpelling = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static extern int MessageBox(IntPtr owner, string text, string caption, uint type);

    /// <summary>Shows <paramref name="text"/> under <paramref name="caption"/> and waits for OK.</summary>
    public static void Show(string caption, string text) =>
        _ = MessageBox(IntPtr.Zero, text, caption, IconError | TopMost);   // OK is the only button: the answer says nothing
}
