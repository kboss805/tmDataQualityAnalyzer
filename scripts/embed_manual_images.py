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
VER = 'include/constants.h'
WALK = 'docs/manual/walkthroughs.html'

# TWO outputs from ONE source, so they cannot drift:
#   BASE - compiled into the exe as a Qt resource. ALWAYS present, in the installer
#          and the portable ZIP alike, and impossible to delete. That is what makes
#          "the user always has a manual" a structural guarantee rather than
#          something the installer has to uphold.
#   FULL - shipped as an installed file, offered as an optional installer task. It is
#          the base document PLUS the task walkthroughs and their figures.
#
# The walkthrough prose lives in docs/manual/walkthroughs.html and is spliced into
# FULL only, so the two documents genuinely differ - which is what the installer's
# optional-manual choice needed before it could be released.
BASE = 'resources/usermanual.html'
FULL = 'UserManual.html'


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

s = io.open(BASE, encoding='utf-8').read()

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
                     % (BASE, n_sub, n_foot))
print('version set to %s' % _v)

# --- figures, in document order -------------------------------------------
def after(anchor, block):
    global s
    assert anchor in s, 'anchor missing: %r' % anchor[:60]
    s = s.replace(anchor, anchor + block, 1)

def before(anchor, block):
    global s
    assert anchor in s, 'anchor missing: %r' % anchor[:60]
    s = s.replace(anchor, block + anchor, 1)

after('<p>Each PCM channel row in the Configure Streams dialog has:</p>',
      fig('Config Streams Dialg.png',
          'The Configure Streams dialog. Each row is one PCM channel; the Ready column confirms a '
          'stream is fully configured before you press Process.',
          'Configure Streams dialog listing four PCM channels with Process, Mode, Configure and Ready columns'))

after('<p>Right-click anywhere on the chart. Entries stay disabled until a file has been processed.</p>',
      fig('Main Context Menu.png',
          'The plot’s right-click menu — every plot control lives here. Entries that need more than '
          'one loaded file, such as Plot File, stay disabled until they apply.',
          'The plot right-click menu open over a chart, showing Set Plot Title, Plot File, '
          'View Mode (V), Customize View, Show Legend, Readout, X Axis, Y Axes, Reset View and Export'))

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

# --- reference-section figures ---------------------------------------------
# These illustrate the reference sections rather than the walkthroughs, which is
# why they were captured but initially never placed. Each is anchored on existing
# prose; after()/before() assert the anchor, so a reworded paragraph breaks the
# build instead of quietly losing a figure.

after('<h2 id="getting-started">1. Getting started</h2>',
      fig('walk-app-01-hamburger.png',
          'The application menu. Every command lives here, grouped into Process, Import/Export '
          'and Settings, with Help as a submenu at the bottom. Entries needing loaded data stay '
          'greyed out until they apply.',
          'The hamburger menu open, showing the Process, Import/Export and Settings sections and '
          'a Help submenu'))

before('<h2 id="configure">',
       fig('walk-inst-01-components.png',
           'The installer offers the fuller manual as an optional component. The built-in manual '
           'is always available from Help regardless of this choice.',
           'Installer page offering an optional full illustrated user manual'))

after('<h2 id="plot">3. Working with the plot</h2>',
      fig('walk-cust-01-lock-tab.png',
          'Customize View, on the Frame Sync Lock tab: one row per stream with its colour and an '
          'editable name. Hiding a stream here removes it from the plot without reprocessing.',
          'Customize Plot Series dialog on the Frame Sync Lock Streams tab listing four streams')
    + fig('walk-cust-03-recolor.png',
          'Choosing a series colour, reached by right-clicking a series in Customize View. Custom '
          'names and colours are what a processing template carries between files.',
          'The colour picker dialog for choosing a plot series colour'))

after('<h3>Navigating</h3>',
      fig('walk-app-03-time-window.png',
          'Set Time Window takes a start and stop as DDD:HH:MM:SS. Values outside the recording '
          'are clamped to its range, and the clamp is noted in the log.',
          'Set Time Window dialog with start and stop times entered'))

after('<h2 id="interface">7. Interface &amp; settings</h2>',
      fig('walk-err-01-log-error.png',
          'The log sidebar. Errors appear in red and warnings in amber among the normal progress '
          'messages - here a stream whose frame sync pattern was not found, which is why only '
          'three of the four curves reached the plot.',
          'The application showing the log sidebar with a red error message among progress lines')
    + fig('walk-theme-01-light-plot.png',
          'The light theme. The choice persists between sessions and applies to every window, '
          'dialog and plot.',
          'A frame sync lock plot rendered in the light theme'))

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

