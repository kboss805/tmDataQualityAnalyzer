using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace TmDq.App.Views
{
    /// <summary>A report with OK, the Qt <c>QMessageBox::information</c> a calibration's summary is shown in (plan step 4.6).
    /// The text is selectable, and scrolls when it is long, as a report of 16 receivers can be.</summary>
    public sealed class ReportWindow : Window
    {
        public ReportWindow(string title, string text)
        {
            Title = title;
            SizeToContent = SizeToContent.WidthAndHeight;
            CanResize = false;
            ShowInTaskbar = false;
            WindowStartupLocation = WindowStartupLocation.CenterOwner;
            Body = new SelectableTextBlock { Text = text, TextWrapping = TextWrapping.Wrap, MaxWidth = 640 };
            OkButton = new Button
            {
                Content = "OK",
                IsDefault = true,
                IsCancel = true,
                MinWidth = 80,
                HorizontalContentAlignment = HorizontalAlignment.Center,
                HorizontalAlignment = HorizontalAlignment.Right,
            };
            OkButton.Click += (_, _) => Close();
            var layout = new DockPanel { Margin = new Thickness(16) };
            DockPanel.SetDock(OkButton, Dock.Bottom);
            OkButton.Margin = new Thickness(0, 12, 0, 0);
            layout.Children.Add(OkButton);
            layout.Children.Add(new ScrollViewer { Content = Body, MaxHeight = 480 });
            Content = layout;
        }

        public SelectableTextBlock Body { get; }

        public Button OkButton { get; }
    }
}
