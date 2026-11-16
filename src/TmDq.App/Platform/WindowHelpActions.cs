using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using TmDq.App.Models.Help;
using TmDq.App.Services;
using TmDq.App.Views;

namespace TmDq.App.Platform
{
    /// <summary>The manual opened in the browser, About in a box over its owner.</summary>
    public sealed class WindowHelpActions : IHelpActions
    {
        private readonly Window _owner;

        public WindowHelpActions(Window owner) => _owner = owner;

        public static string AboutText =>
            "TM Data Quality Analyzer\n\nVersion " + AppInfo.Version + "\n\n" +
            "Analyzes framesync lock statistics and receiver SNR data from IRIG 106 Chapter 10 PCM recordings and plots the " +
            "results over time.";

        public async Task OpenManualAsync()
        {
            string? path = HelpManual.PathToOpen(AppContext.BaseDirectory, Path.GetTempPath());
            if (path is null || !await _owner.Launcher.LaunchFileInfoAsync(new FileInfo(path)))
                await new ReportWindow("User Manual", MainWindow.ManualNotWritten).ShowDialog(_owner);
        }

        public Task ShowAboutAsync() => new ReportWindow(MainWindow.AboutTitle, AboutText).ShowDialog(_owner);
    }
}
