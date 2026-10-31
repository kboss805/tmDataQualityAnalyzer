using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace TmDq.App.Views
{
    /// <summary>The progress dialog (US1.0): shown modally while a run goes on. Closing it from the title bar or with
    /// Alt+F4 is refused, as the Qt dialog refused it, so a stray close cannot leave a run going unseen; Cancel is the
    /// only way out, and the window closes when the run has stopped.</summary>
    public sealed partial class ProcessingWindow : Window
    {
        private bool _done;

        public ProcessingWindow()
        {
            AvaloniaXamlLoader.Load(this);
            // The owner closing, or the application or Windows shutting down, still takes it with them.
            Closing += (_, e) => e.Cancel = !_done && e.CloseReason is WindowCloseReason.Undefined or WindowCloseReason.WindowClosing;
        }

        /// <summary>Closes the window; the only close it accepts.</summary>
        public void CloseWhenDone()
        {
            _done = true;
            Close();
        }
    }
}
