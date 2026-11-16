using System;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace TmDq.App.ViewModels.Dialogs
{
    /// <summary>
    /// What a dialog's view model has in common: the dialog closes by raising <see cref="CloseRequested"/>, with
    /// <see cref="Accepted"/> saying whether it was accepted (OK, Process, Export) or cancelled. The window that shows it
    /// hooks the event and closes itself, so the view model reaches no window.
    /// </summary>
    public abstract class DialogViewModel : ObservableObject
    {
        protected DialogViewModel() => CancelCommand = new RelayCommand(Close);

        /// <summary>Raised when the dialog's accepting command or Cancel closes it; <see cref="Accepted"/> says which.</summary>
        public event EventHandler? CloseRequested;

        /// <summary>Whether the dialog was closed by its accepting command and not by Cancel.</summary>
        public bool Accepted { get; private set; }

        public IRelayCommand CancelCommand { get; }

        /// <summary>Closes the dialog as accepted. A command's own guard (what it needs before it may run) comes first.</summary>
        protected void Accept()
        {
            Accepted = true;
            Close();
        }

        private void Close() => CloseRequested?.Invoke(this, EventArgs.Empty);
    }
}
