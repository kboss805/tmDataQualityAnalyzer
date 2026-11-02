using System;
using System.IO;

namespace TmDq.App.Services
{
    /// <summary>
    /// Help &gt; User Manual, as the Qt application opens it: the full illustrated manual when the installer put it beside
    /// the application (an optional component, so its absence is normal), else the base manual, compiled in and written
    /// to the temporary folder for the browser to open.
    /// </summary>
    public static class HelpManual
    {
        public const string FullManualName = "UserManual.html";
        public const string BaseManualResource = "usermanual.html";
        public const string TempName = "tmDataQualityAnalyzer_manual.html";

        /// <summary>The full manual beside the application, or null when it is not installed.</summary>
        public static string? Installed(string appRoot)
        {
            string path = Path.Combine(appRoot, FullManualName);
            return File.Exists(path) ? path : null;
        }

        /// <summary>The manual to open: the full one if installed, else the base one written to
        /// <paramref name="tempFolder"/>; null when it could not be written.</summary>
        public static string? PathToOpen(string appRoot, string tempFolder)
        {
            if (Installed(appRoot) is { } full) return full;
            string target = Path.Combine(tempFolder, TempName);
            try
            {
                using Stream source = typeof(HelpManual).Assembly.GetManifestResourceStream(BaseManualResource)
                                      ?? throw new FileNotFoundException(BaseManualResource);
                using FileStream file = File.Create(target);
                source.CopyTo(file);
                return target;
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                return null;
            }
        }
    }
}
