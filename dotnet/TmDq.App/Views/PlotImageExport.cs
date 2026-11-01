using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Skia.Helpers;
using SkiaSharp;
using TmDq.App.Plot;

namespace TmDq.App.Views
{
    /// <summary>
    /// The plot as an image file (plan step 4.5), the Qt <c>PlotWidget::exportImage</c>: the chart's data layer at its
    /// size on screen, without the crosshair, the zoom band or the chips, and the legend where it is on the chart (dragged
    /// and scrolled as shown) unless it is hidden. The file's suffix picks the format: .png, .svg, anything else PDF,
    /// with ".pdf" added when missing. One routine draws all three, onto a Skia canvas: a raster surface, an SVG canvas
    /// or a PDF page sized in points, so the screen and every format share one renderer.
    /// </summary>
    public static class PlotImageExport
    {
        /// <summary>Writes the plot in <paramref name="view"/> to <paramref name="path"/>; the file written and its format,
        /// or null when it could not be written.</summary>
        public static async Task<(string Path, ImageFormat Format)?> ExportAsync(PlotView view, string path)
        {
            (string file, ImageFormat format) = ImageFiles.Resolve(path);
            Size size = view.Chart.Bounds.Size;
            int width = Math.Max(1, (int)Math.Round(size.Width)), height = Math.Max(1, (int)Math.Round(size.Height));
            try
            {
                using FileStream stream = File.Create(file);
                switch (format)
                {
                    case ImageFormat.Png:
                    {
                        using SKSurface surface = SKSurface.Create(new SKImageInfo(width, height, SKColorType.Bgra8888, SKAlphaType.Premul));
                        surface.Canvas.Clear(SKColors.Transparent);
                        await DrawAsync(surface.Canvas, view, size);
                        using SKImage image = surface.Snapshot();
                        using SKData png = image.Encode(SKEncodedImageFormat.Png, 100);
                        png.SaveTo(stream);
                        break;
                    }
                    case ImageFormat.Svg:
                    {
                        using SKCanvas canvas = SKSvgCanvas.Create(new SKRect(0, 0, width, height), stream);
                        await DrawAsync(canvas, view, size);
                        break;
                    }
                    default:
                    {
                        using SKDocument document = SKDocument.CreatePdf(stream, new SKDocumentPdfMetadata
                        {
                            Title = view.Plot?.Title ?? string.Empty,
                            Creator = "tmDataQualityAnalyzer",
                        });
                        SKCanvas canvas = document.BeginPage(width, height);
                        await DrawAsync(canvas, view, size);
                        document.EndPage();
                        document.Close();
                        break;
                    }
                }
                return (file, format);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                return null;
            }
        }

        /// <summary>The chart's data layer, then the legend at its place on the chart.</summary>
        private static async Task DrawAsync(SKCanvas canvas, PlotView view, Size size)
        {
            var frame = new ChartFrame(view.Chart, size);
            frame.Measure(size);
            frame.Arrange(new Rect(size));
            await DrawingContextHelper.RenderAsync(canvas, frame);

            // The legend is drawn as a visual in its own right, so its scroll is kept. That draws it at its own origin with
            // the canvas's transform replaced, and clips to its bounds unless told otherwise (which carry its place on the
            // chart): so it is recorded at its origin, clipped to its size, and the recording, still vector, is played back
            // at its place.
            PlotLegend legend = view.Legend;
            if (!legend.IsVisible) return;
            Size box = legend.Bounds.Size;
            using var recorder = new SKPictureRecorder();
            SKCanvas recording = recorder.BeginRecording(new SKRect(0, 0, (float)box.Width, (float)box.Height));
            await DrawingContextHelper.RenderAsync(recording, legend, new Rect(box), new Vector(96, 96));
            using SKPicture picture = recorder.EndRecording();
            canvas.DrawPicture(picture, (float)legend.Position.X, (float)legend.Position.Y);
        }

        /// <summary>A stand-in the size of the chart that draws what an export shows of it.</summary>
        private sealed class ChartFrame : Control
        {
            private readonly PlotChart _chart;
            private readonly Size _size;

            public ChartFrame(PlotChart chart, Size size)
            {
                _chart = chart;
                _size = size;
            }

            public override void Render(DrawingContext context) => _chart.RenderTo(context, _size);
        }
    }
}
