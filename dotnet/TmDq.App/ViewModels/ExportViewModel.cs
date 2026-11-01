using System;
using System.IO;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Services;

namespace TmDq.App.ViewModels
{
    /// <summary>What an export writes: the plot as an image, its data as CSV, or the log as text.</summary>
    public enum ExportKind
    {
        Image,
        Data,
        Log,
    }

    /// <summary>
    /// The Export dialog's state (plan step 4.5), the Qt <c>ExportDialog</c>: three rows, the plot image (ticked by
    /// default), the data and the log, each a box and a path with Browse. A row's path and Browse are enabled only while
    /// its box is ticked, and Export only while some box is ticked and every ticked row has a path. There is no format
    /// choice: an image's suffix decides it.
    /// </summary>
    public sealed partial class ExportViewModel : ObservableObject
    {
        public const string Title = "Export Data";

        private readonly IDataFilePicker? _files;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(CanExport))]
        private bool _exportImage = true;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(CanExport))]
        private bool _exportData;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(CanExport))]
        private bool _exportLog;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(CanExport))]
        private string _imagePath;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(CanExport))]
        private string _dataPath;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(CanExport))]
        private string _logPath;

        /// <param name="files">The save dialogs Browse opens; null leaves Browse unavailable.</param>
        public ExportViewModel(string imagePath, string dataPath, string logPath, IDataFilePicker? files = null)
        {
            _imagePath = imagePath;
            _dataPath = dataPath;
            _logPath = logPath;
            _files = files;
            BrowseImageCommand = new AsyncRelayCommand(() => BrowseAsync(ExportKind.Image), () => _files is not null && ExportImage);
            BrowseDataCommand = new AsyncRelayCommand(() => BrowseAsync(ExportKind.Data), () => _files is not null && ExportData);
            BrowseLogCommand = new AsyncRelayCommand(() => BrowseAsync(ExportKind.Log), () => _files is not null && ExportLog);
            ExportCommand = new RelayCommand(Accept, () => CanExport);
            CancelCommand = new RelayCommand(() => CloseRequested?.Invoke(this, EventArgs.Empty));
        }

        /// <summary>
        /// The three default paths, as the Qt <c>onExportPlot</c> makes them: the plot's title (or "plot") with the
        /// characters a file name cannot hold replaced by "_", as "&lt;title&gt;.png", "&lt;title&gt;.csv" and
        /// "&lt;title&gt;_log.txt" in <paramref name="folder"/>.
        /// </summary>
        public static (string Image, string Data, string Log) DefaultPaths(string folder, string plotTitle)
        {
            string name = string.IsNullOrEmpty(plotTitle) ? "plot" : plotTitle;
            foreach (char c in "\\/:*?\"<>|") name = name.Replace(c, '_');
            return (Path.Combine(folder, name + ".png"), Path.Combine(folder, name + ".csv"), Path.Combine(folder, name + "_log.txt"));
        }

        /// <summary>Raised when Export or Cancel closes the dialog; <see cref="Accepted"/> says which.</summary>
        public event EventHandler? CloseRequested;

        public bool Accepted { get; private set; }

        /// <summary>Some box ticked, and every ticked row with a path.</summary>
        public bool CanExport =>
            (ExportImage || ExportData || ExportLog) &&
            (!ExportImage || ImagePath.Length > 0) && (!ExportData || DataPath.Length > 0) && (!ExportLog || LogPath.Length > 0);

        public IAsyncRelayCommand BrowseImageCommand { get; }

        public IAsyncRelayCommand BrowseDataCommand { get; }

        public IAsyncRelayCommand BrowseLogCommand { get; }

        public IRelayCommand ExportCommand { get; }

        public IRelayCommand CancelCommand { get; }

        partial void OnExportImageChanged(bool value) => BrowseImageCommand.NotifyCanExecuteChanged();

        partial void OnExportDataChanged(bool value) => BrowseDataCommand.NotifyCanExecuteChanged();

        partial void OnExportLogChanged(bool value) => BrowseLogCommand.NotifyCanExecuteChanged();

        protected override void OnPropertyChanged(System.ComponentModel.PropertyChangedEventArgs e)
        {
            base.OnPropertyChanged(e);
            if (e.PropertyName == nameof(CanExport)) ExportCommand.NotifyCanExecuteChanged();
        }

        /// <summary>A save dialog starting at the row's path; a name chosen without a suffix gets the row's own.</summary>
        private async Task BrowseAsync(ExportKind kind)
        {
            string current = kind switch { ExportKind.Image => ImagePath, ExportKind.Data => DataPath, _ => LogPath };
            string? chosen = await _files!.SaveExportAsync(kind, current);
            if (string.IsNullOrEmpty(chosen)) return;
            if (Path.GetExtension(chosen).Length == 0)
                chosen += kind switch { ExportKind.Image => ".png", ExportKind.Data => ".csv", _ => ".txt" };
            switch (kind)
            {
                case ExportKind.Image: ImagePath = chosen; break;
                case ExportKind.Data: DataPath = chosen; break;
                default: LogPath = chosen; break;
            }
        }

        private void Accept()
        {
            Accepted = true;
            CloseRequested?.Invoke(this, EventArgs.Empty);
        }
    }
}
