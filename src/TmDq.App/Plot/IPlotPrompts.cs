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
        /// <summary>A line of text under <paramref name="label"/>, starting as <paramref name="initial"/>.</summary>
        Task<string?> AskTextAsync(string title, string label, string initial);

        /// <summary>A number between <paramref name="min"/> and <paramref name="max"/> to <paramref name="decimals"/>
        /// places, held there as <see cref="PlotCommands.SpinValue"/> holds it.</summary>
        Task<double?> AskNumberAsync(string title, string label, double value, double min, double max, int decimals);

        /// <summary>The Set Time Window dialog: a start and a stop, as DDD:HH:MM:SS text.</summary>
        Task<(string Start, string Stop)?> AskTimeWindowAsync(string start, string stop);

        /// <summary>A colour, starting as <paramref name="initial"/>.</summary>
        Task<Color?> PickColorAsync(Color initial);

        /// <summary>The Customize View dialog over <paramref name="customize"/>: true for OK, false for Cancel.</summary>
        Task<bool> CustomizeAsync(CustomizeViewModel customize);
    }
}
