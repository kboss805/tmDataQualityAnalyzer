using System;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Avalonia.VisualTree;
using TmDq.App.ViewModels;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// The application has no up/down incrementers anywhere (the user's decision of 2026-11-21): a number is typed into a
/// plain text box that refuses what is not a number as it is typed. These hold the decision so a spinner does not come
/// back by a new dialog, a copied snippet or a theme that styles one in.
/// </summary>
public sealed class NoSpinnersTests
{
    private static readonly string[] Spinners = { "NumericUpDown", "ButtonSpinner", "Spinner" };

    [Fact]
    public void NoSourceFileOfTheApplicationNamesASpinnerControl()
    {
        string root = Repo.Project("TmDq.App");
        string[] found = Directory.EnumerateFiles(root, "*.*", SearchOption.AllDirectories)
            .Where(f => f.EndsWith(".axaml", StringComparison.Ordinal) || f.EndsWith(".cs", StringComparison.Ordinal))
            .Where(f => !f.Contains(Path.DirectorySeparatorChar + "obj" + Path.DirectorySeparatorChar, StringComparison.Ordinal)
                     && !f.Contains(Path.DirectorySeparatorChar + "bin" + Path.DirectorySeparatorChar, StringComparison.Ordinal))
            .SelectMany(f => File.ReadLines(f).Select((line, i) => (File: Path.GetRelativePath(root, f), Line: i + 1, Text: line)))
            // A comment saying there is none is not one (this very guard is the place such a sentence is written).
            .Where(l => !l.Text.TrimStart().StartsWith("//", StringComparison.Ordinal) && !l.Text.TrimStart().StartsWith("<!--", StringComparison.Ordinal)
                     && !l.Text.TrimStart().StartsWith("///", StringComparison.Ordinal))
            .Where(l => Spinners.Any(s => l.Text.Contains(s, StringComparison.Ordinal)))
            .Select(l => $"{l.File}({l.Line}): {l.Text.Trim()}")
            .ToArray();
        Assert.True(found.Length == 0, "a spinner control is named (use a TextBox and NumberText):\n" + string.Join("\n", found));
    }

    [AvaloniaTheory]
    [InlineData(false)]
    [InlineData(true)]
    public void NoWindowOfTheApplicationHoldsASpinnerInEitherTheme(bool dark)
    {
        var app = (App)Avalonia.Application.Current!;
        Avalonia.Styling.ThemeVariant? before = app.RequestedThemeVariant;
        app.ApplyTheme(dark);
        try
        {
            int windows = 0;
            foreach ((Type kind, Window window) in IconTests.EveryWindowKind())
            {
                windows++;
                window.Show();
                Dispatcher.UIThread.RunJobs();
                try
                {
                    string[] spinners = window.GetVisualDescendants()
                        .Where(v => v is NumericUpDown or ButtonSpinner)
                        .Select(v => v.GetType().Name).ToArray();
                    Assert.True(spinners.Length == 0, kind.Name + " holds " + string.Join(", ", spinners));
                }
                finally
                {
                    window.Close();
                }
            }
            Assert.True(windows >= 10, "only " + windows + " window kinds were checked");
        }
        finally
        {
            app.RequestedThemeVariant = before;
        }
    }

    [Theory]
    [InlineData("800", 800, true)]
    [InlineData(" -5 ", -5, true)]
    [InlineData("+7", 7, true)]
    [InlineData("99999999999", int.MaxValue, true)]   // past an int: the nearest, which the range check names
    [InlineData("-99999999999", int.MinValue, true)]
    [InlineData("7.0", 0, false)]
    [InlineData("1e3", 0, false)]
    [InlineData("0x10", 0, false)]
    [InlineData("1 000", 0, false)]
    [InlineData("1,000", 0, false)]
    [InlineData("", 0, false)]
    [InlineData("99999999999999999999", 0, false)]   // past a long: no integer
    public void AnIntegerIsDigitsWithAnOptionalSign(string text, int expected, bool whole) =>
        Assert.Equal(whole ? expected : null, NumberText.ParseInteger(text));

    [Theory]
    [InlineData("2.5", 2.5, true)]
    [InlineData(" .5 ", 0.5, true)]
    [InlineData("5.", 5.0, true)]
    [InlineData("-1e-3", -0.001, true)]
    [InlineData("1,5", 0, false)]
    [InlineData("NaN", 0, false)]
    [InlineData("-Infinity", 0, false)]
    [InlineData("1e999", 0, false)]
    [InlineData("", 0, false)]
    [InlineData("1.2.3", 0, false)]
    public void ANumberIsAFiniteDecimalWithAPoint(string text, double expected, bool number)
    {
        Assert.Equal(number ? expected : null, NumberText.ParseNumber(text));
        Assert.Equal(number ? (decimal)expected : null, number ? NumberText.ParseDecimal(text) : null);
        if (!number && text != "1e999") Assert.Null(NumberText.ParseDecimal(text));
    }

    [Fact]
    public void ANumberIsFormattedWithoutLosingAnyOfIt()
    {
        Assert.Equal("5.000", NumberText.FormatNumber(5));
        Assert.Equal("0.0005", NumberText.FormatNumber(0.0005));
        Assert.Equal("5.0", NumberText.FormatDecimal(5m));
        Assert.Equal("0.25", NumberText.FormatDecimal(0.25m));
        Assert.Equal("12", NumberText.FormatFixed(12.4, 0));
        Assert.Equal("1.0", NumberText.FormatFixed(1, 1));
        Assert.Equal("800", NumberText.FormatInteger(800));
    }
}
