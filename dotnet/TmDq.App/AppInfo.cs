using System.Reflection;

namespace TmDq.App
{
    /// <summary>What the application says about itself. The version is the assembly's, which the build reads
    /// from AppVersion in include/constants.h (dotnet/build/AppVersion.props), so it is never written here.</summary>
    public static class AppInfo
    {
        /// <summary>The main window's title, as the Qt application has it (MainView).</summary>
        public const string Title = "TM Data Quality Analyzer";

        /// <summary>Major.minor.patch, e.g. "2.14.2".</summary>
        public static string Version { get; } =
            typeof(AppInfo).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion
            ?? "0.0.0";
    }
}
