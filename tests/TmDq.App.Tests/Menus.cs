using System.Collections.Generic;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using TmDq.App.Views;

namespace TmDq.App.Tests
{
    /// <summary>
    /// Reading the menus as they are declared in the AXAML: open, so their bindings have their view model (a menu is a
    /// popup of its own, and takes its data context from where it opens), and with the items of a list filled from the
    /// data (Recent Files, Plot File, Readout) made, which happens only once its submenu opens.
    /// </summary>
    internal static class Menus
    {
        /// <summary>The plot's right-click menu, opened again over the chart as a right-click or the menu key opens it: by
        /// a context request, which raises its Opening (where the readout's stale pin is dropped), as
        /// <see cref="ContextMenu.Open(Control)"/> alone does not.</summary>
        public static ContextMenu Open(PlotView view)
        {
            if (view.ChartMenu.IsOpen) view.ChartMenu.Close();
            view.Chart.RaiseEvent(new ContextRequestedEventArgs());
            Dispatcher.UIThread.RunJobs();
            return view.ChartMenu;
        }

        /// <summary>The main window's menu, open under its button.</summary>
        public static MenuFlyout Open(MainWindow window)
        {
            if (window.MainMenu.IsOpen) window.MainMenu.Hide();
            window.MainMenu.ShowAt(window.MenuButton);
            Dispatcher.UIThread.RunJobs();
            return window.MainMenu;
        }

        /// <summary>What a menu shows: its own items, or the menu items made for a list's entries. A submenu is opened
        /// first, as the user opens it: only then are a list's items made, and only an item shown follows whether its
        /// command can run (closed, it keeps what it last saw).</summary>
        public static IReadOnlyList<object?> Entries(ItemsControl menu)
        {
            if (menu is MenuItem { IsSubMenuOpen: false } item && item.ItemCount > 0)
            {
                item.IsSubMenuOpen = true;
                Dispatcher.UIThread.RunJobs();
            }
            if (menu.ItemsSource is null) return menu.Items.ToList();
            return Enumerable.Range(0, menu.ItemCount).Select(i => (object?)menu.ContainerFromIndex(i)).ToList();
        }

        /// <summary>Each entry's header, "-" for a separator.</summary>
        public static string[] Headers(ItemsControl menu) => Entries(menu).Select(i => i is MenuItem m ? (string)m.Header! : "-").ToArray();

        public static string[] Headers(IEnumerable<object?> items) => items.Select(i => i is MenuItem m ? (string)m.Header! : "-").ToArray();

        public static MenuItem Item(ItemsControl menu, string header) =>
            Entries(menu).OfType<MenuItem>().Single(i => (string?)i.Header == header);

        public static MenuItem Item(IEnumerable<object?> items, string header) =>
            items.OfType<MenuItem>().Single(i => (string?)i.Header == header);
    }
}
