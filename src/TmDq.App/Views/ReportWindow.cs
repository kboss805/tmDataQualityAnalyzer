using Avalonia.Controls;
using Avalonia.Media;

namespace TmDq.App.Views
{
    /// <summary>A report with OK, which a calibration's summary is shown in (plan step 4.6, US5.3).
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
            OkButton = new Button { Content = "OK", IsDefault = true, IsCancel = true };
            OkButton.Click += (_, _) => Close();
            Content = DialogFooter.Compose(new ScrollViewer { Content = Body, MaxHeight = 480 }, OkButton);
        }

        public SelectableTextBlock Body { get; }

        public Button OkButton { get; }
    }
}
