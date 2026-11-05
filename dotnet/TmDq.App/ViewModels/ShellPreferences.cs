using System;
using System.Collections.Generic;
using System.IO;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Settings;

namespace TmDq.App.ViewModels
{
    /// <summary>
    /// What the shell remembers between sessions (plan step 4.8), each change written at once: Recent Files, the theme
    /// (US9.0), the log sidebar, the last folder and the legend's visibility. The main window reaches it through its own
    /// members, which pass its notices on.
    /// </summary>
    internal sealed class ShellPreferences : ObservableObject
    {
        private readonly PreferencesStore _store;

        public ShellPreferences(PreferencesStore store)
        {
            _store = store;
            ClearRecentFilesCommand = new RelayCommand(() => SetRecentFiles(Array.Empty<string>()));
            ToggleThemeCommand = new RelayCommand(() => IsDarkTheme = !IsDarkTheme);
            ToggleSidebarCommand = new RelayCommand(() => SidebarVisible = !SidebarVisible);
        }

        public Preferences Current => _store.Current;

        /// <summary>The recordings and CSVs opened last, the newest first (the menu's Recent Files).</summary>
        public IReadOnlyList<string> RecentFiles => _store.Current.RecentFiles;

        public IRelayCommand ClearRecentFilesCommand { get; }

        public void SetRecentFiles(IReadOnlyList<string> files)
        {
            _store.Update(p => p with { RecentFiles = files });
            OnPropertyChanged(nameof(RecentFiles));
        }

        /// <summary><paramref name="path"/>, in full, at the top of Recent Files.</summary>
        public void AddRecentFile(string path) => SetRecentFiles(Settings.RecentFiles.Add(RecentFiles, Path.GetFullPath(path)));

        /// <summary>The dark theme (v2.x's default too) or the light one.</summary>
        public bool IsDarkTheme
        {
            get => _store.Current.IsDark;
            set
            {
                if (value == IsDarkTheme) return;
                _store.Update(p => p with { Theme = value ? Preferences.DarkTheme : Preferences.LightTheme });
                OnPropertyChanged();
                OnPropertyChanged(nameof(ThemeMenuText));
            }
        }

        /// <summary>The menu item that switches the theme, named for where it goes.</summary>
        public string ThemeMenuText => IsDarkTheme ? "Switch to Light Theme" : "Switch to Dark Theme";

        public IRelayCommand ToggleThemeCommand { get; }

        /// <summary>The log sidebar shown (the title bar's toggle, Ctrl+B).</summary>
        public bool SidebarVisible
        {
            get => _store.Current.SidebarVisible;
            set
            {
                if (value == SidebarVisible) return;
                _store.Update(p => p with { SidebarVisible = value });
                OnPropertyChanged();
            }
        }

        public IRelayCommand ToggleSidebarCommand { get; }

        /// <summary>The folder of the last recording opened, CSV imported or template used. Nothing is told of a change:
        /// it is read when a dialog opens.</summary>
        public string? LastFolder
        {
            get => _store.Current.LastFolder;
            set => _store.Update(p => p with { LastFolder = value });
        }

        /// <summary>Whether the legend is wanted (US4.0, US4.1).</summary>
        public bool LegendVisible
        {
            get => _store.Current.LegendVisible;
            set => _store.Update(p => p with { LegendVisible = value });
        }
    }
}
