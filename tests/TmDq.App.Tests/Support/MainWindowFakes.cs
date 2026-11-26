using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using TmDq.App.Models.Export;
using TmDq.App.Services;
using TmDq.App.ViewModels.Dialogs;

namespace TmDq.App.Tests;

/// <summary>What kind of file a CSV is (US10.0), answered without the dialog: the queued answers in turn, then
/// <see cref="Always"/> (null, a cancel, unless set). It notes each file it was asked about.</summary>
internal sealed class FakeCsvKinds : ICsvKindDialog
{
    public Queue<CsvKind?> Answers { get; } = new();

    public List<string> Asked { get; } = new();

    /// <summary>The answer once the queue is empty.</summary>
    public CsvKind? Always { get; init; }

    public Task<CsvKind?> ChooseAsync(string fileName)
    {
        Asked.Add(fileName);
        return Task.FromResult(Answers.Count > 0 ? Answers.Dequeue() : Always);
    }
}

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
