using System;
using System.Runtime.InteropServices;

namespace TmDq.App.Tests
{
    /// <summary>The kernel32 calls that read a resource out of an executable without running it, shared by the tests that
    /// look inside the built exe (its icon, its manifest).</summary>
    internal static class PeResources
    {
        public const uint LoadLibraryAsDatafile = 0x2, LoadLibraryAsImageResource = 0x20;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool FreeLibrary(IntPtr module);
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr FindResourceW(IntPtr module, IntPtr name, IntPtr type);
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
        [DllImport("kernel32.dll")]
        public static extern IntPtr LockResource(IntPtr data);
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern uint SizeofResource(IntPtr module, IntPtr resource);
        [DllImport("kernel32.dll")]
        public static extern uint GetACP();
    }
}
