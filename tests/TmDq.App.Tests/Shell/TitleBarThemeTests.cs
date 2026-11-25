using System;
using System.Runtime.InteropServices;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Styling;
using Avalonia.Threading;
using TmDq.App.Platform;
using Xunit;

namespace TmDq.App.Tests;

/// <summary>
/// The title bar follows the theme where Avalonia does not make it (Windows 10): the request Windows is given, on a real window
/// of this machine's, and which Windows versions need it. What the title bar then looks like is the system's drawing, which a
/// headless window has none of; it was looked at on a real window (see the changelog).
/// </summary>
public sealed class TitleBarThemeTests
{
    private const uint OverlappedWindow = 0x00CF0000;   // WS_OVERLAPPEDWINDOW: a frame with a title bar

    [DllImport("user32.dll", EntryPoint = "CreateWindowExW", CharSet = CharSet.Unicode, ExactSpelling = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static extern IntPtr CreateWindowEx(uint exStyle, string className, string title, uint style, int x, int y, int width, int height,
                                                IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);

    [DllImport("user32.dll", ExactSpelling = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DestroyWindow(IntPtr window);

    private static IntPtr NewFrame()
    {
        IntPtr frame = CreateWindowEx(0, "STATIC", "title bar", OverlappedWindow, 0, 0, 200, 100, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
        Assert.NotEqual(IntPtr.Zero, frame);
        return frame;
    }

    [Fact]
    public void AWindowTakesTheDarkAndTheLightTitleBarAndSaysWhichItHas()
    {
        Assert.SkipUnless(OperatingSystem.IsWindows(), "the title bar is Windows'");
        IntPtr window = NewFrame();
        try
        {
            Assert.True(TitleBarTheme.Apply(window, dark: true));
            Assert.True(TitleBarTheme.IsDark(window));
            Assert.True(TitleBarTheme.Apply(window, dark: false, redraw: true));
            Assert.False(TitleBarTheme.IsDark(window));
            Assert.True(TitleBarTheme.Apply(window, dark: true, redraw: true));
            Assert.True(TitleBarTheme.IsDark(window));
        }
        finally
        {
            DestroyWindow(window);
        }
    }

    [Theory]
    [InlineData(17763, true)]    // Windows 10 1809: the first with a dark title bar to ask for
    [InlineData(19045, true)]    // Windows 10 22H2
    [InlineData(20348, true)]    // Windows Server 2022 (the hosted CI)
    [InlineData(21996, true)]    // the last build before Windows 11
    [InlineData(22000, false)]   // Windows 11: Avalonia sets it
    [InlineData(26300, false)]
    public void OnlyWindowsBeforeElevenNeedsHelp(int build, bool needsHelp) => Assert.Equal(needsHelp, TitleBarTheme.NeedsHelp(build));

    // ---- the wiring: a window's theme, as it opens and as it changes, reaches its title bar ----------------------------------

    [AvaloniaFact]
    public void OnWindows10AWindowsTitleBarFollowsItsThemeAsItOpensAndAsItChanges()
    {
        IntPtr frame = NewFrame();
        try
        {
            using IDisposable installed = TitleBarTheme.Install(19045, _ => frame);
            var window = new Window { RequestedThemeVariant = ThemeVariant.Dark };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            Assert.True(TitleBarTheme.IsDark(frame));          // dark as it opens
            window.RequestedThemeVariant = ThemeVariant.Light;
            Dispatcher.UIThread.RunJobs();
            Assert.False(TitleBarTheme.IsDark(frame));         // and light when the application switches
            window.RequestedThemeVariant = ThemeVariant.Dark;
            Dispatcher.UIThread.RunJobs();
            Assert.True(TitleBarTheme.IsDark(frame));

            installed.Dispose();                               // stopped: the theme no longer reaches the title bar
            window.RequestedThemeVariant = ThemeVariant.Light;
            Dispatcher.UIThread.RunJobs();
            Assert.True(TitleBarTheme.IsDark(frame));
            window.Close();

            var later = new Window { RequestedThemeVariant = ThemeVariant.Light };   // nor a window opened after it
            later.Show();
            Dispatcher.UIThread.RunJobs();
            Assert.True(TitleBarTheme.IsDark(frame));
            later.Close();
        }
        finally
        {
            DestroyWindow(frame);
        }
    }

    [AvaloniaFact]
    public void AWindowWithNoHandleUntilItIsUpHasItsTitleBarSetOnceItIs()
    {
        IntPtr frame = NewFrame();
        try
        {
            using IDisposable installed = TitleBarTheme.Install(19045, w => w.IsLoaded ? frame : null);
            var window = new Window { RequestedThemeVariant = ThemeVariant.Dark };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            Assert.True(window.IsLoaded);
            Assert.True(TitleBarTheme.IsDark(frame));          // by the second hook, when the window was up
            window.Close();
        }
        finally
        {
            DestroyWindow(frame);
        }
    }

    [AvaloniaFact]
    public void OnWindows11NothingIsInstalledAndAnotherThemeLeavesTheTitleBarAlone()
    {
        IntPtr frame = NewFrame();
        try
        {
            Assert.True(TitleBarTheme.Apply(frame, dark: true));
            using IDisposable installed = TitleBarTheme.Install(22000, _ => frame);   // Avalonia sets it: nothing of ours
            var window = new Window { RequestedThemeVariant = ThemeVariant.Light };
            window.Show();
            Dispatcher.UIThread.RunJobs();
            Assert.True(TitleBarTheme.IsDark(frame));
            window.Close();
        }
        finally
        {
            DestroyWindow(frame);
        }
    }

    [AvaloniaFact]
    public void AWindowThatDrawsItsOwnTitleBarAndTheOnesWithoutAHandleAreLeftAlone()
    {
        IntPtr frame = NewFrame();
        try
        {
            Assert.True(TitleBarTheme.Apply(frame, dark: true));
            using IDisposable installed = TitleBarTheme.Install(19045, w => w.ExtendClientAreaToDecorationsHint || w.Title == "own" ? frame : null);
            var own = new Window { RequestedThemeVariant = ThemeVariant.Light, ExtendClientAreaToDecorationsHint = true, Title = "own" };
            own.Show();
            Dispatcher.UIThread.RunJobs();
            Assert.True(TitleBarTheme.IsDark(frame));          // the main window's own title bar: not asked of Windows
            own.Close();

            var none = new Window { RequestedThemeVariant = ThemeVariant.Light };   // no handle yet: nothing to ask, and no failure
            none.Show();
            Dispatcher.UIThread.RunJobs();
            none.Close();
            Assert.True(TitleBarTheme.IsDark(frame));
        }
        finally
        {
            DestroyWindow(frame);
        }
    }
}
