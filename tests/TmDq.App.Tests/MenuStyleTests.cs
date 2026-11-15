using System;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Styling;
using Avalonia.Threading;
using Avalonia.VisualTree;
using TmDq.App.Views;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// The menus as VS Code draws them: text only (no icons), compact rows, the row under the pointer in the accent with white
    /// text, the shortcut dimmed until then.
    /// </summary>
    public class MenuStyleTests
    {
        private static Color Token(string key, ThemeVariant theme)
        {
            Assert.True(Application.Current!.TryFindResource(key, theme, out object? b), key);
            return ((ISolidColorBrush)b!).Color;
        }

        private static Color DrawnAt(ILockedFramebuffer fb, double x, double y) => Color.FromUInt32(0xFF000000 | Frames.Pixel(fb, (int)x, (int)y));

        /// <summary>A menu of a plain entry, one with a shortcut, a disabled section heading and a separator, in a window of its
        /// own (a popup is not drawn into its window's frame).</summary>
        private static (Window Window, MenuItem Plain, MenuItem Shortcut) Menu()
        {
            var plain = new MenuItem { Header = "Export..." };
            var shortcut = new MenuItem { Header = "Open...", InputGesture = new KeyGesture(Key.O, KeyModifiers.Control) };
            var presenter = new MenuFlyoutPresenter();
            presenter.Items.Add(new MenuItem { Header = "Process", IsEnabled = false, Classes = { "section" } });
            presenter.Items.Add(shortcut);
            presenter.Items.Add(plain);
            presenter.Items.Add(new Separator());
            presenter.Items.Add(new MenuItem { Header = "Exit" });
            var window = new Window { Content = presenter, SizeToContent = SizeToContent.WidthAndHeight };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            return (window, plain, shortcut);
        }

        private static void Hover(MenuItem item, bool on)
        {
            ((IPseudoClasses)item.Classes).Set(":selected", on);   // what a menu item takes under the pointer, or the keys
            Dispatcher.UIThread.RunJobs();
        }

        [AvaloniaFact]
        public void TheMainMenuHasNoIconsInIt()
        {
            var window = new MainWindow { DataContext = new TmDq.App.ViewModels.MainWindowViewModel() };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            try
            {
                MenuFlyout menu = Menus.Open(window);
                MenuItem[] items = menu.Items.OfType<MenuItem>().ToArray();
                Assert.True(items.Length >= 9, items.Length + " entries");
                Assert.All(items.Concat(Menus.Entries(Menus.Item(menu.Items, "Help")).OfType<MenuItem>()), i => Assert.Null(i.Icon));
            }
            finally
            {
                window.Close();
            }
        }

        [AvaloniaTheory]
        [InlineData(false)]
        [InlineData(true)]
        public void TheRowUnderThePointerIsTheAccentWithWhiteTextAndTheShortcutDimUntilThen(bool dark)
        {
            var app = (App)Application.Current!;
            ThemeVariant? before = app.RequestedThemeVariant;
            app.ApplyTheme(dark);
            (Window window, MenuItem plain, MenuItem shortcut) = Menu();
            try
            {
                ThemeVariant theme = window.ActualThemeVariant;
                Color accent = Token("AccentFillColorDefaultBrush", theme), secondary = Token("TextFillColorSecondaryBrush", theme);
                Color text = Token("TextFillColorPrimaryBrush", theme);
                TextBlock Hint() => shortcut.GetVisualDescendants().OfType<TextBlock>().Single(t => t.Text is { } s && s.Contains("Ctrl"));
                TextBlock Label(MenuItem i) => i.GetVisualDescendants().OfType<TextBlock>().First(t => t.Text == (string)i.Header!);

                // At rest: no fill, the text colour, the shortcut dimmed.
                Rect row = Frames.InWindow(plain, window);
                using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
                    Assert.Equal(Token("LayerFillColorDefaultBrush", theme), DrawnAt(fb, row.Left + 3, row.Center.Y));
                Assert.Equal((text, secondary), (((ISolidColorBrush)Label(plain).Foreground!).Color, ((ISolidColorBrush)Hint().Foreground!).Color));

                // Under the pointer: the row is the accent as drawn, its text and its shortcut white.
                Hover(plain, true);
                Hover(shortcut, true);
                row = Frames.InWindow(plain, window);
                using (ILockedFramebuffer fb = Frames.Capture(window).Lock())
                    Assert.Equal(accent, DrawnAt(fb, row.Left + 3, row.Center.Y));
                Assert.Equal(Colors.White, ((ISolidColorBrush)Label(plain).Foreground!).Color);
                Assert.Equal(Colors.White, ((ISolidColorBrush)Hint().Foreground!).Color);
            }
            finally
            {
                window.Close();
                app.RequestedThemeVariant = before;
            }
        }

        [AvaloniaFact]
        public void ARowIsCompactTwentyEightHighWithItsHighlightInsetFromTheMenusEdges()
        {
            (Window window, MenuItem plain, MenuItem shortcut) = Menu();
            try
            {
                Rect row = Frames.InWindow(plain, window), menu = new Rect(window.ClientSize);
                Assert.Equal((28.0, 28.0), (row.Height, Frames.InWindow(shortcut, window).Height));
                // The highlight does not run to the menu's edge: it is inset, as VS Code's is.
                Assert.True(row.Left >= 4 && menu.Right - row.Right >= 4, $"row {row} in menu {menu}");
            }
            finally
            {
                window.Close();
            }
        }
    }
}
