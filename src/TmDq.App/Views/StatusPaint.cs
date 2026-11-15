using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Data.Converters;
using Avalonia.Media;
using Avalonia.Styling;
using TmDq.App.ViewModels;

namespace TmDq.App.Views
{
    /// <summary>What a status means, as Fluent names it: success, caution, critical, or neither (neutral).</summary>
    public enum StatusLevel
    {
        Success,
        Caution,
        Critical,
        Neutral,
    }

    /// <summary>
    /// The one place a status becomes a colour. A view model says what the state is (a match, a calibration's status, a log
    /// line's level); this says which of Fluent's status tokens (<c>SystemFillColorSuccess</c>, ...) paints it, so every
    /// status in the application, the log's and the dialogs', is the same colour, in each theme.
    /// </summary>
    public static class StatusPaint
    {
        /// <summary>Paints a text with its status colour, following the theme: <c>v:Status.Level="{Binding ...}"</c>.</summary>
        public static readonly AttachedProperty<StatusLevel?> LevelProperty =
            AvaloniaProperty.RegisterAttached<TextBlock, StatusLevel?>("Level", typeof(StatusPaint));

        /// <summary>The batch confirmation's glyph (<see cref="OfMatch"/>).</summary>
        public static readonly IValueConverter Match = new FuncValueConverter<bool, StatusLevel?>(ok => OfMatch(ok));

        /// <summary>Extract Calibration's two status lines (<see cref="Of"/>).</summary>
        public static readonly IValueConverter Calibration = new FuncValueConverter<CalibrationStatusKind, StatusLevel?>(k => Of(k));

        static StatusPaint() => LevelProperty.Changed.AddClassHandler<TextBlock>((text, e) =>
        {
            if (e.NewValue is StatusLevel level) text.Bind(TextBlock.ForegroundProperty, text.GetResourceObservable(BrushKey(level)));
            else text.ClearValue(TextBlock.ForegroundProperty);
        });

        public static StatusLevel? GetLevel(TextBlock text) => text.GetValue(LevelProperty);

        public static void SetLevel(TextBlock text, StatusLevel? level) => text.SetValue(LevelProperty, level);

        /// <summary>Success for a file that matches the template, critical for one that does not.</summary>
        public static StatusLevel OfMatch(bool ok) => ok ? StatusLevel.Success : StatusLevel.Critical;

        /// <summary>Success, caution (a warning) or critical, and neutral while pending.</summary>
        public static StatusLevel Of(CalibrationStatusKind kind) => kind switch
        {
            CalibrationStatusKind.Ok => StatusLevel.Success,
            CalibrationStatusKind.Warn => StatusLevel.Caution,
            CalibrationStatusKind.Fail => StatusLevel.Critical,
            _ => StatusLevel.Neutral,
        };

        /// <summary>The name of the level's token (and of its brush with "Brush" after it).</summary>
        public static string TokenOf(StatusLevel level) => level switch
        {
            StatusLevel.Success => "SystemFillColorSuccess",
            StatusLevel.Caution => "SystemFillColorCaution",
            StatusLevel.Critical => "SystemFillColorCritical",
            _ => "SystemFillColorNeutral",
        };

        public static string BrushKey(StatusLevel level) => TokenOf(level) + "Brush";

        /// <summary>The level's brush in a theme: for what is drawn from code at one moment (the log's lines).</summary>
        public static IBrush? BrushOf(StatusLevel level, ThemeVariant? theme) =>
            Application.Current is { } app && app.TryFindResource(BrushKey(level), theme ?? ThemeVariant.Default, out object? found) ? found as IBrush : null;
    }
}
