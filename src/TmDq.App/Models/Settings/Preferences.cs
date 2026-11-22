using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Serialization;

namespace TmDq.App.Models.Settings;

/// <summary>
/// What the application remembers between sessions (plan step 4.8): the theme, the log sidebar, the plot's legend,
/// the recent files, the folder the last recording came from and the folder the last calibration recording came
/// from. (v2.x kept the same things in an INI file, which v3.0.x imported on its first run; from v3.1.0 that
/// file is not read.)
/// </summary>
public sealed record Preferences
{
    public const string DarkTheme = "dark";
    public const string LightTheme = "light";

    /// <summary>"dark" or "light"; dark at first, as in v2.x.</summary>
    public string Theme { get; init; } = DarkTheme;

    public bool SidebarVisible { get; init; } = true;

    public bool LegendVisible { get; init; } = true;

    /// <summary>Recordings and CSVs, the newest first, at most <see cref="RecentFiles.Limit"/>.</summary>
    public IReadOnlyList<string> RecentFiles { get; init; } = Array.Empty<string>();

    /// <summary>Where the last recording, CSV or template came from: where the file dialogs start.</summary>
    public string? LastFolder { get; init; }

    /// <summary>Where Extract Calibration's last calibration recording came from.</summary>
    public string? LastCalibrationFolder { get; init; }

    /// <summary>Anything but "light" is the dark theme, as v2.x read its setting.</summary>
    [JsonIgnore]
    public bool IsDark => Theme != LightTheme;

    // Records compare lists by reference; two preferences with the same files are the same preferences.
    public bool Equals(Preferences? other) =>
        other is not null && Theme == other.Theme && SidebarVisible == other.SidebarVisible && LegendVisible == other.LegendVisible &&
        RecentFiles.SequenceEqual(other.RecentFiles) && LastFolder == other.LastFolder && LastCalibrationFolder == other.LastCalibrationFolder;

    public override int GetHashCode() => HashCode.Combine(Theme, SidebarVisible, LegendVisible, RecentFiles.Count, LastFolder, LastCalibrationFolder);
}

/// <summary>The recent-files list's rules, v2.x's: how a file is added, and the pruning at start-up.</summary>
public static class RecentFiles
{
    public const int Limit = 5;

    /// <summary><paramref name="path"/> first, any earlier entry for it removed, at most <see cref="Limit"/>.</summary>
    public static IReadOnlyList<string> Add(IReadOnlyList<string> files, string path) =>
        new[] { path }.Concat(files.Where(f => f != path)).Take(Limit).ToList();

    /// <summary>The files that still exist, in order, at most <see cref="Limit"/>: what the list holds at start-up.</summary>
    public static IReadOnlyList<string> Prune(IEnumerable<string> files, Func<string, bool> exists) =>
        files.Where(f => f.Length > 0 && exists(f)).Take(Limit).ToList();

    /// <summary>A recording or an exported CSV, by its suffix in any case: what Recent Files and drag and drop open.</summary>
    public static bool IsSupported(string path) =>
        path.EndsWith(".ch10", StringComparison.OrdinalIgnoreCase) || path.EndsWith(".csv", StringComparison.OrdinalIgnoreCase);

    /// <summary>An exported CSV, which is imported; anything else is opened as a recording.</summary>
    public static bool IsCsv(string path) => path.EndsWith(".csv", StringComparison.OrdinalIgnoreCase);
}
