using Avalonia.Controls;

namespace TmDq.App.Views.Dialogs
{
    /// <summary>What the dialogs built in code have in common: a title, sized to their content, not resizable, not in the
    /// taskbar, and centred over the window that opened them. Their content is a <c>DialogFooter</c> over a body.</summary>
    public abstract class DialogWindow : Window
    {
        protected DialogWindow(string title)
        {
            Title = title;
            SizeToContent = SizeToContent.WidthAndHeight;
            CanResize = false;
            ShowInTaskbar = false;
            WindowStartupLocation = WindowStartupLocation.CenterOwner;
        }
    }
}
