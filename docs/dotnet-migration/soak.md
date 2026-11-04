# The side-by-side soak: the Avalonia application against the Qt one (plan step 5.2)

Plan step 5.2 asks for the same recordings processed in both applications, the CSV exports compared byte for byte and
the plots by eye, across every fixture and the full PRN recording. This is that record, made on 2026-11-02 on top of
step 5.1 (`361f8e8`).

## What it found

- **Every run exported the same bytes.** 34 runs over all 17 recordings on this machine (11.6 GB), 40 CSV files and
  58 MB of CSV between them: each one identical in the two applications.
- **The plots match.** Every curve, band, colour, axis range, tick, label and title is the same in the two applications'
  images. The differences are in the legend and the margins (below), and one of them is a defect in the Qt
  application.
- **The Qt batch export draws the legend before laying it out.** When Apply Template writes a recording's missed-frames
  image, the Qt legend has just been rebuilt for that view and its rows have not been placed yet, so they overlap: the
  "Fifteen" and "Eleven" rows of the PRN template are drawn over each other. The Avalonia application lays the view out
  before it exports (step 4.7 found and fixed this in its own batch), so its rows are clean. The Qt application has
  shipped this since batch export did (v2.7.0); it is not fixed there, since the switch-over (5.3) retires it.
- **The Avalonia application processes 3 to 6.5 % slower on the large recordings, and the whole difference is the
  process's CET shadow stack.** Pinned to the performance cores, three rounds each, the two applications alternating:
  the full PRN recording 12.4 s against 13.2 s, Safran 12.0 against 12.4-12.5 s, PRN-AGC 73.0-73.4 against 77.0-77.2 s,
  every round within a fraction of a percent of the others. The same four PRN streams run straight through `TmDq.Core`,
  with no application at all, take 13.2 s too, so nothing above the core adds time. Nor does the DLL's build (both
  compile the core with `/O2 /MD /fp:precise`, C++20, MSVC 14.44): the same `tmdq_core.dll` called from a small C++
  program takes 12.5 s, as Qt does. What differs is the host. .NET 9 and later mark the app host CET-compatible
  (`CETCompat`, on unless a project turns it off), so Windows keeps a hardware shadow stack of every call and return in
  the process, the native core's included. The C++ program linked with `/CETCOMPAT` takes 13.2 s; the C# one built with
  `CETCompat=false` takes 12.5 s. It stays on (the user's decision, 2026-11-02): it is the protection against
  return-oriented attacks for a process whose C library parses recordings it is given, and the Qt application never had
  it.
- **Memory.** The Avalonia process's peak is about 60 MB higher on every run, which is the .NET runtime, the test host
  and the headless UI; the largest run (PRN-AGC at 100 ms, a 31 MB CSV) is 89 MB higher, for the managed copies of its
  results beside the core's.
- **Nothing was found that holds up the switch-over.**

## How it was made

**One list of runs, read by both applications.** `dotnet/TmDq.App.Tests/Soak/soak.json` holds the 34 runs. Each names a
recording, the byte order and its streams, and every stream states every setting (mode, pattern, mask, frame length,
Derandomize, Invert Data, data rate, sample period, polarity, voltage range, scale, receivers, channels per receiver,
receiver parameters file, and a calibration where it has one), so neither application fills anything in from its own
defaults; the defaults are 5.1's, held by tests. Two runs are template batches instead, each naming a template and its
recordings.

**The Qt half was a throwaway harness in the Qt test suite** (never committed, in a worktree of its own). It drove the
Qt application's own pieces, wired as the Qt main window wires them: `MainViewModel` opening the recording and running
the configured streams, each finished stream handed to `PlotViewModel::addStreamData`, the title and source label set as
`MainView::onProcessingFinished` sets them, then `exportCsv` and `PlotWidget::exportImage`. A calibration was extracted
as the Qt Extract Calibration dialog extracts it (the step file parsed, `buildCalibrationRequest`, a
`CalibrationExtractor` run, the valid profiles kept by word). A batch ran through `BatchController` with every file
validated, the template's looks reused and each file exported, as the Qt confirmation leaves it. It wrote each run's
CSV, a 1600 x 900 image of the plot in the light theme, and the CSV's length and SHA-256. Its runs were made twice,
from a Debug and a Release build, and gave the same bytes.

**The Avalonia half is committed** (`SoakTests`, in `TmDq.App.Tests`). Each run goes through the application's own
path: a real `ConfigureStreamsViewModel` over the recording, each stream's row set as its setup dialog's OK leaves it,
a calibration extracted by a real `ExtractCalibrationViewModel` (its clips set, the recording chosen, OK), Process,
then `MainWindowViewModel.LoadAsync`; a batch through `MainWindowViewModel.RunBatchAsync`. It writes the same files,
and fails unless each CSV is, file for file, what the Qt application wrote: the manifest's `qt` blocks hold the Qt
application's length and SHA-256 for every file.

**The tests are explicit.** Most runs read recordings that are not committed (the full ones are gigabytes, the local
ones live only under `tests/data` on this machine), so the app suite lists the 34 cases as not run, rather than
skipping them or running them in CI. `scripts/run_soak.ps1` runs them, one process a run so that each run's peak
working set is its own, and prints a line a run. A missing recording fails; it never skips.

**Once Qt is gone (step 6.1) the manifest is the oracle.** The `qt` blocks were written by the Qt application and
stay, so the soak keeps holding the Avalonia application to the Qt application's output after there is no Qt
application to run.

## What it covered

**The recordings (17, every one on this machine):** the five committed ones, the full PRN, Safran and PRN-AGC
recordings (640 MB, 2.6 GB and 4.6 GB), and nine local ones (the three PRN test files, the two STEPCAL recordings, the
NRZ-L test and calibration recordings, STEP CALS and the RNRZ-L test recording).

**Which channels lock was found first,** by running six frame-sync guesses (PRN-15, PRN-11, and the 800- and 8000-bit
receiver frames each with and without Derandomize) over every channel of every recording, and four more with Invert
Data. Two recordings' TMATS names say the opposite of their data: STEP CALS channel 27 ("NRZ-L CODE OUT") is
randomized and channel 28 ("RNRZ-L DIRECT") is not, and PRN-AGC's channel 26 ("RNRZ-L") is not randomized. Safran's
channel 14 locks with PRN-15 in either byte order, its channel 13 only with Legacy Chapter 10; its channels 11 and 12,
and the RASA layout's channels 19 to 25, match none of the guesses and are not in the soak.

**The settings swept, on the small recordings:**
- each Frame Sync Lock sample rate (1 s, 100 ms, 10 ms) and each Receiver SNR averaging period (1 Hz, 10 Hz, 100 Hz);
- Frame Sync Lock and Receiver SNR, including both on one recording's channels and four streams at once;
- Derandomize on and off; Legacy Chapter 10 on (Safran) and off;
- a narrower sync mask, and a data rate typed over the TMATS one;
- the shipped default, RASA and TRC receiver parameters, with their polarities, voltage ranges and scales;
- non-linear calibration, six ways: step_cal from itself with the manual's clips; the NRZ-L test recording from its
  calibration recording, and the RASA recording from STEPCAL 1, each with the RASA step file; both STEP CALS channels
  from their own recording; the RNRZ-L recording from itself with the TRC step file;
- two template batches: the Qt application's own PRN template (names and colours reused) over five recordings, and a
  four-channel 100 ms template over three, each file exported (its CSV and both images).

The full recordings ran with their own defaults, at 1 s and at 10 ms or 100 ms.

**Not covered, and why:**
- **Invert Data with a run that locks:** no recording on this machine is inverted (the four inverted guesses locked
  nothing). Inversion is held by the core's own tests and by 4.6's calibration test of the raw-affecting flags.
- **An hours-long interactive session in both applications.** The soak is the data and the plots; the release build is
  used by hand at 5.3.

## The runs

Times are processing only, from Process to the last stream, both applications built in Release, each run in a process
of its own, unpinned. Peak is that process's peak working set; the Avalonia figure includes the .NET runtime, the test
host and the headless UI.

| run | recording | CSVs | CSV size | Qt | Avalonia | Qt peak | Avalonia peak |
|---|---|---|---|---|---|---|---|
| `prn-sample` | prn_testfile, sample.ch10 | 1 | 9 KB | 1.9 s | 2.1 s | 53 MB | 117 MB |
| `prn-sample-4ch-100ms` | prn_testfile, sample.ch10 | 1 | 90 KB | 1.9 s | 2.1 s | 53 MB | 118 MB |
| `prn-sample-4ch-10ms` | prn_testfile, sample.ch10 | 1 | 902 KB | 2.0 s | 2.1 s | 54 MB | 118 MB |
| `prn-sample-mask-rate` | prn_testfile, sample.ch10 | 1 | 41 KB | 1.5 s | 1.5 s | 53 MB | 118 MB |
| `step_cal-snr-1s` | step_cal, example.ch10 | 1 | 31 KB | 0.2 s | 0.2 s | 55 MB | 117 MB |
| `step_cal-snr-100ms` | step_cal, example.ch10 | 1 | 298 KB | 0.2 s | 0.2 s | 56 MB | 120 MB |
| `step_cal-snr-10ms` | step_cal, example.ch10 | 1 | 2.9 MB | 0.4 s | 0.2 s | 68 MB | 124 MB |
| `step_cal-snr-calibrated` | step_cal, example.ch10 | 1 | 31 KB | 0.2 s | 0.2 s | 56 MB | 118 MB |
| `step_cal-lock-10ms` | step_cal, example.ch10 | 1 | 242 KB | 0.2 s | 0.2 s | 53 MB | 115 MB |
| `trc-snr-1s` | agc_rnrz-l_trc_testfile.ch10 | 1 | 90 KB | 0.8 s | 0.8 s | 55 MB | 120 MB |
| `trc-snr-trc-100ms` | agc_rnrz-l_trc_testfile.ch10 | 1 | 879 KB | 0.8 s | 0.8 s | 59 MB | 126 MB |
| `trc-lock-10ms` | agc_rnrz-l_trc_testfile.ch10 | 1 | 736 KB | 0.8 s | 0.8 s | 53 MB | 118 MB |
| `rasa-snr-1s` | agc_nzl_rasa_testfile.ch10 | 1 | 42 KB | 0.4 s | 0.4 s | 54 MB | 117 MB |
| `rasa-snr-10ms` | agc_nzl_rasa_testfile.ch10 | 1 | 4.1 MB | 0.5 s | 0.4 s | 71 MB | 128 MB |
| `safran-sample-1s` | safran_testfile, sample.ch10 | 1 | 9 KB | 0.6 s | 0.7 s | 53 MB | 117 MB |
| `safran-sample-100ms` | safran_testfile, sample.ch10 | 1 | 91 KB | 0.6 s | 0.7 s | 53 MB | 117 MB |
| `prn-full-1s` | prn_testfile, full.ch10 | 1 | 32 KB | 12.5 s | 13.3 s | 53 MB | 118 MB |
| `prn-full-10ms` | prn_testfile, full.ch10 | 1 | 3.0 MB | 12.5 s | 13.3 s | 56 MB | 118 MB |
| `safran-full-1s` | safran_testfile, full.ch10 | 1 | 189 KB | 12.0 s | 12.5 s | 53 MB | 112 MB |
| `prn_agc-full-1s` | prn_agc_testfile, full.ch10 | 1 | 3.3 MB | 72.9 s | 77.4 s | 63 MB | 126 MB |
| `prn_agc-full-100ms` | prn_agc_testfile, full.ch10 | 1 | 30.7 MB | 73.0 s | 77.3 s | 162 MB | 251 MB |
| `stepcal1-rasa-1s` | STEPCAL 1 (NRZ-L).ch10 | 1 | 42 KB | 0.4 s | 0.4 s | 54 MB | 117 MB |
| `stepcal2-rasa-100ms` | STEPCAL 2 (NRZ-L).ch10 | 1 | 373 KB | 0.3 s | 0.4 s | 56 MB | 119 MB |
| `nrz-l-rasa-100ms` | nrz-l_testfile.ch10 | 1 | 420 KB | 0.4 s | 0.4 s | 57 MB | 120 MB |
| `nrz-l_cal-rasa-1s` | nrz-l_cal_testfile.ch10 | 1 | 42 KB | 0.4 s | 0.4 s | 55 MB | 117 MB |
| `nrz-l-rasa-calibrated` | nrz-l_testfile.ch10 | 1 | 42 KB | 0.4 s | 0.4 s | 55 MB | 117 MB |
| `rasa-calibrated-from-stepcal1` | agc_nzl_rasa_testfile.ch10 | 1 | 42 KB | 0.4 s | 0.4 s | 55 MB | 119 MB |
| `stepcals-both-100ms` | STEP CALS (RNRZ-L, NRZ-L).ch10 | 1 | 3.2 MB | 1.6 s | 1.5 s | 75 MB | 141 MB |
| `stepcals-both-calibrated` | STEP CALS (RNRZ-L, NRZ-L).ch10 | 1 | 337 KB | 1.4 s | 1.5 s | 60 MB | 129 MB |
| `rnrz-l-trc-snr-1s` | rnrz-l_testfile.ch10 | 1 | 90 KB | 0.8 s | 0.8 s | 55 MB | 119 MB |
| `rnrz-l-trc-calibrated` | rnrz-l_testfile.ch10 | 1 | 90 KB | 0.8 s | 0.8 s | 56 MB | 122 MB |
| `prn-test-file-0002-10ms` | PRN_TEST_FILE0002.ch10 | 1 | 3.0 MB | 12.4 s | 13.3 s | 56 MB | 121 MB |
| `batch-prn-sample-template` | 5 recordings | 5 | 69 KB | 15.0 s | 16.1 s | 57 MB | 118 MB |
| `batch-prn-4ch-100ms` | 3 recordings | 3 | 2.7 MB | 27.5 s | 29.2 s | 69 MB | 157 MB |

## The plots, by eye

Every run's two images, side by side, were published for comparing by eye. They match in everything the data
decides. The differences:

- **The Qt batch's legend rows overlap** (above): the Qt application's defect, in its batch export only.
- **Avalonia draws the legend's scroll bar into the image** when the legend scrolls (a recording with dozens of
  receiver channels); the Qt export has none. US6.0 says an image is what the chart shows, and the Avalonia chart shows
  the scroll bar, so this is the newer application keeping the story.
