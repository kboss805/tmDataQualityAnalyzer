using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Media;
using TmDq.App.Plot;
using TmDq.App.Views;

namespace TmDq.App.Services
{
    /// <summary>The plot's questions as windows, modal to the window that holds <paramref name="host"/> (plan step 4.4). A
    /// question with no window to sit over is answered "cancel".</summary>
    public sealed class WindowPlotPrompts : IPlotPrompts
    {
        private readonly Control _host;

        public WindowPlotPrompts(Control host) => _host = host;

        private Window? Owner => TopLevel.GetTopLevel(_host) as Window;

        public Task<string?> AskTextAsync(string title, string label, string initial) =>
            Ask<string?>(new TextPromptWindow(title, label, initial));

        public Task<double?> AskNumberAsync(string title, string label, double value, double min, double max, int decimals) =>
            Ask<double?>(new NumberPromptWindow(title, label, value, min, max, decimals));

        public Task<(string Start, string Stop)?> AskTimeWindowAsync(string start, string stop) =>
            Ask<(string Start, string Stop)?>(new TimeWindowPromptWindow(start, stop));

        public Task<Color?> PickColorAsync(Color initial) => Ask<Color?>(new ColorPromptWindow(initial));

        public async Task<bool> CustomizeAsync(CustomizeViewModel customize)
        {
            Window? owner = Owner;
            if (owner is null) return false;
            // The dialog's own questions (a colour, a name) sit over the dialog, not over the main window.
            return await new CustomizeViewWindow { DataContext = customize }.ShowDialog<bool>(owner);
        }

        private async Task<T?> Ask<T>(Window window)
        {
            Window? owner = Owner;
            if (owner is null) return default;
            return await window.ShowDialog<T?>(owner);
        }
    }
}
