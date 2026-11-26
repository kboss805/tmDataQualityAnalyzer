using System;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Styling;
using Avalonia.Threading;
using Avalonia.VisualTree;
using TmDq.App.Views;
using TmDq.App.Views.Controls;
using TmDq.App.Views.Converters;
using TmDq.App.Views.Dialogs;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// A dialog's anatomy, as Fluent's ContentDialog has it: content in 24 px of padding above one footer band, its buttons at the
/// right in equal widths, the default (Enter) in the accent and a cancel (Escape). Held for every window kind the application
/// has, in both themes, so a dialog added later that lays its own buttons out fails here.
/// </summary>
public class DialogAnatomyTests
{
    private static Color Brush(string key, ThemeVariant theme)
    {
        Assert.True(Application.Current!.TryFindResource(key, theme, out object? b), key);
        return ((ISolidColorBrush)b!).Color;
    }

    private static Color DrawnAt(ILockedFramebuffer fb, double x, double y) => Color.FromUInt32(0xFF000000 | Frames.Pixel(fb, (int)x, (int)y));

    [AvaloniaTheory]
    [InlineData(false)]
    [InlineData(true)]
    public void EveryDialogHasTheOneFooterBandWithItsButtonsAtTheRightInEqualWidths(bool dark)
    {
        var app = (App)Application.Current!;
        ThemeVariant? before = app.RequestedThemeVariant;
        app.ApplyTheme(dark);
        try
        {
            int dialogs = 0;
            foreach ((Type kind, Window window) in IconTests.EveryWindowKind())
            {
                if (kind == typeof(MainWindow)) continue;
                dialogs++;
                window.Show();
                Dispatcher.UIThread.RunJobs();
                try
                {
                    string name = kind.Name;
                    DialogFooter footer = Assert.Single(window.GetVisualDescendants().OfType<DialogFooter>());
                    Rect band = Frames.InWindow(footer, window);

                    // A band across the foot of the window, in the base colour under a divider: drawn, not only declared.
                    Assert.Equal((name, 0.0, window.ClientSize.Width), (name, band.Left, band.Width));
                    Assert.Equal((name, window.ClientSize.Height), (name, band.Bottom));
                    Color baseColour = Brush("SolidBackgroundFillColorBaseBrush", window.ActualThemeVariant);
                    Color divider = Brush("DividerStrokeColorDefaultBrush", window.ActualThemeVariant);
                    using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
                    {
                        Assert.Equal((name, baseColour), (name, DrawnAt(fb, 2, band.Top + 8)));
                        Assert.Equal((name, divider), (name, DrawnAt(fb, 2, band.Top)));
                    }

                    // 24 px of padding round the content: the footer's sibling in the dock.
                    var dock = Assert.IsType<DockPanel>(footer.Parent);
                    Assert.Equal((name, new Thickness(24)), (name, Assert.Single(dock.Children, c => c != footer).Margin));

                    // The buttons: one width, Fluent's spacing between them, the last 24 px in from the right edge, none narrower than 96.
                    Button[] buttons = footer.Buttons.OfType<Button>().ToArray();
                    Assert.NotEmpty(buttons);
                    Rect[] at = buttons.Select(b => Frames.InWindow(b, window)).ToArray();
                    Assert.Single(at.Select(r => r.Width).Distinct());
                    Assert.True(at[0].Width >= 96, name + ": " + at[0].Width);
                    for (int i = 1; i < at.Length; i++) Assert.Equal((name, 8.0), (name, at[i].Left - at[i - 1].Right));
                    Assert.Equal((name, window.ClientSize.Width - 24), (name, at[^1].Right));

                    // What belongs to the choice rather than the answer (Apply to all, All) is at the left, clear of the buttons.
                    if (footer.Leading is { } leading)
                    {
                        Rect left = Frames.InWindow(leading, window);
                        Assert.Equal((name, 24.0), (name, left.Left));
                        Assert.True(left.Right <= at[0].Left, name + ": " + left.Right + " > " + at[0].Left);
                    }

                    // Enter takes the accent; Escape cancels, except while a run is going (Escape must not stop it).
                    Button[] defaults = buttons.Where(b => b.IsDefault).ToArray();
                    if (kind == typeof(ProcessingWindow))
                    {
                        Assert.Empty(defaults);
                        Assert.DoesNotContain(buttons, b => b.IsCancel);
                    }
                    else
                    {
                        Assert.Contains(Assert.Single(defaults).Classes, c => c == "accent");
                        Assert.Contains(buttons, b => b.IsCancel);
                        Assert.All(buttons.Except(defaults), b => Assert.DoesNotContain("accent", b.Classes));
                    }
                }
                finally
                {
                    window.Close();
                }
            }
            Assert.True(dialogs >= 10, dialogs + " dialog kinds");
        }
        finally
        {
            app.RequestedThemeVariant = before;
        }
    }

    [AvaloniaFact]
    public void TheFootersButtonsTakeTheirLeastWidthFromTheApplicationsResource()
    {
        // One source: App.axaml's ContentDialogButtonMinWidth (Fluent's 96), which the footer reads rather than a width of its own.
        Assert.True(Application.Current!.TryFindResource("ContentDialogButtonMinWidth", out object? width));
        var ok = new Button { Content = "OK" };
        var window = new Window { Content = DialogFooter.Compose(new TextBlock { Text = "Body" }, ok), SizeToContent = SizeToContent.WidthAndHeight };
        window.Show();
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(96.0, width);
        Assert.Equal(96.0, ok.MinWidth);

        window.Resources["ContentDialogButtonMinWidth"] = 150.0;   // a window that asks for wider buttons gets them
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(150.0, ok.MinWidth);
        window.Close();
    }

    [AvaloniaFact]
    public void ATextShowsItsStatusInTheThemesColourAndFollowsAChangeOfTheme()
    {
        var app = (App)Application.Current!;
        ThemeVariant? before = app.RequestedThemeVariant;
        app.ApplyTheme(false);
        var text = new TextBlock { Text = "x" };
        var window = new Window { Content = text };
        window.Show();
        try
        {
            StatusPaint.SetLevel(text, StatusLevel.Critical);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(Color.Parse("#C42B1C"), ((ISolidColorBrush)text.Foreground!).Color);
            // The same status in the other theme's shade, without the text being touched.
            app.ApplyTheme(true);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(Color.Parse("#FF99A4"), ((ISolidColorBrush)text.Foreground!).Color);
            StatusPaint.SetLevel(text, StatusLevel.Success);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(Color.Parse("#6CCB5F"), ((ISolidColorBrush)text.Foreground!).Color);
            // No status: the text colour the theme gives it again.
            StatusPaint.SetLevel(text, null);
            Dispatcher.UIThread.RunJobs();
            Assert.False(text.IsSet(TextBlock.ForegroundProperty));
        }
        finally
        {
            window.Close();
            app.RequestedThemeVariant = before;
        }
    }
}
