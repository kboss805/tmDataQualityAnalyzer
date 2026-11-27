using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.Input;
using TmDq.App.Models.Templates;
using TmDq.App.Services;

namespace TmDq.App.ViewModels.Dialogs;

/// <summary>One file in the batch confirmation: ✓ and its name, or ✗, its name and why it does not match.</summary>
public sealed record BatchFileRow(FileCheck Check)
{
    public string Glyph => Check.Ok ? Glyphs.Ok : Glyphs.Fail;

    /// <summary>The file's name, then "  —  reason" for one that does not match.</summary>
    public string Text => Path.GetFileName(Check.FilePath) + (Check.Ok ? "" : "  —  " + Check.Reason);

    /// <summary>The whole path, as the row's tooltip.</summary>
    public string ToolTip => Check.FilePath;
}

/// <summary>
/// The batch confirmation (plan step 4.7, US1.1): how many of the chosen files match the
/// template, a row for each, and the options: a CSV and plot images per file into a folder, and the template's series
/// names and colours reused (on at first). Process needs a matching file, and a folder while exporting.
/// </summary>
public sealed class BatchApplyViewModel : DialogViewModel
{
    public const string Title = "Process Multiple Files";

    /// <summary>What a batch does to the plot, and what its Cancel does: said before it starts, since it is the one action that does
    /// not stack.</summary>
    public const string ClearsThePlot = "The plot is cleared first: what it shows now, recordings and imports alike, is removed and " +
                                        "these files take its place. Cancel during processing stops the batch; the files already " +
                                        "processed stay.";

    private readonly ITemplateFilePicker? _files;

    private bool _exportPerFile;
    private string _outputFolder;
    private bool _reuseAppearance = true;

    public BatchApplyViewModel(IReadOnlyList<FileCheck> checks, string outputFolder, ITemplateFilePicker? files = null)
    {
        Rows = checks.Select(c => new BatchFileRow(c)).ToList();
        _outputFolder = outputFolder;
        _files = files;
        BrowseCommand = new AsyncRelayCommand(BrowseAsync, () => _files is not null && ExportPerFile);
        ProcessCommand = new RelayCommand(Process, () => CanProcess);
    }

    public IReadOnlyList<BatchFileRow> Rows { get; }

    public bool ExportPerFile
    {
        get => _exportPerFile;
        set
        {
            if (!SetProperty(ref _exportPerFile, value)) return;
            OnPropertyChanged(nameof(CanProcess));
            BrowseCommand.NotifyCanExecuteChanged();
        }
    }

    public string OutputFolder
    {
        get => _outputFolder;
        set
        {
            if (!SetProperty(ref _outputFolder, value)) return;
            OnPropertyChanged(nameof(CanProcess));
        }
    }

    public bool ReuseAppearance { get => _reuseAppearance; set => SetProperty(ref _reuseAppearance, value); }

    public int MatchCount => Rows.Count(r => r.Check.Ok);

    public string Summary => string.Create(CultureInfo.InvariantCulture,
        $"{MatchCount} of {Rows.Count} selected file(s) match the template and will be processed.");

    /// <summary>The files that will be processed, in the order chosen.</summary>
    public IReadOnlyList<string> Matched => Rows.Where(r => r.Check.Ok).Select(r => r.Check.FilePath).ToList();

    public bool CanProcess => MatchCount > 0 && (!ExportPerFile || OutputFolder.Trim().Length > 0);

    public IAsyncRelayCommand BrowseCommand { get; }

    public IRelayCommand ProcessCommand { get; }

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
        Accept();
    }
}
