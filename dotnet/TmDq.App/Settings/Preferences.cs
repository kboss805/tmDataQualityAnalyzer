using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace TmDq.App.Settings
{
    /// <summary>
    /// What the application remembers between sessions (plan step 4.8): the theme, the log sidebar, the plot's legend,
    /// the recent files, the folder the last recording came from and the folder the last calibration recording came
    /// from. The Qt application kept the same things in its QSettings INI file; on the first run they are imported from
    /// it (<see cref="QtSettingsImport"/>).
    /// </summary>
    public sealed record Preferences
    {
        public const string DarkTheme = "dark";
        public const string LightTheme = "light";

        /// <summary>"dark" or "light"; dark at first, as in the Qt application.</summary>
        public string Theme { get; init; } = DarkTheme;

        public bool SidebarVisible { get; init; } = true;

        public bool LegendVisible { get; init; } = true;

        /// <summary>Recordings and CSVs, the newest first, at most <see cref="RecentFiles.Limit"/>.</summary>
        public IReadOnlyList<string> RecentFiles { get; init; } = Array.Empty<string>();

        /// <summary>Where the last recording, CSV or template came from: where the file dialogs start.</summary>
        public string? LastFolder { get; init; }

        /// <summary>Where Extract Calibration's last calibration recording came from.</summary>
        public string? LastCalibrationFolder { get; init; }

        /// <summary>Anything but "light" is the dark theme, as the Qt application reads its setting.</summary>
        [JsonIgnore]
        public bool IsDark => Theme != LightTheme;

        // Records compare lists by reference; two preferences with the same files are the same preferences.
        public bool Equals(Preferences? other) =>
            other is not null && Theme == other.Theme && SidebarVisible == other.SidebarVisible && LegendVisible == other.LegendVisible &&
            RecentFiles.SequenceEqual(other.RecentFiles) && LastFolder == other.LastFolder && LastCalibrationFolder == other.LastCalibrationFolder;

        public override int GetHashCode() => HashCode.Combine(Theme, SidebarVisible, LegendVisible, RecentFiles.Count, LastFolder, LastCalibrationFolder);
    }

    /// <summary>The recent-files list's rules, the Qt <c>addRecentFile</c>'s and its start-up pruning.</summary>
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

    /// <summary>
    /// The preferences file: JSON, beside the Qt application's INI file, in <c>%APPDATA%\tmDataQualityAnalyzer</c>, or,
    /// for a portable copy (a file named "portable" beside the exe), in a <c>tmDataQualityAnalyzer</c> folder beside the
    /// exe, as the Qt application decides where its settings go. Every change is written at once.
    /// </summary>
    public sealed class PreferencesStore
    {
        public const string FileName = "preferences.json";
        public const string QtFileName = "tmDataQualityAnalyzer.ini";
        public const string PortableMarker = "portable";
        public const string FolderName = "tmDataQualityAnalyzer";

        private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };
        private readonly string? _file;

        /// <param name="file">The file each change is written to; null keeps the preferences in memory (the tests' store).</param>
        public PreferencesStore(Preferences current, string? file = null)
        {
            Current = current;
            _file = file;
        }

        public Preferences Current { get; private set; }

        /// <summary>Where the preferences live for an application in <paramref name="appDirectory"/>.</summary>
        public static string FolderFor(string appDirectory, string appData) =>
            File.Exists(Path.Combine(appDirectory, PortableMarker)) ? Path.Combine(appDirectory, FolderName) : Path.Combine(appData, FolderName);

        /// <summary>
        /// The preferences in <paramref name="folder"/>: this application's file when there is one; else, on the first
        /// run, the Qt application's settings imported from its INI file there, and written as this application's;
        /// else the defaults. A file that cannot be read is the defaults. Recent files that no longer exist are dropped.
        /// </summary>
        public static PreferencesStore Load(string folder, Func<string, bool>? exists = null)
        {
            exists ??= File.Exists;
            string file = Path.Combine(folder, FileName);
            Preferences loaded;
            if (File.Exists(file))
            {
                loaded = Read(file) ?? new Preferences();
            }
            else
            {
                string qt = Path.Combine(folder, QtFileName);
                loaded = File.Exists(qt) ? QtSettingsImport.Read(qt) : new Preferences();
            }
            var store = new PreferencesStore(loaded with { RecentFiles = RecentFiles.Prune(loaded.RecentFiles, exists) }, file);
            if (!File.Exists(file)) store.Save();
            return store;
        }

        private static Preferences? Read(string file)
        {
            try
            {
                Preferences? read = JsonSerializer.Deserialize<Preferences>(File.ReadAllText(file), Json);
                return read is null ? null : read with
                {
                    Theme = read.Theme ?? Preferences.DarkTheme,
                    RecentFiles = (read.RecentFiles ?? Array.Empty<string>()).Where(f => f is not null).ToList(),
                };
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
            {
                return null;
            }
        }

        /// <summary>Changes the preferences and writes them; a write that fails keeps the change for the session.</summary>
        public void Update(Func<Preferences, Preferences> change)
        {
            Preferences next = change(Current);
            if (next.Equals(Current)) return;
            Current = next;
            Save();
        }

        private void Save()
        {
            if (_file is null) return;
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(_file)!);
                File.WriteAllText(_file, JsonSerializer.Serialize(Current, Json));
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                // Preferences are a convenience: a read-only folder costs the next session its memory, nothing more.
            }
        }
    }
}
