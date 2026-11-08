using System.Collections.Generic;
using System.Threading.Tasks;
using TmDq.App.ViewModels;

namespace TmDq.App.Services
{
    /// <summary>Asks the user for the files the template commands need. An interface so the view models can be tested
    /// without a file dialog.</summary>
    public interface ITemplateFilePicker
    {
        /// <summary>The template to apply, or null if the user cancelled.</summary>
        Task<string?> OpenTemplateAsync(string? folder);

        /// <summary>Where to save a template, or null if the user cancelled.</summary>
        Task<string?> SaveTemplateAsync(string? folder);

        /// <summary>The recordings to run a template over (several at once), or an empty list if the user cancelled.</summary>
        Task<IReadOnlyList<string>> OpenRecordingsAsync(string? folder);

        /// <summary>The folder chosen for a batch's per-file export, or null if the user cancelled.</summary>
        Task<string?> PickFolderAsync(string? folder);
    }

    /// <summary>Shows the template commands' dialogs modally. An interface so the view model can be tested without windows.</summary>
    public interface ITemplateDialogs
    {
        /// <summary>Shows the batch confirmation; returns when it closes (<see cref="BatchApplyViewModel.Accepted"/> says how).</summary>
        Task ConfirmBatchAsync(BatchApplyViewModel vm);

        /// <summary>Which of <paramref name="choices"/> to capture a template from; null if the user cancelled.</summary>
        Task<int?> ChooseSourceAsync(IReadOnlyList<string> choices);
    }
}
