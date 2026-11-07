using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using TmDq.Core;

namespace TmDq.Core.Tests;

/// <summary>Where the DLL and the committed recordings are, and a one-time load of the DLL.</summary>
internal static class Env
{
    private static readonly object s_lock = new();
    private static bool s_loaded;

    public static string RepoRoot { get; } = FindRepoRoot();

    [DllImport("kernel32")]
    private static extern uint GetACP();

    public static uint ActiveCodePage => GetACP();

    /// <summary>Loads tmdq_core.dll once per process. Call at the start of any test that uses the library.</summary>
    public static void EnsureLoaded()
    {
        lock (s_lock)
        {
            if (s_loaded)
            {
                return;
            }
            Library.Load(DllPath());
            s_loaded = true;
        }
    }

    /// <summary>TMDQ_CORE_DLL if set, else the Release build, else the Debug one.</summary>
    public static string DllPath()
    {
        string? fromEnv = Environment.GetEnvironmentVariable("TMDQ_CORE_DLL");
        if (!string.IsNullOrEmpty(fromEnv))
        {
            return fromEnv;
        }

        foreach (string config in new[] { "Release", "Debug" })
        {
            string candidate = Path.Combine(RepoRoot, "native", "out", config, "tmdq_core.dll");
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }
        throw new FileNotFoundException(
            "tmdq_core.dll has not been built: run scripts\\build_core_dll.ps1 (or set TMDQ_CORE_DLL)");
    }

    /// <summary>A committed recording under tests/data/test files.</summary>
    public static string Recording(string name)
    {
        string path = Path.Combine(RepoRoot, "tests", "data", "test files", name);
        if (!File.Exists(path))
        {
            throw new FileNotFoundException("a committed test recording is missing", path);
        }
        return path;
    }

    public static string Prn => Recording("prn_testfile, sample.ch10");
    public static string StepCal => Recording("step_cal, example.ch10");
    public static string Trc => Recording("agc_rnrz-l_trc_testfile.ch10");
    public static string Rasa => Recording("agc_nzl_rasa_testfile.ch10");
    public static string Safran => Recording("safran_testfile, sample.ch10");

    private static string FindRepoRoot()
    {
        for (DirectoryInfo? d = new(AppContext.BaseDirectory); d is not null; d = d.Parent)
        {
            if (File.Exists(Path.Combine(d.FullName, "tmdq.sln")))
            {
                return d.FullName;
            }
        }
        throw new DirectoryNotFoundException("the repository root (the folder holding tmdq.sln) was not found above " + AppContext.BaseDirectory);
    }
}
