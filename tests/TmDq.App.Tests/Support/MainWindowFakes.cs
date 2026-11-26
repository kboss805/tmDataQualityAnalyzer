using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using TmDq.App.Services;
using TmDq.App.ViewModels.Dialogs;

namespace TmDq.App.Tests;

/// <summary>The Export dialog without its window: it keeps the view model it was shown and lets the test answer it.</summary>
internal sealed class FakeExportDialog : IExportDialog
{
    public ExportViewModel? Shown { get; private set; }

    /// <summary>What the "user" does in the dialog; nothing (the dialog left as it opened) when unset.</summary>
    public Action<ExportViewModel>? Answer { get; set; }

    public Task ShowAsync(ExportViewModel vm)
    {
        Shown = vm;
        Answer?.Invoke(vm);
        return Task.CompletedTask;
    }
}

/// <summary>The progress dialog without its window: the text of each it was shown, how often it was closed, and a hook
/// that can press Cancel through the view model, as the user would.</summary>
internal sealed class FakeProgressDialog : IProcessingDialog
{
    public List<string> Shown { get; } = new();

    public int Closed { get; private set; }

    public Action<ProcessingViewModel>? OnShow { get; set; }

    public void Show(ProcessingViewModel vm)
    {
        Shown.Add(vm.Text);
        OnShow?.Invoke(vm);
    }

    public void Close() => Closed++;
}
