using Avalonia.Data.Converters;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using TmDq.App.ViewModels;

namespace TmDq.App.Views
{
    /// <summary>
    /// How the dialogs paint what a view model reports: the view model says what the state is (a colour, a match, a
    /// calibration's status), and these converters say how it looks, so no view model makes a brush.
    /// </summary>
    public static class StatusBrushes
    {
        private static readonly IBrush Red = new ImmutableSolidColorBrush(Color.FromRgb(0xCC, 0x00, 0x00));
        private static readonly IBrush Goldenrod = new ImmutableSolidColorBrush(Color.FromRgb(0xDA, 0xA5, 0x20));

        /// <summary>A colour as a brush: Customize View's swatches and the names written in their series' colours.</summary>
        public static readonly IValueConverter FromColor = new FuncValueConverter<Color, IBrush>(c => new ImmutableSolidColorBrush(c));

        /// <summary>The batch confirmation's glyph (<see cref="OfMatch"/>).</summary>
        public static readonly IValueConverter Match = new FuncValueConverter<bool, IBrush>(OfMatch);

        /// <summary>Extract Calibration's two status lines (<see cref="Of"/>).</summary>
        public static readonly IValueConverter Calibration = new FuncValueConverter<CalibrationStatusKind, IBrush>(Of);

        /// <summary>Green for a file that matches the template, red for one that does not.</summary>
        public static IBrush OfMatch(bool ok) => ok ? Brushes.Green : Red;

        /// <summary>The status colours, v2.x's: green, the log's goldenrod, red, and gray while pending.</summary>
        public static IBrush Of(CalibrationStatusKind kind) => kind switch
        {
            CalibrationStatusKind.Ok => Brushes.Green,
            CalibrationStatusKind.Warn => Goldenrod,
            CalibrationStatusKind.Fail => Red,
            _ => Brushes.Gray,
        };
    }
}
