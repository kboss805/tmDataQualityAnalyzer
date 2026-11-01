using System.Threading.Tasks;
using Avalonia.Media;

namespace TmDq.App.Plot
{
    /// <summary>
    /// The questions the plot's menu and Customize View ask (plan step 4.4): an interface so the commands behind them can be
    /// tested without a window. Each returns null when the user cancels. The windows are <c>WindowPlotPrompts</c>.
    /// </summary>
    public interface IPlotPrompts
    {
        /// <summary>A line of text, as <c>QInputDialog::getText</c> asks it.</summary>
        Task<string?> AskTextAsync(string title, string label, string initial);

        /// <summary>A number between <paramref name="min"/> and <paramref name="max"/> to <paramref name="decimals"/>
        /// places, as <c>QInputDialog::getDouble</c> asks it.</summary>
        Task<double?> AskNumberAsync(string title, string label, double value, double min, double max, int decimals);

        /// <summary>The Set Time Window dialog: a start and a stop, as DDD:HH:MM:SS text.</summary>
        Task<(string Start, string Stop)?> AskTimeWindowAsync(string start, string stop);

        /// <summary>A colour, as <c>QColorDialog::getColor</c> asks it.</summary>
        Task<Color?> PickColorAsync(Color initial);

        /// <summary>The Customize View dialog over <paramref name="customize"/>: true for OK, false for Cancel.</summary>
        Task<bool> CustomizeAsync(CustomizeViewModel customize);
    }
}
