// HybridDriver - drives the Qt-free tmdq_core.dll from managed code and checks it against the
// numbers the interop spike recorded BEFORE the de-Qt sweep.
//
//   dotnet run -c Release --project dotnet\hybrid-spike\driver -- [fixture.ch10] [tmdq_core.dll]
//
// What makes this more than a smoke test: the interop spike measured, on the same fixture and the
// same stream, 116 samples / 93.532 % mean lock / 1,100 missed frames at a 1 s sample period and
// 11,554 samples / 838 missed / 92.643 % at 10 ms. Every refactor since changed the code that
// produces those numbers (QString -> std::string, QObject -> event sinks, QVector -> std::vector,
// the reader, the worker, FrameSetup, the DTOs). If any step had altered a result it would show
// here, through a different build system, a different threading primitive and a different host.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using Tmdq.Interop;

internal static class Program
{
    private static int s_checks;
    private static int s_failed;

    private static void Check(bool ok, string what, string detail = "")
    {
        s_checks++;
        if (!ok) { s_failed++; }
        Console.WriteLine("  [{0}] {1}{2}", ok ? "ok" : "FAIL", what, detail.Length > 0 ? "  (" + detail + ")" : "");
    }

    [DllImport("kernel32")] private static extern uint GetACP();

    private static LockRequest PrnRequest(IntPtr path, int timeCh, int pcmCh, double period, ulong sync = 0x334AABBFUL)
    {
        // PRN15, as settings/framesync_patterns/framesync_PRN15.toml defines it.
        return new LockRequest
        {
            FilenameUtf8      = path,
            TimeChannelId     = timeCh,
            PcmChannelId      = pcmCh,
            FrameSync         = sync,
            FrameSyncMask     = 0xFFFFFFFFUL,
            SyncPatternLength = 32,
            BitsInMinorFrame  = 32767,
            WordsInMinorFrame = 2048,
            SamplePeriodSec   = period,
            SwapBytes         = 1,
            IsRandomized      = 0,
            IsInverted        = 0,
        };
    }

    private sealed class Run
    {
        public int Rc;
        public int Samples;
        public double LockMean, LockMin, LockMax, LastMissed, Span, NativeMs, TotalMs;
        public string Error = "";
    }

    private static Run RunStream(string file, int timeCh, int pcmCh, double period, ulong sync = 0x334AABBFUL)
    {
        IntPtr p = TmdqCore.Utf8Alloc(file);
        try
        {
            LockRequest req = PrnRequest(p, timeCh, pcmCh, period, sync);
            LockResult res;
            Stopwatch sw = Stopwatch.StartNew();
            int rc = TmdqCore.tmdq_run_lock_stream(ref req, out res);
            sw.Stop();
            Run r = new Run { Rc = rc, TotalMs = sw.Elapsed.TotalMilliseconds };
            if (rc != 0) { r.Error = TmdqCore.LastError(); return r; }
            try
            {
                double[] t = TmdqCore.ReadDoubles(res.TimesSec, res.SampleCount);
                double[] l = TmdqCore.ReadDoubles(res.LockPercent, res.SampleCount);
                double[] m = TmdqCore.ReadDoubles(res.MissedFrames, res.SampleCount);
                r.Samples = res.SampleCount;
                r.NativeMs = res.ElapsedMs;
                double sum = 0; r.LockMin = double.MaxValue; r.LockMax = double.MinValue;
                foreach (double v in l) { sum += v; r.LockMin = Math.Min(r.LockMin, v); r.LockMax = Math.Max(r.LockMax, v); }
                r.LockMean = sum / Math.Max(l.Length, 1);
                r.LastMissed = m[m.Length - 1];
                r.Span = t[t.Length - 1] - t[0];
            }
            finally { TmdqCore.tmdq_free_lock_result(ref res); }
            return r;
        }
        finally { Marshal.FreeHGlobal(p); }
    }

    private static string FindDll()
    {
        for (DirectoryInfo d = new DirectoryInfo(AppContext.BaseDirectory); d != null; d = d.Parent)
        {
            string c = Path.Combine(d.FullName, "dotnet", "hybrid-spike", "native", "out", "tmdq_core.dll");
            if (File.Exists(c)) { return c; }
        }
        return null;
    }

    private static string FindFixture()
    {
        for (DirectoryInfo d = new DirectoryInfo(AppContext.BaseDirectory); d != null; d = d.Parent)
        {
            string c = Path.Combine(d.FullName, "tests", "data", "test files", "prn_testfile, sample.ch10");
            if (File.Exists(c)) { return c; }
        }
        return null;
    }

