using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Presenters;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.LogicalTree;
using Avalonia.Threading;
using Avalonia.VisualTree;
using TmDq.App.Processing;
using TmDq.App.Services;
using TmDq.App.Streams;
using TmDq.App.ViewModels;
using TmDq.App.Views;
using TmDq.Core;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// The stream dialogs as windows on the headless platform (plan step 4.1b): what each shows and how its controls are
    /// bound, laid out by the real theme, and the windows opened and closed modally by <see cref="WindowStreamDialogs"/>.
    /// The rules behind them are tested without windows (StreamRulesTests, ConfigureStreamsTests,
    /// StreamSetupViewModelTests); these hold the windows to them.
    /// </summary>
    [Collection(NativeCoreCollection.Name)]
    public sealed class StreamWindowsTests
    {
        private static string Prn => Repo.Recording("prn_testfile, sample.ch10");

        public StreamWindowsTests() => NativeCore.EnsureLoaded();

        private static T Shown<T>(T window) where T : Window
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            return window;
        }

        private static ConfigureStreamsViewModel Configure(FakeStreamDialogs? dialogs = null) =>
            new(Prn, StreamsRun.Open(Prn), dialogs ?? new FakeStreamDialogs(), new FakeFilePicker(), Repo.Root);

        private static StreamSetup Row(StreamMode mode) =>
            new() { PcmChannelId = 13, Label = "CH-01 PRN 15 5M", Mode = mode, SettingsMode = mode, Process = true, TmatsDataRateMbps = 5 };

        private static List<Grid> RowGrids(Window w) =>
            w.FindControl<ItemsControl>("Rows")!.GetVisualDescendants().OfType<Grid>()
             .Where(g => g.DataContext is StreamRowViewModel && g.Parent is ContentPresenter).ToList();

        private static T InRow<T>(Grid row, int column) where T : Control =>
            row.Children.OfType<T>().Single(c => Grid.GetColumn(c) == column);

        [AvaloniaFact]
        public void ConfigureStreamsShowsARowPerChannelAndTheRecordingsSettings()
        {
            ConfigureStreamsViewModel vm = Configure();
            var w = Shown(new ConfigureStreamsWindow { DataContext = vm });
            Assert.Equal("Configure Streams", w.Title);
            List<Grid> rows = RowGrids(w);
            Assert.Equal(4, rows.Count);
            Assert.Equal(new[] { "CH-01 PRN 15 5M", "CH-02 PRN 15 20M", "CH-03 PRN 11 1M", "CH-04 PRN 11 5M" },
                         rows.Select(r => InRow<TextBlock>(r, 1).Text));
            Assert.All(rows, r => Assert.Equal("Frame Sync Lock", InRow<ComboBox>(r, 3).SelectedItem));
            Assert.All(rows, r => Assert.False(InRow<Button>(r, 4).IsEffectivelyEnabled));

            ComboBox time = w.FindControl<ComboBox>("TimeChannel")!;
            Assert.Equal("1 - IRIG-B", time.SelectedItem);
            Assert.False(w.FindControl<CheckBox>("LegacyChapter10")!.IsChecked);
            Assert.False(w.FindControl<Button>("ProcessButton")!.IsEffectivelyEnabled);
            Assert.True(w.FindControl<Button>("CancelButton")!.IsEffectivelyEnabled);
            Assert.Equal("Check at least one stream to process.", w.FindControl<TextBlock>("WhyNot")!.Text);
            w.Close();
        }

        [AvaloniaFact]
        public async Task TheReadyColumnAndProcessFollowTheRows()
        {
            var dialogs = new FakeStreamDialogs { OnEdit = FakeStreamDialogs.Ok() };
            ConfigureStreamsViewModel vm = Configure(dialogs);
            var w = Shown(new ConfigureStreamsWindow { DataContext = vm });
            Grid first = RowGrids(w)[0];
            TextBlock ready = InRow<TextBlock>(first, 5);
            Assert.DoesNotContain("isBlocking", ready.Classes);

            InRow<CheckBox>(first, 0).IsChecked = true;   // as a click does
            Dispatcher.UIThread.RunJobs();
            Assert.True(vm.Rows[0].Process);
            Assert.Contains("isBlocking", ready.Classes);
            Assert.Equal("✗", ready.Text);
            Assert.Equal("Click the gear icon to configure this stream.", ToolTip.GetTip(ready));
            Button gear = InRow<Button>(first, 4);
            Assert.True(gear.IsEffectivelyEnabled);

            await vm.Rows[0].ConfigureCommand.ExecuteAsync(null);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("✓", ready.Text);
            Assert.Contains("isReady", ready.Classes);
            Assert.DoesNotContain("isBlocking", ready.Classes);
            Assert.True(w.FindControl<Button>("ProcessButton")!.IsEffectivelyEnabled);
            Assert.Equal("", w.FindControl<TextBlock>("WhyNot")!.Text);

            InRow<ComboBox>(first, 3).SelectedIndex = 0;   // Receiver SNR
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(StreamMode.ReceiverChannelInfo, vm.Rows[0].Setup.Mode);
            Assert.Contains("isBlocking", ready.Classes);
            Assert.False(w.FindControl<Button>("ProcessButton")!.IsEffectivelyEnabled);
            w.Close();
        }

        [AvaloniaFact]
        public void TheAllBoxAndLegacyChapter10ReachTheViewModel()
        {
            ConfigureStreamsViewModel vm = Configure();
            var w = Shown(new ConfigureStreamsWindow { DataContext = vm });
            w.FindControl<CheckBox>("All")!.IsChecked = true;
            w.FindControl<CheckBox>("LegacyChapter10")!.IsChecked = true;
            Dispatcher.UIThread.RunJobs();
            Assert.All(vm.Rows, r => Assert.True(r.Process));
            Assert.All(RowGrids(w), r => Assert.True(InRow<CheckBox>(r, 0).IsChecked));
            Assert.True(vm.LegacyChapter10);
            w.Close();
        }

        [AvaloniaFact]
        public void TheFrameSyncLockDialogHidesTheReceiverSettings()
        {
            var vm = new StreamSetupViewModel(Row(StreamMode.FrameSyncLock), new FakeFilePicker(), null, null);
            var w = Shown(new StreamSetupWindow { DataContext = vm });
            Assert.Equal("Configure Frame Sync Lock — CH-01 PRN 15 5M", w.Title);
            Assert.False(w.FindControl<Control>("ReceiverGroup")!.IsVisible);
            Assert.Equal("334AABBF", w.FindControl<TextBox>("SyncPattern")!.Text);
            Assert.Equal(32767m, w.FindControl<NumericUpDown>("BitsPerFrame")!.Value);
            Assert.Equal("1 s", w.FindControl<ComboBox>("Period")!.SelectedItem);
            Assert.Equal("Apply to all Frame Sync Lock streams", w.FindControl<CheckBox>("ApplyToAll")!.Content);
            Assert.Equal("0 = TMATS (5.000 Mbps)", w.FindControl<TextBlock>("DataRateHint")!.Text);
            Assert.False(w.FindControl<TextBlock>("Problems")!.IsVisible);
            Assert.True(w.FindControl<Button>("OkButton")!.IsEffectivelyEnabled);
            w.Close();
        }

        [AvaloniaFact]
        public void TheReceiverSnrDialogShowsTheReceiverSettingsLaidOut()
        {
            var vm = new StreamSetupViewModel(Row(StreamMode.ReceiverChannelInfo) with { PolarityIndex = 1, SlopeIndex = 2 },
                                              new FakeFilePicker(), null, null);
            var w = Shown(new StreamSetupWindow { DataContext = vm });
            Control group = w.FindControl<Control>("ReceiverGroup")!;
            Assert.True(group.IsVisible);
            Assert.True(group.Bounds.Height > 60, $"receiver group {group.Bounds}");
            Assert.Equal("Negative", w.FindControl<ComboBox>("Polarity")!.SelectedItem);
            Assert.Equal("0–10 V", w.FindControl<ComboBox>("Slope")!.SelectedItem);
            Assert.Equal(20m, w.FindControl<NumericUpDown>("Scale")!.Value);
            Assert.Equal(16m, w.FindControl<NumericUpDown>("NumReceivers")!.Value);
            Assert.Equal(3m, w.FindControl<NumericUpDown>("ReceiverChannels")!.Value);
            // Everything fits in the window the content sized.
            foreach (string name in new[] { "SyncPattern", "LoadFrameSync", "SaveReceiverParams", "OkButton", "CancelButton" })
            {
                Rect r = Frames.InWindow(w.FindControl<Control>(name)!, w);
                Assert.True(r.Right <= w.ClientSize.Width + 0.5 && r.Bottom <= w.ClientSize.Height + 0.5, $"{name} {r} in {w.ClientSize}");
            }
            w.Close();
        }

        [AvaloniaFact]
        public void TypingAnInvalidPatternShowsTheProblemAndHoldsOk()
        {
            var vm = new StreamSetupViewModel(Row(StreamMode.FrameSyncLock), new FakeFilePicker(), null, null);
            var w = Shown(new StreamSetupWindow { DataContext = vm });
            TextBox sync = w.FindControl<TextBox>("SyncPattern")!;
            sync.Text = "";
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("", vm.SyncPattern);
            TextBlock problems = w.FindControl<TextBlock>("Problems")!;
            Assert.True(problems.IsVisible);
            Assert.Equal(StreamRules.PatternRequired, problems.Text);
            Assert.False(w.FindControl<Button>("OkButton")!.IsEffectivelyEnabled);

            sync.Text = "FE6B2840";
            Dispatcher.UIThread.RunJobs();
            Assert.False(problems.IsVisible);
            Assert.True(w.FindControl<Button>("OkButton")!.IsEffectivelyEnabled);
            w.Close();
        }

        [AvaloniaFact]
        public async Task TheDialogsOpenModallyAndCloseOnOkOrCancel()
        {
            var owner = Shown(new Window());
            var dialogs = new WindowStreamDialogs(owner);

            var setup = new StreamSetupViewModel(Row(StreamMode.FrameSyncLock), new FakeFilePicker(), null, null);
            Task edit = dialogs.EditStreamAsync(setup);
            Dispatcher.UIThread.RunJobs();
            Window open = Assert.IsType<StreamSetupWindow>(owner.OwnedWindows.Single());
            Assert.False(edit.IsCompleted);
            setup.OkCommand.Execute(null);
            Dispatcher.UIThread.RunJobs();
            await edit;
            Assert.True(setup.Accepted);
            Assert.False(open.IsVisible);

            ConfigureStreamsViewModel configure = Configure();
            Task shown = dialogs.ConfigureStreamsAsync(configure);
            Dispatcher.UIThread.RunJobs();
            Assert.IsType<ConfigureStreamsWindow>(owner.OwnedWindows.Single());
            configure.CancelCommand.Execute(null);
            Dispatcher.UIThread.RunJobs();
            await shown;
            Assert.False(configure.Accepted);
            Assert.Empty(owner.OwnedWindows);

            ConfigureStreamsViewModel ready = Configure(new FakeStreamDialogs { OnEdit = FakeStreamDialogs.Ok() });
            ready.Rows[0].Process = true;
            await ready.Rows[0].ConfigureCommand.ExecuteAsync(null);
            shown = dialogs.ConfigureStreamsAsync(ready);
            Dispatcher.UIThread.RunJobs();
            Assert.Single(owner.OwnedWindows);
            ready.ProcessCommand.Execute(null);
            Dispatcher.UIThread.RunJobs();
            await shown;
            Assert.True(ready.Accepted);
            Assert.Empty(owner.OwnedWindows);
            owner.Close();
        }
    }
}
