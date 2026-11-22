using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using TmDq.App.Models.Export;
using TmDq.App.Services;
using TmDq.App.Views.Dialogs;

namespace TmDq.App.Platform;

/// <summary>The kind-of-CSV question as a choice window, modal to the main window.</summary>
public sealed class WindowCsvKindDialog : ICsvKindDialog
{
    private readonly Window _owner;

    public WindowCsvKindDialog(Window owner) => _owner = owner;

    public async Task<CsvKind?> ChooseAsync(string fileName)
    {
        var window = new ChoiceWindow("Import CSV", $"What kind of file is '{fileName}'?", CsvKinds.All.Select(CsvKinds.Title).ToList());
        int? chosen = await window.ShowDialog<int?>(_owner);
        return chosen is { } index ? CsvKinds.All[index] : null;
    }
}