    private static int Main(string[] args)
    {
        string fixture = args.Length > 0 ? args[0] : FindFixture();
        string dll = args.Length > 1 ? args[1] : FindDll();
        if (fixture == null || !File.Exists(fixture)) { Console.Error.WriteLine("fixture not found"); return 2; }
        if (dll == null || !File.Exists(dll)) { Console.Error.WriteLine("tmdq_core.dll not found - run native\\build_core.ps1"); return 2; }

        Console.WriteLine("host: {0}  runtime {1}  active code page {2}", Process.GetCurrentProcess().ProcessName,
                          Environment.Version, GetACP());
        Console.WriteLine("dll : {0} ({1:N0} bytes)", dll, new FileInfo(dll).Length);

        string preload = TmdqCore.Preload(dll);
        if (preload != null) { Console.Error.WriteLine(preload); return 2; }

        Console.WriteLine("\nABI");
        string layout = TmdqCore.CheckLayout();
        Check(layout == null, "abi version and struct layouts agree", layout ?? "version " + TmdqCore.tmdq_abi_version());

        Console.WriteLine("\nprobe");
        Ch10FileInfo info;
        IntPtr fp = TmdqCore.Utf8Alloc(fixture);
        int prc = TmdqCore.tmdq_probe_file(fp, out info);
        Marshal.FreeHGlobal(fp);
        Check(prc == 0, "probe succeeds", prc == 0 ? "" : TmdqCore.LastError());
        if (prc != 0) { return 1; }
        Check(info.TimeChannelId == 1 && info.PcmChannelCount == 4 &&
              info.PcmChannelIds[0] == 13 && info.PcmChannelIds[3] == 16,
              "time channel 1, PCM channels 13..16", info.TimeChannelId + " / " + info.PcmChannelCount + " ch, first " + info.PcmChannelIds[0]);

        Console.WriteLine("\nagainst the interop spike's recorded (pre-sweep) results, ch 13 as PRN15");
        Run a = RunStream(fixture, info.TimeChannelId, 13, 1.0);
        Check(a.Rc == 0, "1 s run succeeds", a.Error);
        Check(a.Samples == 116, "1 s: 116 samples", a.Samples.ToString());
        Check(Math.Abs(a.LockMean - 93.532) < 0.0006, "1 s: mean lock 93.532 %", a.LockMean.ToString("F4"));
        Check(a.LockMin == 0.0 && a.LockMax == 100.0, "1 s: lock spans 0..100", a.LockMin + ".." + a.LockMax);
        Check(a.LastMissed == 1100.0, "1 s: 1,100 missed frames", a.LastMissed.ToString());
        Check(Math.Abs(a.Span - 115.0) < 0.5, "1 s: spans 115 s", a.Span.ToString("F3"));
        Console.WriteLine("      native {0:F1} ms, through P/Invoke {1:F1} ms", a.NativeMs, a.TotalMs);

        Run b = RunStream(fixture, info.TimeChannelId, 13, 0.01);
        Check(b.Rc == 0, "10 ms run succeeds", b.Error);
        Check(b.Samples == 11554, "10 ms: 11,554 samples", b.Samples.ToString());
        Check(b.LastMissed == 838.0, "10 ms: 838 missed frames", b.LastMissed.ToString());
        Check(Math.Abs(b.LockMean - 92.643) < 0.0006, "10 ms: mean lock 92.643 %", b.LockMean.ToString("F4"));
        Console.WriteLine("      native {0:F1} ms, through P/Invoke {1:F1} ms", b.NativeMs, b.TotalMs);

        Console.WriteLine("\nfailures are reported with their own reason");
        Run bad = RunStream(fixture, info.TimeChannelId, 13, 1.0, sync: 0x12345678UL);
        Check(bad.Rc == 3, "a wrong sync pattern is TMDQ_ERR_PROCESS", "rc " + bad.Rc);
        Check(bad.Error.Length > 0, "and says why", "\"" + bad.Error + "\"");
        Run gone = RunStream(Path.Combine(Path.GetTempPath(), "tmdq-no-such-file.ch10"), 1, 13, 1.0);
        Check(gone.Rc == 2, "a missing file is TMDQ_ERR_PREPARE", "rc " + gone.Rc + ", \"" + gone.Error + "\"");

        Console.WriteLine("\ntwo runs at once on separate threads (the UI thread and a worker)");
        Run c1 = null, c2 = null;
        Thread t1 = new Thread(() => c1 = RunStream(fixture, 1, 13, 1.0));
        Thread t2 = new Thread(() => c2 = RunStream(fixture, 1, 13, 1.0));
        Stopwatch both = Stopwatch.StartNew();
        t1.Start(); t2.Start(); t1.Join(); t2.Join();
        both.Stop();
        Check(c1.Rc == 0 && c2.Rc == 0, "both succeed", c1.Error + c2.Error);
        Check(c2.Samples == a.Samples && c2.LastMissed == a.LastMissed, "and so does the second", c2.Samples + " samples");
        Check(c1.Samples == a.Samples && c1.LastMissed == a.LastMissed && Math.Abs(c1.LockMean - a.LockMean) < 1e-9,
              "the same stream twice at once equals one run alone", c1.Samples + " samples, " + c1.LastMissed + " missed");
        Console.WriteLine("      two streams in {0:F0} ms (one alone: {1:F0} ms)", both.Elapsed.TotalMilliseconds, a.TotalMs);

        Console.WriteLine("\na non-ASCII folder name");
        string dir = Path.Combine(Path.GetTempPath(), "tmdq-café-Ω");
        Directory.CreateDirectory(dir);
        string copy = Path.Combine(dir, "recording.ch10");
        File.Copy(fixture, copy, true);
        Run u = RunStream(copy, info.TimeChannelId, 13, 1.0);
        Console.WriteLine("      outcome: rc {0}{1}", u.Rc, u.Rc == 0 ? ", " + u.Samples + " samples" : ", \"" + u.Error + "\"");
        if (GetACP() == 65001)
        {
            Check(u.Rc == 0 && u.Samples == a.Samples, "opens under the UTF-8 active code page", "code page 65001");
        }
        else
        {
            Console.WriteLine("      (active code page {0}, not UTF-8: reported, not asserted)", GetACP());
        }
        try { Directory.Delete(dir, true); } catch { }

        Console.WriteLine("\n{0} of {1} checks passed", s_checks - s_failed, s_checks);
        return s_failed == 0 ? 0 : 1;
    }
}
