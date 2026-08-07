"""Embed docs/manual_images/processed/*.png into resources/usermanual.html.

Called by scripts/build_manual.ps1, which crops and downscales the screenshots
first - run that rather than this directly.

The manual is a Qt resource: it gets copied to a temp directory and opened in the
default browser, so relative image paths would not resolve. Every figure is
therefore a base64 data URI, keeping the manual one self-contained file.

Idempotent: it strips any figures and figure CSS already present before inserting
fresh ones, so it can run repeatedly against the manual in place. That matters
because the COMMITTED manual already contains embedded figures - regenerating from
it must not double them - and because prose edits between runs must survive.
"""
import io, base64, os, re

SRC = 'docs/manual_images/processed'
MAN = 'resources/usermanual.html'
VER = 'include/constants.h'


def app_version():
    """Read the version from AppVersion in constants.h - the single source of truth.

    Previously this script carried a literal old->new version replacement, which
    silently stopped matching the moment the manual already held the new string, so
    a regenerated manual kept whatever version it had. Deriving it means a release
    bump cannot leave the manual behind.
    """
    src = io.open(VER, encoding='utf-8').read()
    nums = [re.search(r'k%s\s*=\s*(\d+)' % k, src) for k in ('Major', 'Minor', 'Patch')]
    if not all(nums):
        raise SystemExit('could not parse AppVersion from %s' % VER)
    return '.'.join(m.group(1) for m in nums)

def fig(fname, caption, alt):
    with open(os.path.join(SRC, fname), 'rb') as f:
        b64 = base64.b64encode(f.read()).decode('ascii')
    return ('\n<figure>\n  <img alt="%s" src="data:image/png;base64,%s">\n'
            '  <figcaption>%s</figcaption>\n</figure>\n' % (alt, b64, caption))

s = io.open(MAN, encoding='utf-8').read()

# --- strip previously embedded figures so this is idempotent ---------------
before = s.count('<figure>')
s = re.sub(r'\n?<figure>.*?</figure>\n', '', s, flags=re.S)
s = re.sub(r'  figure \{.*?font-style: italic; \}\n', '', s, flags=re.S)
if before:
    print('stripped %d existing figure(s)' % before)

# --- styling --------------------------------------------------------------
old = "  nav ul { list-style: none; padding-left: 0; columns: 2; }"
new = """  figure { margin: 1.2rem 0; padding: 0; }
  figure img {
    display: block; max-width: 100%; height: auto;
    border: 1px solid var(--line); border-radius: 6px;
  }
  figcaption { color: var(--muted); font-size: .9em; margin-top: .45rem; font-style: italic; }
  nav ul { list-style: none; padding-left: 0; columns: 2; }"""
assert old in s; s = s.replace(old, new, 1)

# --- version, derived from constants.h so it cannot go stale ---------------
_v = app_version()
s, n_sub = re.subn(r'(<p class="subtitle">User Manual — Version )[0-9.]+(</p>)',
                   r'\g<1>%s\g<2>' % _v, s, count=1)
s, n_foot = re.subn(r'(This manual describes\s+version )[0-9.]+\.',
                    r'\g<1>%s.' % _v, s, count=1)
if not (n_sub and n_foot):
    raise SystemExit('version markers not found in %s (subtitle=%d footer=%d)'
                     % (MAN, n_sub, n_foot))
print('version set to %s' % _v)

# --- figures, in document order -------------------------------------------
def after(anchor, block):
    global s
    assert anchor in s, 'anchor missing: %r' % anchor[:60]
    s = s.replace(anchor, anchor + block, 1)

after('<p>Each PCM channel row in the Configure Streams dialog has:</p>',
      fig('Config Streams Dialg.png',
          'The Configure Streams dialog. Each row is one PCM channel; the Ready column confirms a '
          'stream is fully configured before you press Process.',
          'Configure Streams dialog listing four PCM channels with Process, Mode, Configure and Ready columns'))

after('<p>Right-click anywhere on the chart. Entries stay disabled until a file has been processed.</p>',
      fig('Main Context Menu.png',
          'The plot’s right-click menu — every plot control lives here. Entries that need more than '
          'one loaded file, such as Plot File, stay disabled until they apply.',
          'The plot right-click menu open over a chart, showing Set Plot Title, Plot File, View Mode, '
          'Customize View, Show Legend, Readout, X Axis, Y Axes and Export'))

after('accumulation line.</p>',
      fig('FrameSync Perctentage Plot with Legend.png',
          'Lock&nbsp;% view. These streams hold near 100% and then drop vertically to zero at loss '
          'of lock — the sharp cliffs are the events worth investigating. The legend names each '
          'stream, and there is no SNR scale on the right because this file carries none.',
          'Frame sync lock percentage plot with four streams near 100 percent dropping vertically to zero')
    + fig('Frame Error Accumulation Plot with Legend.png',
          'The same streams in <strong>Accumulation</strong> view (press <kbd>V</kbd>, or use the '
          'on-chart button, to switch). Each vertical rise is frames lost at one of those cliffs; a '
          'flat line means no frames were missed.',
          'Accumulated missed frames plot showing step increases where lock was lost'))

after('to the nearest calibrated step rather than extrapolating.</p>',
      fig('Uncalibrated SNR Plot without Legend.png',
          'An AGC step sweep <strong>without</strong> a calibration profile: the steps are uneven '
          'and the channels sit slightly apart, because raw counts are only being scaled linearly.',
          'Receiver SNR staircase without calibration, showing uneven step heights and offset channels')
    + fig('Calibrated SNR Plot without Legend.png',
          'The same sweep <strong>with</strong> a non-linear calibration profile attached: the steps '
          'are evenly spaced and reach the injected levels, topping out at 60&nbsp;dB. Comparing '
          'these two is the quickest way to confirm a calibration actually took.',
          'Receiver SNR staircase with calibration applied, showing evenly spaced steps reaching 60 dB'))

# The legend-toggle bullet is the one place where the with/without contrast is the
# point, rather than incidental to whatever else the figure illustrates.
after('      sessions, and a hidden legend is also left out of exported images.</li>\n',
      fig('FrameSync Perctentage Plot with Legend.png',
          'The legend shown — one row per visible series, draggable anywhere inside the chart.',
          'Plot with the legend overlay visible in the top-right corner')
    + fig('FrameSync Perctentage Plot without Legend.png',
          'The same plot with the legend hidden from the <strong>Toggle Legend</strong> chip. A '
          'hidden legend is also left out of exported images.',
          'The same plot with the legend overlay hidden'))

marker = '</ul>\n\n<h2 id="interface">'
i = s.index(marker)
s = s[:i + len('</ul>\n')] + fig(
    'Export Dialog.png',
    'The export dialog. Each output has its own switch and filename, so you can produce any '
    'combination — image, data, log — in a single action.',
    'Export Data dialog with separate toggles and filename fields for image, CSV and log output'
) + s[i + len('</ul>\n'):]

io.open(MAN, 'w', encoding='utf-8', newline='\n').write(s)
print('figures embedded: %d' % s.count('<figure>'))
print('manual size: %.0f KB' % (len(s.encode('utf-8'))/1024))
