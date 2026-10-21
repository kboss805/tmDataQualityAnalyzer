using System;
using System.Runtime.InteropServices;

namespace Tmdq.WpfSpike
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct ChartTiming
    {
        public double GeometryMs;
        public double RasterMs;
        public double GpuWaitMs;
        public int Segments;
    }

    /// <summary>P/Invoke surface of native\tmdq_chart.dll.</summary>
    internal static class ChartNative
    {
        private const string Dll = "tmdq_chart.dll";

        [DllImport(Dll)] public static extern IntPtr tmdq_chart_create(int w, int h);
        [DllImport(Dll)] public static extern IntPtr tmdq_chart_surface(IntPtr chart);
        [DllImport(Dll)] public static extern int tmdq_chart_is_hardware(IntPtr chart);
        [DllImport(Dll)] public static extern int tmdq_chart_resize(IntPtr chart, int w, int h);
        [DllImport(Dll)] public static extern void tmdq_chart_generate(IntPtr chart, int series, int samples);
        [DllImport(Dll)] public static extern int tmdq_chart_render(IntPtr chart, double xl, double xu, double yl, double yu,
                                                                    uint marker, int fence, out ChartTiming timing);
        [DllImport(Dll)] public static extern void tmdq_chart_destroy(IntPtr chart);

        [DllImport(Dll, EntryPoint = "tmdq_chart_error")] private static extern IntPtr ErrorPtr(IntPtr chart);
        [DllImport(Dll, EntryPoint = "tmdq_chart_adapter")] private static extern IntPtr AdapterPtr(IntPtr chart);

        public static string Error(IntPtr chart) => Marshal.PtrToStringUTF8(ErrorPtr(chart)) ?? "";
        public static string Adapter(IntPtr chart) => Marshal.PtrToStringUTF8(AdapterPtr(chart)) ?? "";
    }
}
