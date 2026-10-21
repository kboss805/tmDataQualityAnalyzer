"""Runs single-defect mutants against one UI test project; each must fail the test written for it.

usage: python dotnet/hybrid-spike/uitest/run_mutants.py avalonia|wpf

Each mutant is applied, the project rebuilt (exe deleted first, so a failed build cannot run the
previous binary), the suite run, and the source restored with a fresh mtime. A mutant counts as caught
only if the test written for it fails, not merely some test.
"""
import io
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__)).replace(os.sep, '/')
which = sys.argv[1]

if which == 'avalonia':
    proj = ROOT + '/avalonia/AvaloniaUiTests.csproj'
    exe = ROOT + '/avalonia/bin/Release/net10.0/AvaloniaUiTests.exe'
    X, C = ROOT + '/avalonia/PlotView.axaml', ROOT + '/avalonia/PlotView.axaml.cs'
    mutants = [
        ('theme-style minimum beats the size set in code', X,
         '<Setter Property="Foreground" Value="White"/>', '<Setter Property="Foreground" Value="White"/><Setter Property="MinHeight" Value="32"/>',
         'TitleButtonKeepsTheSizeSetInCode'),
        ('legend height not capped', X, 'MaxHeight="120"', 'MaxHeight="2000"', 'LegendGrowsWithItsRowsAndScrollsOnceItIsFull'),
        ('L key ignored', C, 'case Key.L: vm.ToggleLegend(); break;', 'case Key.L: break;', 'KeyboardShortcutsDriveTheView'),
        ('legend visibility not bound', X, 'IsVisible="{Binding LegendVisible}" ', '', 'KeyboardShortcutsDriveTheView'),
        ('drag maps x by the height', C, 'double w = plot.Bounds.Width,', 'double w = plot.Bounds.Height,', 'DraggingABandZoomsToIt'),
        ('legend background transparent', X, 'Background="#E6101828"', 'Background="#00101828"', 'ARenderedFrameShowsTheLegendOverThePlotWithText'),
        ('legend label text not drawn', X, '<TextBlock Text="{Binding}"', '<TextBlock Text=""', 'ARenderedFrameShowsTheLegendOverThePlotWithText'),
    ]
else:
    proj = ROOT + '/wpf/WpfUiTests.csproj'
    exe = ROOT + '/wpf/bin/Release/net10.0-windows/WpfUiTests.exe'
    X, C = ROOT + '/wpf/PlotView.xaml', ROOT + '/wpf/PlotView.xaml.cs'
    mutants = [
        ('theme-style minimum beats the size set in code', X,
         '<Setter Property="Foreground" Value="White"/>', '<Setter Property="Foreground" Value="White"/><Setter Property="MinHeight" Value="32"/>',
         'TitleButtonKeepsTheSizeSetInCode'),
        ('legend height not capped', X, 'MaxHeight="120"', 'MaxHeight="2000"', 'LegendGrowsWithItsRowsAndScrollsOnceItIsFull'),
        ('L key ignored', C, 'case Key.L: vm.ToggleLegend(); break;', 'case Key.L: break;', 'KeyboardShortcutsDriveTheView'),
        ('legend visibility not bound', X, 'Visibility="{Binding LegendVisible, Converter={StaticResource Vis}}" ', '', 'KeyboardShortcutsDriveTheView'),
        ('drag maps x by the height', C, 'double w = Plot.ActualWidth;', 'double w = Plot.ActualHeight;', 'DraggingABandZoomsToIt'),
        ('legend background transparent', X, 'Background="#E6101828"', 'Background="#00101828"', 'ARenderedFrameShowsTheLegendOverThePlotWithText'),
        ('legend label text not drawn', X, '<TextBlock Text="{Binding}"', '<TextBlock Text=""', 'ARenderedFrameShowsTheLegendOverThePlotWithText'),
    ]


def run():
    if os.path.exists(exe):
        os.remove(exe)
    b = subprocess.run(['dotnet', 'build', proj, '-c', 'Release', '-nologo', '-v', 'q'], capture_output=True, text=True)
    if b.returncode != 0 or not os.path.exists(exe):
        return None, 'BUILD FAILED: ' + (b.stdout + b.stderr)[-400:]
    r = subprocess.run([exe], capture_output=True, text=True, timeout=600)
    out = r.stdout + r.stderr
    failed = sorted(set(re.findall(r'\.(\w+) \[FAIL\]', out)))
    summary = re.search(r'Total: (\d+), Errors: (\d+), Failed: (\d+)', out)
    return failed, summary.group(0) if summary else out[-300:]


failed, s = run()
print('baseline:', s, failed)
assert failed == [], 'baseline must be green'
caught = 0
for name, path, old, new, expect in mutants:
    src = io.open(path, encoding='utf-8', newline='').read()
    assert src.count(old) == 1, (name, src.count(old))
    try:
        io.open(path, 'w', encoding='utf-8', newline='').write(src.replace(old, new))
        failed, s = run()
    finally:
        io.open(path, 'w', encoding='utf-8', newline='').write(src)
        os.utime(path, None)
    ok = failed is not None and expect in failed
    caught += ok
    print('%-50s %s  failed=%s  %s' % (name, 'CAUGHT' if ok else 'SURVIVED', failed, s))
failed, s = run()
print('restored:', s, failed)
print('caught %d of %d' % (caught, len(mutants)))
