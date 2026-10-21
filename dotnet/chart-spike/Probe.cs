// Probe.cs - can Win2D render offscreen in an UNPACKAGED app?
//
// Initializes the Windows App Runtime EXPLICITLY rather than relying on the
// auto-initializer. The auto-initializer terminates the process on failure with no
// message - observed as a silent exit 22 - which tells you nothing about which runtime
// family it wanted or why it refused. TryInitialize hands back an HRESULT instead.
//
// Targets the 1.8 family because that is the newest WindowsAppRuntime installed here.
// Note Win2D has NO 2.x package (newest is 1.4.0), so the 2.5.1 runtime cannot serve it
// regardless - Win2D is a 1.x component.

using System;
using Microsoft.Graphics.Canvas;
using Microsoft.Windows.ApplicationModel.DynamicDependency;

internal static class Probe
{
    /// 1.8 packed as major<<16 | minor, the form TryInitialize expects.
    private const uint MajorMinor18 = 0x00010008;

    private static int Main()
    {
        int hr;
        if (!Bootstrap.TryInitialize(MajorMinor18, out hr))
        {
            Console.Error.WriteLine(
                "Bootstrap.TryInitialize(1.8) failed: HRESULT 0x{0:X8}", hr);
            Console.Error.WriteLine(
                "  This is the error the auto-initializer swallowed behind exit 22.");
            return 2;
        }
        Console.WriteLine("Windows App Runtime 1.8 initialized");

        try
        {
            var device = CanvasDevice.GetSharedDevice();
            using (var rt = new CanvasRenderTarget(device, 1200, 700, 96))
            {
                using (var ds = rt.CreateDrawingSession())
                {
                    ds.Clear(Windows.UI.Color.FromArgb(255, 255, 255, 255));
                    ds.DrawLine(10, 10, 1190, 690,
                                Windows.UI.Color.FromArgb(255, 0, 0, 0), 1.5f);
                }
                Console.WriteLine("Win2D OK: offscreen {0}x{1} rendered",
                                  rt.SizeInPixels.Width, rt.SizeInPixels.Height);
            }
            return 0;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine("Win2D FAILED: " + e.GetType().Name + ": " + e.Message);
            return 1;
        }
        finally
        {
            Bootstrap.Shutdown();
        }
    }
}
