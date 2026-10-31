using CommunityToolkit.Mvvm.ComponentModel;

namespace TmDq.App.ViewModels
{
    /// <summary>The main window's state. A skeleton for now (plan step 3.1); the walking skeleton (3.4) gives it a
    /// recording to open and a series to show.</summary>
    public sealed partial class MainWindowViewModel : ObservableObject
    {
        public string Title => AppInfo.Title;

        public string VersionText => "Version " + AppInfo.Version;
    }
}
