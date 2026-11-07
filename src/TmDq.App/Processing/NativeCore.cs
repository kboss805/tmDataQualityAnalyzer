using System;
using System.IO;
using TmDq.Core;

namespace TmDq.App.Processing
{
    /// <summary>Where the application finds tmdq_core.dll, and loading it once.</summary>
    public static class NativeCore
    {
        /// <summary>TMDQ_CORE_DLL if set, else tmdq_core.dll beside the application (the build and the packages put it
        /// there; it is kept out of the single-file bundle, because a native library has to be on disk to load).</summary>
        public static string DllPath()
        {
            string? fromEnv = Environment.GetEnvironmentVariable("TMDQ_CORE_DLL");
            return string.IsNullOrEmpty(fromEnv) ? Path.Combine(AppContext.BaseDirectory, "tmdq_core.dll") : fromEnv;
        }

        /// <summary>Loads and verifies the DLL; a second call does nothing.</summary>
        /// <exception cref="FileNotFoundException">There is no tmdq_core.dll where <see cref="DllPath"/> says.</exception>
        public static void EnsureLoaded() => Library.Load(DllPath());
    }
}
