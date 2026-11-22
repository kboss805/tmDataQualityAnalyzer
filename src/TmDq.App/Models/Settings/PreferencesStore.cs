using System;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace TmDq.App.Models.Settings;

/// <summary>
/// The preferences file: JSON, in <c>%APPDATA%\tmDataQualityAnalyzer</c>, or, for a portable copy (a file named
/// "portable" beside the exe), in a <c>tmDataQualityAnalyzer</c> folder beside the exe, where v2.x kept its settings
/// too. Every change is written at once.
/// </summary>
public sealed class PreferencesStore
{
    public const string FileName = "preferences.json";
    public const string PortableMarker = "portable";
    private const string FolderName = "tmDataQualityAnalyzer";

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
    /// The preferences in <paramref name="folder"/>: this application's file when there is one, else the defaults,
    /// written there. A file that cannot be read is the defaults. Recent files that no longer exist are dropped.
    /// </summary>
    public static PreferencesStore Load(string folder, Func<string, bool>? exists = null)
    {
        exists ??= File.Exists;
        string file = Path.Combine(folder, FileName);
        Preferences loaded = File.Exists(file) ? Read(file) ?? new Preferences() : new Preferences();
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
