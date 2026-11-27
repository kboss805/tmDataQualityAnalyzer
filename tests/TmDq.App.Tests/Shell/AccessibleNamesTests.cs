using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia.Automation.Peers;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Avalonia.VisualTree;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Streams;
using TmDq.Core;
using TmDq.App.ViewModels.Dialogs;
using TmDq.App.ViewModels.Plot;
using TmDq.App.Views.Dialogs;
using TmDq.App.Views.Plot;
using Xunit;
using static TmDq.App.Tests.AroundTheChartSupport;

namespace TmDq.App.Tests;

/// <summary>
/// What a screen reader is told about the controls: every control a person operates has a name that says what it is, and no two
/// in one window say the same. A control gets its name from its own text; a text box or a combo box has none, because the label
/// beside it is a separate element, and a button drawn as an icon font glyph is named by a private-use character no screen
/// reader can say. Held for every window the application has, and for the windows that are filled with rows (the streams
/// of a recording, the series of a plot), whose controls are made per row.
/// </summary>
public sealed class AccessibleNamesTests
{
    public AccessibleNamesTests() => NativeCore.EnsureLoaded();

    private static bool IsOperated(Control c) =>
        c is TextBox or ComboBox or Button or CheckBox or ToggleButton or RadioButton or Slider or ToggleSwitch
        && c is not RepeatButton
        // The parts of a control Avalonia draws (a ComboBox's drop-down button, the colour picker's) are named by it.
        && !(c.Name is { } n && n.StartsWith("PART_", StringComparison.Ordinal));

    /// <summary>Said aloud: it holds a letter or a digit. A glyph from an icon font does not.</summary>
    private static bool CanBeSpoken(string name) => name.Any(char.IsLetterOrDigit);

    /// <summary>The problems of <paramref name="window"/>, one line each: a control with no name that can be spoken, and a name
    /// two controls share.</summary>
    private static List<string> Problems(string what, Window window)
    {
        var problems = new List<string>();
        var seen = new Dictionary<string, string>();
        foreach (Control c in window.GetVisualDescendants().OfType<Control>().Where(c => c.IsVisible && IsOperated(c)))
        {
            // What the picker draws inside itself is Avalonia's, with names of its own.
            if (c.GetVisualAncestors().Any(a => a is ColorView)) continue;
            string name = ControlAutomationPeer.CreatePeerForElement(c).GetName();
            string where = $"{what}: {c.GetType().Name}{(string.IsNullOrEmpty(c.Name) ? "" : " " + c.Name)}";
            if (!CanBeSpoken(name))
            {
                problems.Add($"{where} has no name a screen reader can say (it is '{name}')");
            }
            else if (seen.TryGetValue(name, out string? first))
            {
                problems.Add($"{where} has the name '{name}', which {first} has too");
            }
            else
            {
                seen[name] = where;
            }
        }
        return problems;
    }

    private static List<string> ProblemsOfShown(string what, Window window)
    {
        window.Show();
        Dispatcher.UIThread.RunJobs();
        try
        {
            return Problems(what, window);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void EveryWindowNamesWhatItOffers()
    {
        var problems = new List<string>();
        foreach ((Type kind, Window window) in IconTests.EveryWindowKind())
        {
            // The stream setup dialog takes its words from its view model (the "Apply to all" box reads "Apply to all
            // Frame Sync Lock streams"), so a window with none has a box with no text: the test below holds it, with one.
            if (kind == typeof(StreamSetupWindow))
            {
                window.Close();
                continue;
            }
            problems.AddRange(ProblemsOfShown(kind.Name, window));
        }
        Assert.True(problems.Count == 0, string.Join("\n", problems));
    }

    [AvaloniaFact]
    public void TheRowsAndTheSetupDialogsOfARecordingAreNamed()
    {
        string prn = Repo.Recording("prn_testfile, sample.ch10");
        var configure = new ConfigureStreamsViewModel(prn, StreamsRun.Open(prn), new FakeStreamDialogs(), null, Repo.Root);
        Assert.NotEmpty(configure.Rows);
        var problems = new List<string>(ProblemsOfShown("ConfigureStreamsWindow with rows", new ConfigureStreamsWindow { DataContext = configure }));

        var customize = new CustomizeViewModel(Mixed(), new FakePrompts());
        Assert.NotEmpty(customize.LockRows);
        problems.AddRange(ProblemsOfShown("CustomizeViewWindow with rows", new CustomizeViewWindow { DataContext = customize }));

        // The stream setup dialog of a frame sync stream, and of a receiver one (which shows the receiver half too).
        Ch10File file = StreamsRun.Open(prn);
        Ch10File step = StreamsRun.Open(Repo.Recording("step_cal, example.ch10"));
        StreamSetup lockRow = StreamRules.ForEditing(StreamDefaults.NewStream(file.PcmChannels[0]) with { Process = true }, Repo.Root);
        problems.AddRange(ProblemsOfShown("StreamSetupWindow, lock", new StreamSetupWindow
        {
            DataContext = new StreamSetupViewModel(lockRow, null, Repo.Root, null, file.DefaultTimeChannel, true, new FakeStreamDialogs(), null),
        }));
        StreamSetup snrRow = StreamRules.ForEditing(StreamRules.WithMode(StreamDefaults.NewStream(step.PcmChannels[0]), StreamMode.ReceiverChannelInfo)
                                                    with { Process = true }, Repo.Root);
        problems.AddRange(ProblemsOfShown("StreamSetupWindow, receiver", new StreamSetupWindow
        {
            DataContext = new StreamSetupViewModel(snrRow, null, Repo.Root, null, step.DefaultTimeChannel, true, new FakeStreamDialogs(), null),
        }));

        Assert.True(problems.Count == 0, string.Join("\n", problems));
    }

    [AvaloniaFact]
    public void TheGuardSeesAControlWithNoName()
    {
        // The guard is only worth having if it can fail: a text box beside a label, and a button drawn as a glyph.
        var window = new Window
        {
            Content = new StackPanel
            {
                Children = { new TextBlock { Text = "Rate" }, new TextBox(), new Button { Content = "" }, new Button { Content = "Go" }, new Button { Content = "Go" } },
            },
        };
        List<string> problems = ProblemsOfShown("test window", window);
        Assert.Equal(3, problems.Count);
        Assert.Contains(problems, p => p.Contains("TextBox", StringComparison.Ordinal));
        Assert.Contains(problems, p => p.Contains("Button", StringComparison.Ordinal) && p.Contains("no name", StringComparison.Ordinal));
        Assert.Contains(problems, p => p.Contains("'Go', which", StringComparison.Ordinal));
    }
}
