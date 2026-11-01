using System.IO;

namespace TmDq.App.Plot
{
    /// <summary>An image format the plot is exported in.</summary>
    public enum ImageFormat
    {
        Png,
        Svg,
        Pdf,
    }

    /// <summary>Which format a plot image is written in, from its file name (plan step 4.5).</summary>
    public static class ImageFiles
    {
        /// <summary>
        /// The file an image export to <paramref name="path"/> writes, and its format, as the Qt <c>exportImage</c> decides
        /// them: .png and .svg (in any case) are themselves, and anything else is a PDF, with ".pdf" added unless the name
        /// already ends with it. There is no format choice; the suffix is it.
        /// </summary>
        public static (string Path, ImageFormat Format) Resolve(string path)
        {
            string suffix = Path.GetExtension(path).ToLowerInvariant();
            if (suffix == ".png") return (path, ImageFormat.Png);
            if (suffix == ".svg") return (path, ImageFormat.Svg);
            return (suffix == ".pdf" ? path : path + ".pdf", ImageFormat.Pdf);
        }
    }
}
