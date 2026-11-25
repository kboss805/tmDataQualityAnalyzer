using System.Collections.Generic;
using System.Threading.Tasks;
using Avalonia.Media;
using TmDq.App.Services;

namespace TmDq.App.ViewModels.Plot;

/// <summary>The plot's questions with no one to ask: every one is answered "cancel", so nothing changes. What a
/// <c>PlotView</c> asks through until the application hands it the windows that ask the user.</summary>
public sealed class NoPlotPrompts : IPlotPrompts
{
    public Task<string?> AskTextAsync(string title, string label, string initial) => Task.FromResult<string?>(null);

    public Task<double?> AskNumberAsync(string title, string label, double value, double min, double max, int decimals) =>
        Task.FromResult<double?>(null);

    public Task<(string Start, string Stop)?> AskTimeWindowAsync(string start, string stop) =>
        Task.FromResult<(string Start, string Stop)?>(null);

    public Task<int?> ChooseAsync(string title, string label, IReadOnlyList<string> choices) => Task.FromResult<int?>(null);

    public Task<Color?> PickColorAsync(Color initial) => Task.FromResult<Color?>(null);

    public Task<bool> CustomizeAsync(CustomizeViewModel customize) => Task.FromResult(false);
}
