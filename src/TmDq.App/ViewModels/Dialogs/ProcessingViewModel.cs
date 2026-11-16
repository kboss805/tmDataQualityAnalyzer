using System;
using System.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace TmDq.App.ViewModels.Dialogs;

/// <summary>
/// The progress dialog's state for one run (US1.0, US3.0, US3.2): the reader's progress, and Cancel, which stops
/// every stream. As in v2.x, Cancel only asks: the text becomes "Cancelling..." and
/// the button is disabled, and the dialog stays until the run has actually stopped.
/// </summary>
public sealed class ProcessingViewModel : ObservableObject, IDisposable
{
    private readonly CancellationTokenSource _cancel = new();
    private bool _disposed;
    private int _progress;
    private string _text;

    /// <param name="text">What the dialog says while the work goes on: the run's "Processing...", or a calibration
    /// extraction's own words.</param>
    public ProcessingViewModel(string text = "Processing...")
    {
        _text = text;
        CancelCommand = new RelayCommand(Cancel, () => !IsCancelling);
    }

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

    /// <summary>Releases the token source once the run has stopped and the dialog is gone; Cancel then does nothing.</summary>
    public void Dispose()
    {
        _disposed = true;
        _cancel.Dispose();
    }

    private void Cancel()
    {
        if (_disposed || IsCancelling) return;
        Text = "Cancelling...";
        _cancel.Cancel();
        OnPropertyChanged(nameof(IsCancelling));
        CancelCommand.NotifyCanExecuteChanged();
    }
}
