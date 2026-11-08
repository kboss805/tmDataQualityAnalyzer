using System.Collections.Generic;
using System.Threading.Tasks;
using Avalonia.Controls;
using TmDq.App.Services;
using TmDq.App.ViewModels;
using TmDq.App.Views;

namespace TmDq.App.Platform
{
    /// <summary>The template commands' dialogs as windows, modal to the main window.</summary>
    public sealed class WindowTemplateDialogs : ITemplateDialogs
    {
        private readonly Window _owner;

        public WindowTemplateDialogs(Window owner) => _owner = owner;

        public async Task ConfirmBatchAsync(BatchApplyViewModel vm)
        {
            var window = new BatchApplyWindow { DataContext = vm };
            vm.CloseRequested += (_, _) => window.Close();
            await window.ShowDialog(_owner);
        }

        public Task<int?> ChooseSourceAsync(IReadOnlyList<string> choices) =>
            new ChoiceWindow("Save as Template", "Capture the per-stream settings from which source?", choices).ShowDialog<int?>(_owner);
    }
}
