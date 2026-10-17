// Program.cs - the same spike as run_spike.ps1, as a real console app.
//
// NOT RUNNABLE ON THIS MACHINE YET. There is no .NET SDK installed here (runtimes 6, 8
// and 10 are present, but C:\Program Files\dotnet\sdk does not exist and only VS
// BuildTools is installed, so there is no .NET workload either). run_spike.ps1 drives
// the identical ABI through PowerShell's Add-Type instead, which is how the spike was
// actually measured.
//
// This file is committed so that the moment an SDK lands, `dotnet run` reproduces the
// same checks - and so the modern path the PowerShell route CANNOT exercise is already
// written down: see ReadDoublesSpan below, which skips the Marshal.Copy entirely.
//
//   dotnet run --project dotnet/interop-spike/InteropSpike.csproj -- "<path to .ch10>"

using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using Tmdq.Interop;

internal static class Program
{
    private static int Main(string[] args)
    {
        string fixturePath = args.Length > 0
            ? args[0]
            : Path.Combine("tests", "data", "test files", "prn_testfile, sample.ch10");

        if (!File.Exists(fixturePath))
        {
            Console.Error.WriteLine("fixture not found: " + fixturePath);
            return 1;
        }

        // Same reason as run_spike.ps1: DllImport resolves by bare name through the OS
        // search order, which does not dependably include the working directory.
        string dll = Path.GetFullPath(Path.Combine(
            AppContext.BaseDirectory, "..", "..", "..", "release", "tmdq_core.dll"));
        if (File.Exists(dll))
        {
            string preload = TmdqCore.Preload(dll);
            if (preload != null) { Console.Error.WriteLine(preload); return 1; }
        }

        string layout = TmdqCore.CheckLayout();
        if (layout != null)
        {
            Console.Error.WriteLine("layout disagrees: " + layout);
            return 1;
        }
        Console.WriteLine("abi version {0}, struct layout agrees", TmdqCore.tmdq_abi_version());

        IntPtr pathPtr = TmdqCore.Utf8Alloc(fixturePath);
        try
        {
            Ch10FileInfo info;
            int rc = TmdqCore.tmdq_probe_file(pathPtr, out info);
            if (rc != 0)
            {
                Console.Error.WriteLine("probe failed: " + TmdqCore.LastError());
                return 1;
            }
            Console.WriteLine("time channel {0}, {1} pcm channels",
                              info.TimeChannelId, info.PcmChannelCount);

            // PRN15, as settings/framesync_patterns/framesync_PRN15.toml defines it.
            LockRequest req = new LockRequest
            {
                FilenameUtf8      = pathPtr,
                TimeChannelId     = info.TimeChannelId,
                PcmChannelId      = info.PcmChannelIds[0],
                FrameSync         = 0x334AABBFUL,
                FrameSyncMask     = 0xFFFFFFFFUL,
                SyncPatternLength = 32,
                BitsInMinorFrame  = 32767,
                WordsInMinorFrame = 2048,
                SamplePeriodSec   = 0.01,
                SwapBytes         = 1,
                IsRandomized      = 0,
                IsInverted        = 0,
            };

            LockResult res;
            Stopwatch sw = Stopwatch.StartNew();
            rc = TmdqCore.tmdq_run_lock_stream(ref req, out res);
            sw.Stop();
            if (rc != 0)
            {
                Console.Error.WriteLine("run failed: " + TmdqCore.LastError());
                return 1;
            }

            try
            {
                Stopwatch copy = Stopwatch.StartNew();
                double[] times = TmdqCore.ReadDoubles(res.TimesSec, res.SampleCount);
                double[] lock_ = TmdqCore.ReadDoubles(res.LockPercent, res.SampleCount);
                double[] missed = TmdqCore.ReadDoubles(res.MissedFrames, res.SampleCount);
                copy.Stop();

                Console.WriteLine("{0} samples, native {1:F1} ms, total {2:F1} ms, copy {3:F2} ms",
                    res.SampleCount, res.ElapsedMs, sw.Elapsed.TotalMilliseconds,
                    copy.Elapsed.TotalMilliseconds);
                Console.WriteLine("span {0:F3}..{1:F3}s, last missed {2}",
                    times[0], times[times.Length - 1], missed[missed.Length - 1]);

                // THE MODERN PATH, which run_spike.ps1 cannot reach. No copy at all -
                // the span aliases native memory directly, valid only until
                // tmdq_free_lock_result. Worth measuring against the Marshal.Copy above:
                // if it is materially faster, the real shell should read results this way.
                Stopwatch spanSw = Stopwatch.StartNew();
                double sum = 0;
                ReadOnlySpan<double> view = ReadDoublesSpan(res.LockPercent, res.SampleCount);
                for (int i = 0; i < view.Length; i++) { sum += view[i]; }
                spanSw.Stop();
                Console.WriteLine("span-based mean {0:F3} in {1:F3} ms (zero copy)",
                    sum / Math.Max(view.Length, 1), spanSw.Elapsed.TotalMilliseconds);
            }
            finally
            {
                TmdqCore.tmdq_free_lock_result(ref res);
            }
        }
        finally
        {
            Marshal.FreeHGlobal(pathPtr);
        }

        Console.WriteLine("spike passed");
        return 0;
    }

    /// Aliases native memory as a span - no allocation, no copy. Only valid while the
    /// native result is still alive.
    private static unsafe ReadOnlySpan<double> ReadDoublesSpan(IntPtr p, int count)
    {
        return new ReadOnlySpan<double>((void*)p, count);
    }
}