- **Avalonia's plot area is a little taller** (about 13 px of 900): its bottom margin is smaller. The ticks and labels
  fall in the same places relative to the data.

**One trap, in the Qt harness, not in either application:** the first Qt images were exported the moment a run
finished, with no turn of the event loop, and drew the legend's rows where they had been before the last rebuild (on
PRN-AGC, CH-04 drawn over CH-01). A user exports long after the run, so the harness let the layout settle before each
single run's export and the images were made again. The batch was left as it is, because there the Qt application
itself exports straight after the rebuild.

## Running it again

```
powershell -ExecutionPolicy Bypass -File scripts\run_soak.ps1 [-NoBuild] [-Only <run>,...] [-Out <folder>]
```

Each run writes its CSV, its image and `result.json` to `<Out>\<run>\` (the temporary folder's `tmdq-soak` by default).
Run it after any change to processing, the plot's series or the CSV writer. A run that no longer exports the Qt
application's bytes fails with both lengths and hashes.

## The mutation check

The soak's code is the Avalonia half's configurator and its comparison, so each mutant broke one thing it hands on and
was run against the run that should catch it, in a worktree: the sample period, Derandomize, Legacy Chapter 10, the
calibration applied, its profiles and its clip, the receiver parameters file, the polarity, the voltage range, the
scale, the number of receivers, the frame length, the mode, the sync mask, the typed data rate, the batch's reuse of
the template's looks, and OK refused in Extract Calibration. **17 mutants: 16 caught** (two rewritten first, since the
compiler refused them as written) and **1 equivalent:** the number of receivers, which only matters without a
receiver parameters file, and every Receiver SNR run names one.

**One survivor was a gap in the soak, now closed:** the typed data rate. The run typed 1.0 Mbps on a channel whose
TMATS rate is 1 Mbps, so it changed nothing. It now types 1.25 Mbps, which changes the export, and both applications
were run on it again and agree.

The comparison itself was checked by giving it a manifest that is wrong: a wrong hash, a file the Qt application did
not write, no Qt result, and a recording that is not there each fail the run, and the true manifest passes.
