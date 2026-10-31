using System.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace TmDq.App.ViewModels
{
    /// <summary>
    /// The progress dialog's state for one run (US1.0, US3.0, US3.2): the reader's progress, and Cancel, which stops
    /// every stream. As in the Qt <c>ProcessingProgressDialog</c>, Cancel only asks: the text becomes "Cancelling..." and
    /// the button is disabled, and the dialog stays until the run has actually stopped.
    /// </summary>
    public sealed partial class ProcessingViewModel : ObservableObject
    {
        private readonly CancellationTokenSource _cancel = new();
        private int _progress;
        private string _text = "Processing...";

        public ProcessingViewModel() => CancelCommand = new RelayCommand(Cancel, () => !IsCancelling);

        /// <summary>0 to 100.</summary>
        public int Progress
        {
            get => _progress;
            set => SetProperty(ref _progress, value);
        }

        public string Text
        {
            get => _text;
            private set => SetProperty(ref _text, value);
        }

        public bool IsCancelling => _cancel.IsCancellationRequested;

        /// <summary>Signalled by Cancel; the run stops when it is.</summary>
        public CancellationToken Token => _cancel.Token;

        public IRelayCommand CancelCommand { get; }

        private void Cancel()
        {
            if (IsCancelling) return;
            Text = "Cancelling...";
            _cancel.Cancel();
            OnPropertyChanged(nameof(IsCancelling));
            CancelCommand.NotifyCanExecuteChanged();
        }
    }
}
