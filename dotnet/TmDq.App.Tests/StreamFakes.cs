using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using TmDq.App.Services;
using TmDq.App.ViewModels;

namespace TmDq.App.Tests
{
    /// <summary>Answers the stream dialogs as a user would, through the view model each is handed.</summary>
    internal sealed class FakeStreamDialogs : IStreamDialogs
    {
        public Func<ConfigureStreamsViewModel, Task>? OnConfigure { get; set; }

        public Func<StreamSetupViewModel, Task>? OnEdit { get; set; }

        public List<StreamSetupViewModel> Edited { get; } = new();

        public int ConfigureCalls { get; private set; }

        public async Task ConfigureStreamsAsync(ConfigureStreamsViewModel vm)
        {
            ConfigureCalls++;
            if (OnConfigure is not null) await OnConfigure(vm);
        }

        public async Task EditStreamAsync(StreamSetupViewModel vm)
        {
            Edited.Add(vm);
            if (OnEdit is not null) await OnEdit(vm);
        }

        /// <summary>A setup dialog answered by changing it with <paramref name="change"/> and pressing OK.</summary>
        public static Func<StreamSetupViewModel, Task> Ok(Action<StreamSetupViewModel>? change = null) => vm =>
        {
            change?.Invoke(vm);
            vm.OkCommand.Execute(null);
            return Task.CompletedTask;
        };
    }

    /// <summary>A file dialog that gives the next queued answer (null is a cancel) and records what it was asked.</summary>
    internal sealed class FakeFilePicker : ISettingsFilePicker
    {
        private readonly Queue<string?> _answers;

        public FakeFilePicker(params string?[] answers) => _answers = new Queue<string?>(answers);

        public List<(string Kind, string Title, string? Folder)> Asked { get; } = new();

        public Task<string?> OpenAsync(string title, string? folder)
        {
            Asked.Add(("open", title, folder));
            return Task.FromResult(_answers.Count > 0 ? _answers.Dequeue() : null);
        }

        public Task<string?> SaveAsync(string title, string? folder)
        {
            Asked.Add(("save", title, folder));
            return Task.FromResult(_answers.Count > 0 ? _answers.Dequeue() : null);
        }
    }
}
