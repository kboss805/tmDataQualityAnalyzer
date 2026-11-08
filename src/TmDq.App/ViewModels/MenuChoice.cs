using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;

namespace TmDq.App.ViewModels
{
    /// <summary>
    /// One entry of a menu whose entries come from the data: Recent Files, the plot's Plot File and Readout. The view
    /// shows each as a menu item (the <c>MenuChoice</c> and <c>RadioChoice</c> themes); a <see cref="Separator"/> is
    /// drawn as the menu's line. Only whether it is chosen changes once made, so a choice in an open menu is never
    /// replaced under the pointer.
    /// </summary>
    public sealed partial class MenuChoice : ObservableObject
    {
        /// <summary>The header Avalonia draws as a separator.</summary>
        public const string SeparatorHeader = "-";

        [ObservableProperty]
        private bool _isChecked;

        public MenuChoice(string header, ICommand? command = null, object? parameter = null)
        {
            Header = header;
            Command = command;
            Parameter = parameter;
        }

        /// <summary>A line between two groups of entries.</summary>
        public static MenuChoice Separator() => new(SeparatorHeader) { IsEnabled = false };

        public string Header { get; }

        public ICommand? Command { get; }

        public object? Parameter { get; }

        public bool IsEnabled { get; init; } = true;

        /// <summary>The entry's tooltip: a recent file's whole path.</summary>
        public string? Tip { get; init; }

        public bool IsSeparator => Header == SeparatorHeader;
    }
}
