"""Embed docs/manual_images/processed/*.png into resources/usermanual.html.

Called by scripts/build_manual.ps1, which crops and downscales the screenshots
first - run that rather than this directly.

The manual is a Qt resource: it gets copied to a temp directory and opened in the
default browser, so relative image paths would not resolve. Every figure is
therefore a base64 data URI, keeping the manual one self-contained file.

NOT idempotent on its own: it inserts figures at text anchors, so run it against
the COMMITTED manual (git checkout resources/usermanual.html first) or the images
are embedded twice. build_manual.ps1 is the supported entry point.
"""
import io, base64, os

SRC = 'docs/manual_images/processed'
MAN = 'resources/usermanual.html'

def fig(fname, caption, alt):
    with open(os.path.join(SRC, fname), 'rb') as f:
        b64 = base64.b64encode(f.read()).decode('ascii')
    return ('\n<figure>\n  <img alt="%s" src="data:image/png;base64,%s">\n'
            '  <figcaption>%s</figcaption>\n</figure>\n' % (alt, b64, caption))

s = io.open(MAN, encoding='utf-8').read()

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

s = s.replace('<p class="subtitle">User Manual — Version 2.7.0</p>',
              '<p class="subtitle">User Manual — Version 2.9.0</p>', 1)

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
      fig('FrameSync Perctentage Plot.png',
          'Lock&nbsp;% view. These streams hold near 100% and then drop vertically to zero at loss '
          'of lock — the sharp cliffs are the events worth investigating.',
          'Frame sync lock percentage plot with four streams near 100 percent dropping vertically to zero')
    + fig('Frame Accumulation Plot.png',
          'The same streams in <strong>Accumulation</strong> view (press <kbd>V</kbd>, or use the '
          'on-chart button, to switch). Each vertical rise is frames lost at one of those cliffs; a '
          'flat line means no frames were missed.',
          'Accumulated missed frames plot showing step increases where lock was lost'))

after('to the nearest calibrated step rather than extrapolating.</p>',
      fig('Uncalibrated SNR plot.png',
          'An AGC step sweep <strong>without</strong> a calibration profile: the steps are uneven '
          'and the channels sit slightly apart, because raw counts are only being scaled linearly.',
          'Receiver SNR staircase without calibration, showing uneven step heights and offset channels')
    + fig('calibrated SNR plot.png',
          'The same sweep <strong>with</strong> a non-linear calibration profile attached: the steps '
          'are evenly spaced and reach the injected levels, topping out at 60&nbsp;dB. Comparing '
          'these two is the quickest way to confirm a calibration actually took.',
          'Receiver SNR staircase with calibration applied, showing evenly spaced steps reaching 60 dB'))

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
