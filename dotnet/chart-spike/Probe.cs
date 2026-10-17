using System;
using Microsoft.Graphics.Canvas;
using Windows.Foundation;

internal static class Probe
{
    private static int Main()
    {
        try
        {
            var device = CanvasDevice.GetSharedDevice();
            using (var rt = new CanvasRenderTarget(device, 1200, 700, 96))
            {
                using (var ds = rt.CreateDrawingSession())
                {
                    ds.Clear(Windows.UI.Color.FromArgb(255, 255, 255, 255));
                    ds.DrawLine(10, 10, 1190, 690, Windows.UI.Color.FromArgb(255, 0, 0, 0), 1.5f);
                }
                Console.WriteLine("Win2D OK: offscreen {0}x{1} rendered, device={2}",
                    rt.SizeInPixels.Width, rt.SizeInPixels.Height, device.GetType().Name);
            }
            return 0;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine("Win2D FAILED: " + e.GetType().Name + ": " + e.Message);
            return 1;
        }
    }
}
