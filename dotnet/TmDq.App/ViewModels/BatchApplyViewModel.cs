using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Services;
using TmDq.App.Templates;

namespace TmDq.App.ViewModels
{
    /// <summary>One file in the batch confirmation: ✓ and its name, or ✗, its name and why it does not match.</summary>
    public sealed record BatchFileRow(FileCheck Check)
    {
        public string Glyph => Check.Ok ? "✓" : "✗";

        public IBrush GlyphBrush => Check.Ok ? Brushes.Green : new SolidColorBrush(Color.FromRgb(0xCC, 0x00, 0x00));

        /// <summary>The file's name, then "  —  reason" for one that does not match.</summary>
        public string Text => Path.GetFileName(Check.FilePath) + (Check.Ok ? "" : "  —  " + Check.Reason);

        /// <summary>The whole path, as the row's tooltip.</summary>
        public string ToolTip => Check.FilePath;
    }

    /// <summary>
    /// The batch confirmation (plan step 4.7, US1.1), the Qt <c>BatchApplyDialog</c>: how many of the chosen files match the
    /// template, a row for each, and the options: a CSV and plot images per file into a folder, and the template's series
    /// names and colours reused (on at first). Process needs a matching file, and a folder while exporting.
    /// </summary>
    public sealed partial class BatchApplyViewModel : ObservableObject
    {
        public const string Title = "Batch Process Files";

        private readonly ITemplateFilePicker? _files;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(CanProcess))]
        private bool _exportPerFile;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(CanProcess))]
        private string _outputFolder;

        [ObservableProperty]
        private bool _reuseAppearance = true;

        public BatchApplyViewModel(IReadOnlyList<FileCheck> checks, string outputFolder, ITemplateFilePicker? files = null)
        {
            Rows = checks.Select(c => new BatchFileRow(c)).ToList();
            _outputFolder = outputFolder;
            _files = files;
            BrowseCommand = new AsyncRelayCommand(BrowseAsync, () => _files is not null && ExportPerFile);
            ProcessCommand = new RelayCommand(Process, () => CanProcess);
            CancelCommand = new RelayCommand(() => CloseRequested?.Invoke(this, EventArgs.Empty));
        }

        /// <summary>Raised when Process or Cancel closes the dialog; <see cref="Accepted"/> says which.</summary>
        public event EventHandler? CloseRequested;

        public IReadOnlyList<BatchFileRow> Rows { get; }

        public int MatchCount => Rows.Count(r => r.Check.Ok);

        public string Summary => string.Create(CultureInfo.InvariantCulture,
            $"{MatchCount} of {Rows.Count} selected file(s) match the template and will be processed.");

        /// <summary>The files that will be processed, in the order chosen.</summary>
        public IReadOnlyList<string> Matched => Rows.Where(r => r.Check.Ok).Select(r => r.Check.FilePath).ToList();

        public bool CanProcess => MatchCount > 0 && (!ExportPerFile || OutputFolder.Trim().Length > 0);

        public bool Accepted { get; private set; }

        public IAsyncRelayCommand BrowseCommand { get; }

        public IRelayCommand ProcessCommand { get; }

        public IRelayCommand CancelCommand { get; }

        partial void OnExportPerFileChanged(bool value) => BrowseCommand.NotifyCanExecuteChanged();

        protected override void OnPropertyChanged(System.ComponentModel.PropertyChangedEventArgs e)
        {
            base.OnPropertyChanged(e);
            if (e.PropertyName == nameof(CanProcess)) ProcessCommand.NotifyCanExecuteChanged();
        }

        private async Task BrowseAsync()
        {
            string? folder = await _files!.PickFolderAsync(OutputFolder);
            if (!string.IsNullOrEmpty(folder)) OutputFolder = folder;
        }

        private void Process()
        {
            if (!CanProcess) return;
            Accepted = true;
            CloseRequested?.Invoke(this, EventArgs.Empty);
        }
    }
}
