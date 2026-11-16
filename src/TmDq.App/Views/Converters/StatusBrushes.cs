using Avalonia.Data.Converters;
using Avalonia.Media;
using Avalonia.Media.Immutable;

namespace TmDq.App.Views.Converters;

/// <summary>A series' colour as a brush (what the user chose is data, not a theme's): Customize View's swatches and the
/// names written in their series' colours. What a status looks like is <see cref="StatusPaint"/>'s.</summary>
public static class StatusBrushes
{
    public static readonly IValueConverter FromColor = new FuncValueConverter<Color, IBrush>(c => new ImmutableSolidColorBrush(c));
}
