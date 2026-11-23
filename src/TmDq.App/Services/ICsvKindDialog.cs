using System.Threading.Tasks;
using TmDq.App.Models.Export;

namespace TmDq.App.Services;

/// <summary>Asks what kind of CSV a file is (US10.0), every time one is imported: from the menu, by a drop, from Recent Files. An
/// interface so the view model can be tested without a window. The window is <c>WindowCsvKindDialog</c>.</summary>
public interface ICsvKindDialog
{
    /// <summary>The kind the user says <paramref name="fileName"/> is; null if the user cancelled.</summary>
    Task<CsvKind?> ChooseAsync(string fileName);
}