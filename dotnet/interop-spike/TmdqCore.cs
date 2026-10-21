// TmdqCore.cs - managed side of the interop spike.
//
// P/Invoke declarations and a thin wrapper over the flat C ABI in tmdq_abi.h.
//
// DELIBERATELY CONSERVATIVE C#. This file must compile under BOTH the modern .NET SDK
// and the older compiler PowerShell's Add-Type uses, because this machine currently has
// the .NET runtimes but NO SDK - so Add-Type is the only way to actually run managed
// code here today. That means:
//
//   - DllImport, not [LibraryImport] (the .NET 7+ source generator).
//   - IntPtr and Marshal.Copy, not Span<T> / NativeMemory.
//   - UTF-8 encoded by hand, because Marshal.StringToCoTaskMemUTF8 is .NET Core 2.1+.
//   - No nullable reference types, file-scoped namespaces or top-level statements.
//
// None of that weakens what the spike proves: DllImport marshalling of blittable
// structs and pointer-plus-length arrays is the same mechanism on .NET Framework and
// on .NET 10. What it does NOT exercise is the modern zero-copy path (Span<T> over the
// returned arrays, which would skip the Marshal.Copy below), and that is worth
// re-measuring once an SDK is installed.

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Tmdq.Interop
{
    public enum TmdqStatus
    {
        Ok = 0,
        BadArg = 1,
        Prepare = 2,
        Process = 3,
        NoSamples = 4,
        Internal = 5
    }

    /// Mirrors tmdq_lock_request. Pack = 8 matches the native #pragma pack(push, 8).
    /// Field ORDER and TYPES must match exactly; TmdqCore.CheckLayout() asserts the
    /// resulting size against the C compiler's, because a silent offset shift here
    /// reads plausible garbage rather than failing.
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    public struct LockRequest
    {
        public IntPtr FilenameUtf8;
        public int TimeChannelId;
        public int PcmChannelId;
        public ulong FrameSync;
        public ulong FrameSyncMask;
        public int SyncPatternLength;
        public int WordsInMinorFrame;
        public int BitsInMinorFrame;
        public double SamplePeriodSec;
        public int SwapBytes;
        public int IsRandomized;
        public int IsInverted;
    }

    /// Mirrors tmdq_lock_result. The three pointers are owned by the native side until
    /// FreeLockResult.
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    public struct LockResult
    {
        public int SampleCount;
        public IntPtr TimesSec;
        public IntPtr LockPercent;
        public IntPtr MissedFrames;
        public double ElapsedMs;
    }

    /// Mirrors tmdq_file_info, including the fixed inline array.
    ///
    /// Named Ch10FileInfo, not FileInfo: the latter collides with System.IO.FileInfo
    /// (error CS0104) the moment a file writes "using System.IO", which Program.cs does.
    /// The native side needs no rename - tmdq_ prefixes already keep C names distinct.
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    public struct Ch10FileInfo
    {
        public int TimeChannelId;
        public int PcmChannelCount;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = TmdqCore.MaxPcmChannels)]
        public int[] PcmChannelIds;
    }

    public static class TmdqCore
    {
        public const int MaxPcmChannels = 32;
        public const int ExpectedAbiVersion = 1;

        private const string Dll = "tmdq_core";

        [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr LoadLibraryExW(string path, IntPtr unused, uint flags);

        /// Makes the DLL's own directory the first place its dependencies are looked for.
        /// Without this the loader does not search there, so Qt6Core.dll sitting beside
        /// tmdq_core.dll would not be found.
        private const uint LoadWithAlteredSearchPath = 0x00000008;

        /// Loads tmdq_core.dll from an explicit path BEFORE any DllImport call.
        ///
        /// Necessary, and the right thing anyway. DllImport("tmdq_core") resolves by
        /// bare name through the OS search order, which does NOT dependably include the
        /// working directory - the spike's first run failed with 0x8007007E
        /// ("module could not be found") even with the DLL and Qt sitting right there.
        /// Loading by absolute path puts the module in the process by the name DllImport
        /// then matches, and avoids the hard-coded-PATH rot that caused this project's
        /// 0xC0000135 after the Qt 6.11.1 bump.
        ///
        /// @return null on success, otherwise a message with the Win32 error.
        public static string Preload(string dllPath)
        {
            IntPtr h = LoadLibraryExW(dllPath, IntPtr.Zero, LoadWithAlteredSearchPath);
            if (h != IntPtr.Zero) { return null; }
            int err = Marshal.GetLastWin32Error();
            return "LoadLibraryEx failed for '" + dllPath + "' with Win32 error " + err
                   + (err == 126 ? " (ERROR_MOD_NOT_FOUND - a DEPENDENCY is missing, not this DLL)"
                                 : string.Empty);
        }

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int tmdq_abi_version();

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int tmdq_sizeof_lock_request();

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int tmdq_sizeof_lock_result();

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int tmdq_sizeof_file_info();

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int tmdq_probe_file(IntPtr filenameUtf8, out Ch10FileInfo info);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int tmdq_run_lock_stream(ref LockRequest req, out LockResult result);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void tmdq_free_lock_result(ref LockResult result);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr tmdq_last_error();

        /// Reads the native thread-local error as UTF-8. Marshal.PtrToStringAnsi would
        /// mis-decode anything non-ASCII, so the bytes are walked by hand - the same
        /// reason the filename is encoded by hand going the other way.
        public static string LastError()
        {
            IntPtr p = tmdq_last_error();
            if (p == IntPtr.Zero) { return string.Empty; }
            int len = 0;
            while (Marshal.ReadByte(p, len) != 0) { len++; }
            if (len == 0) { return string.Empty; }
            byte[] buf = new byte[len];
            Marshal.Copy(p, buf, 0, len);
            return Encoding.UTF8.GetString(buf);
        }

        /// Allocates a NUL-terminated UTF-8 copy of @p s. Caller frees with
        /// Marshal.FreeHGlobal.
        public static IntPtr Utf8Alloc(string s)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(s ?? string.Empty);
            IntPtr p = Marshal.AllocHGlobal(bytes.Length + 1);
            Marshal.Copy(bytes, 0, p, bytes.Length);
            Marshal.WriteByte(p, bytes.Length, 0);
            return p;
        }

        // Managed struct sizes, computed in C# rather than by the caller. PowerShell
        // cannot call Marshal.SizeOf on a type literal - it binds the SizeOf(object)
        // overload and fails with "Type 'System.RuntimeType' cannot be marshaled" - and
        // the managed side owning its own layout question is the better split anyway.
        public static int ManagedSizeOfLockRequest() { return Marshal.SizeOf(typeof(LockRequest)); }
        public static int ManagedSizeOfLockResult()  { return Marshal.SizeOf(typeof(LockResult)); }
        public static int ManagedSizeOfCh10FileInfo() { return Marshal.SizeOf(typeof(Ch10FileInfo)); }

        /// The layout assertion the spike exists to make. Returns null when the managed
        /// and native views of every struct agree, otherwise a description of the first
        /// mismatch. This is the check that would have caught a wrong Pack.
        public static string CheckLayout()
        {
            int v = tmdq_abi_version();
            if (v != ExpectedAbiVersion)
            {
                return "ABI version mismatch: DLL reports " + v + ", this binding expects "
                       + ExpectedAbiVersion;
            }

            int mReq = Marshal.SizeOf(typeof(LockRequest));
            int nReq = tmdq_sizeof_lock_request();
            if (mReq != nReq)
            {
                return "LockRequest size mismatch: managed " + mReq + ", native " + nReq;
            }

            int mRes = Marshal.SizeOf(typeof(LockResult));
            int nRes = tmdq_sizeof_lock_result();
            if (mRes != nRes)
            {
                return "LockResult size mismatch: managed " + mRes + ", native " + nRes;
            }

            int mInfo = Marshal.SizeOf(typeof(Ch10FileInfo));
            int nInfo = tmdq_sizeof_file_info();
            if (mInfo != nInfo)
            {
                return "Ch10FileInfo size mismatch: managed " + mInfo + ", native " + nInfo;
            }

            return null;
        }

        /// Copies one of the result arrays out of native memory.
        public static double[] ReadDoubles(IntPtr p, int count)
        {
            double[] a = new double[count];
            if (count > 0 && p != IntPtr.Zero) { Marshal.Copy(p, a, 0, count); }
            return a;
        }
    }
}