# Placed immediately before the next heading, so it lands at the end of the
# Import & export section regardless of how that section's prose is written. The
# previous version keyed on the section ending with '</ul>', and rewriting the
# prose broke it - a figure's position should depend on the heading it precedes,
# not on the shape of the paragraph before it.
before('<h2 id="interface">',
       fig('Export Dialog.png',
           'The export dialog. Each output has its own switch and filename, so you can produce '
           'any combination — image, data, log — in a single action.',
           'Export Data dialog with separate toggles and filename fields for image, CSV and log output'))

io.open(BASE, 'w', encoding='utf-8', newline='\n').write(s)
print('base manual : %-24s %5.0f KB, %d figures'
      % (BASE, len(s.encode('utf-8'))/1024, s.count('<figure>')))

# --- full manual = base + walkthroughs ------------------------------------
# Built from the SAME document object, never hand-copied - two manuals maintained
# as two documents would drift, and shipped-doc drift is this project's most
# repeated failure.
#
# The prose lives in docs/manual/walkthroughs.html rather than in this script, so
# it can be edited as HTML. Figure placeholders there are expanded here, which is
# what keeps the walkthroughs' figures on the same base64 pipeline as the rest.
full = s

def expand_figures(fragment):
    """Replace {{FIG:file|caption|alt}} placeholders with embedded <figure> blocks."""
    def sub(m):
        parts = m.group(1).split('|')
        if len(parts) != 3:
            raise SystemExit('bad FIG placeholder (need file|caption|alt): %r' % m.group(1))
        fname, caption, alt = (x.strip() for x in parts)
        if not os.path.exists(os.path.join(SRC, fname)):
            raise SystemExit('walkthrough figure not found: %s' % fname)
        return fig(fname, caption, alt)
    out, n = re.subn(r'\{\{FIG:([^}]+)\}\}', sub, fragment)
    print('walkthrough figures expanded: %d' % n)
    return out

if os.path.exists(WALK):
    fragment = io.open(WALK, encoding='utf-8').read()
    # Drop the fragment's own HTML comment header - it is guidance for whoever
    # edits the file, not manual content.
    fragment = re.sub(r'^\s*<!--.*?-->\s*', '', fragment, count=1, flags=re.S)
    fragment = expand_figures(fragment)

    # Number the walkthrough headings 1..N in document order. They are written
    # WITHOUT numbers in the fragment so that adding or reordering a section cannot
    # leave a stale number behind, and so the shift applied to the reference
    # sections below is derived from reality rather than kept in sync by hand.
    walk_ids = re.findall(r'<h2 id="(walk-[a-z-]+)">', fragment)
    for n, wid in enumerate(walk_ids, start=1):
        fragment = fragment.replace('<h2 id="%s">' % wid,
                                    '<h2 id="%s">%d. ' % (wid, n), 1)

    # The walkthroughs take 1..N ahead of the reference sections, so those shift by N -
    # in the nav AND in each heading, or the two disagree and the contents list stops
    # matching the document.
    #
    # The number pattern is \d+ rather than a fixed range: it was [1-8] when there were
    # eight reference sections, which meant adding a ninth silently left it unshifted -
    # numbered 9 in a full manual whose other sections had moved into the teens. A
    # counted range here is a trap that springs the next time someone writes a section.
    SHIFT = len(walk_ids)
    def renumber(text):
        text = re.sub(r'(<li><a href="#[a-z-]+">)(\d+)\. ',
                      lambda m: '%s%d. ' % (m.group(1), int(m.group(2)) + SHIFT), text)
        text = re.sub(r'(<h2 id="[a-z-]+">)(\d+)\. ',
                      lambda m: '%s%d. ' % (m.group(1), int(m.group(2)) + SHIFT), text)
        return text
    full = renumber(full)

    # Contents entries for the new sections, ahead of the renumbered ones.
    # Two-space indent to match the existing entries; the nav is hand-formatted.
    walk_nav = ''.join(
        '  <li><a href="#%s">%s</a></li>\n' % (wid, title) for wid, title in
        re.findall(r'<h2 id="(walk-[a-z-]+)">([^<]+)</h2>', fragment))
    if not walk_nav:
        raise SystemExit('no <h2 id="walk-..."> sections found in %s' % WALK)

    nav_anchor = '<ul>\n  <li><a href="#getting-started">'
    if nav_anchor not in full:
        raise SystemExit('contents list anchor not found - has the nav markup changed?')
    full = full.replace(nav_anchor, '<ul>\n' + walk_nav + '  <li><a href="#getting-started">', 1)

    # ...and the sections themselves, ahead of the first reference heading.
    body_anchor = '<h2 id="getting-started">'
    full = full.replace(body_anchor, fragment.rstrip() + '\n\n' + body_anchor, 1)

io.open(FULL, 'w', encoding='utf-8', newline='\n').write(full)
print('full manual : %-24s %5.0f KB, %d figures%s'
      % (FULL, len(full.encode('utf-8'))/1024, full.count('<figure>'),
         '  (identical to base - no walkthroughs yet)' if full == s else ''))
