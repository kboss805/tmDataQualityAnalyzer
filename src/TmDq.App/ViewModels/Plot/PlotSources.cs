using System.Collections.Generic;
using System.Globalization;
using System.Linq;

namespace TmDq.App.ViewModels.Plot;

/// <summary>
/// The recordings a plot holds (a processed recording, a batch file, an imported CSV), as Plot File and Remove File know them: each
/// one's name, the one Plot File shows alone, and what follows from the plot's series about them (the list, the id a new one takes,
/// whether a series is of the one shown, and the title once some are removed). It reads the plot's own series list and never
/// changes it; the plot decides what is removed, redrawn and told.
/// </summary>
public sealed class PlotSources
{
    private readonly IReadOnlyList<PlotSeries> _series;
    private readonly Dictionary<int, string> _labels = new();

    /// <param name="series">The plot's series, read as they are each time (never copied).</param>
    public PlotSources(IReadOnlyList<PlotSeries> series) => _series = series;

    /// <summary>The recording Plot File shows alone, or -1 for every one overlaid.</summary>
    public int Shown { get; private set; } = -1;

    /// <summary>Whether <paramref name="series"/> is of what Plot File shows: the recording shown, or any when all are.</summary>
    public bool IsShown(PlotSeries series) => Shown < 0 || series.SourceId == Shown;

    /// <summary>Shows <paramref name="sourceId"/> alone, or every recording for -1.</summary>
    public void Show(int sourceId) => Shown = sourceId;

    /// <summary>Each recording the series come from, once, in the order first met, with its <see cref="Label"/>.</summary>
    public IReadOnlyList<(int Id, string Label)> List()
    {
        var list = new List<(int, string)>();
        var seen = new HashSet<int>();
        foreach (PlotSeries s in _series)
        {
            if (seen.Add(s.SourceId)) list.Add((s.SourceId, Label(s.SourceId)));
        }
        return list;
    }

    /// <summary>The id a recording added now would take: one past the greatest the plot holds, or 0 for an empty plot, so that it
    /// never shares an id with data already shown (an imported CSV).</summary>
    public int NextId => _series.Count == 0 ? 0 : _series.Max(s => s.SourceId) + 1;

    /// <summary>A recording's name, also before it has any series: the one it was given (<see cref="Name"/>), else "Source N".</summary>
    public string Label(int sourceId) =>
        _labels.TryGetValue(sourceId, out string? label) ? label : string.Create(CultureInfo.InvariantCulture, $"Source {sourceId}");

    /// <summary>Names a recording for Plot File and Remove File.</summary>
    public void Name(int sourceId, string label) => _labels[sourceId] = label;

    /// <summary>The names given to <paramref name="sourceIds"/>; one never named has none here.</summary>
    public HashSet<string> NamesOf(IEnumerable<int> sourceIds) =>
        sourceIds.Where(_labels.ContainsKey).Select(id => _labels[id]).ToHashSet();

    /// <summary>
    /// Forgets <paramref name="removed"/>, whose series the plot has just taken out: their names, and the one shown if it was one
    /// of them (every recording is then overlaid).
    /// </summary>
    /// <returns>Whether what is drawn changed: every recording was overlaid, or the one shown went. With another one shown, it
    /// did not, and the view can stay where it is.</returns>
    public bool Forget(IReadOnlyCollection<int> removed)
    {
        foreach (int id in removed) _labels.Remove(id);
        bool drawnChanged = Shown < 0 || removed.Contains(Shown);
        if (removed.Contains(Shown)) Shown = -1;
        return drawnChanged;
    }

    /// <summary>The plot's title once <paramref name="removedNames"/> are gone: a title that named one of them (as a recording's
    /// does until it is renamed) names the first recording left; a title the operator wrote is theirs and stays.</summary>
    public string TitleAfterRemoving(string title, IReadOnlySet<string> removedNames) =>
        removedNames.Contains(title) ? List()[0].Label : title;

    /// <summary>Every name forgotten and every recording overlaid: the plot cleared, or a new file in place of what it held.</summary>
    public void Reset()
    {
        _labels.Clear();
        Shown = -1;
    }

    /// <summary>The names only (Shown kept): a CSV that replaces the plot's series is the only recording, and the earlier names
    /// went with them.</summary>
    public void ForgetNames() => _labels.Clear();
}
