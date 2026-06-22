# Receiver SNR Calibration — Processing Logic & Design Notes

## Objective

Convert raw receiver AGC sample counts (16-bit integers, 0–65535) decommutated
from a PCM telemetry stream into calibrated SNR values in dB, per receiver
channel, averaged over a user-selected time window. Two calibration models are
supported:

- **Linear** (default, US1.0/US3.0/US3.1): a slope/offset model derived from the
  receiver's voltage range, polarity, and dB/V scale.
- **Non-linear step calibration** (optional, US3.2): a per-channel piecewise-linear
  raw→dB profile extracted from a dedicated calibration Chapter 10 file. Grooms
  out receiver non-linearities the linear model cannot capture.

Frame decommutation (sync acquisition/lock, word extraction) is shared with the
Frame Sync Lock path and is documented separately in
[framesync_logic.md](framesync_logic.md). This document covers only what happens
to a decommutated word once it has been pulled from the minor frame.

---

## 1. The Raw Sample

For each receiver channel, the word map (`FrameSetup`) records which **word index**
within the minor frame carries that channel's AGC sample. Once a frame is locked
and extracted, the scanner reads:

```cpp
int64_t raw_value = frame_words[param->word] & word_mask;   // 0 .. kMaxRawSampleValue (0xFFFF)
```

This raw count is the common input to **both** calibration models. The only
difference between SNR-extraction, linear-SNR, and non-linear-SNR runs is what
slope/offset/profile is attached to each `ParameterInfo`, and when the conversion
is applied.

---

## 2. Linear Calibration (default model)

Built in `MainViewModel::buildStreamJob()` from three operator inputs: a **voltage
range** (`slope_idx`), a **polarity** (`polarity_idx`), and a **scale** in dB/V
(`scale_dB_per_V`).

```cpp
voltage_lower = kSlopeVoltageLower[slope_idx] * scale_dB_per_V;   // ranges: ±10V, ±5V, 0–10V, 0–5V
voltage_upper = kSlopeVoltageUpper[slope_idx] * scale_dB_per_V;

param->slope = (voltage_upper - voltage_lower) / kMaxRawSampleValue;   // dB per raw count
```

Polarity selects how the zero-dB reference maps onto the raw range:

```cpp
if (negative_polarity) {            // polarity_idx == 1
    param->slope *= -1;
    param->scale = -voltage_upper / (voltage_upper - voltage_lower) * kMaxRawSampleValue;
} else {
    param->scale =  voltage_lower / (voltage_upper - voltage_lower) * kMaxRawSampleValue;
}
```

The conversion itself is **offset-then-scale** (note `scale` is an offset in raw
counts, `slope` is the dB-per-count gain — the field names predate this doc):

```
dB = (raw + param->scale) * param->slope
```

Every channel gets a linear model. A channel only departs from it if a **valid**
non-linear profile is attached on top (§4).

---

## 3. Averaging Order — Raw First, Then Calibrate

**This is the single most important rule in the SNR path.** Within each output
window the scanner accumulates the **raw counts**, and calibration (linear *or*
non-linear) is applied exactly once, to the windowed **average**, when the window
closes.

In the per-frame extraction loop (`FrameProcessor::process()`):

```cpp
param->sample_sum += static_cast<double>(raw_value);   // RAW counts, not calibrated dB
```

In `recordTimeSample()` when the window closes:

```cpp
const double mean_raw = param->sample_sum / n_samples;
value = param->profile.valid
    ? interpolateCalibration(mean_raw, param->profile)        // non-linear
    : (mean_raw + param->scale) * param->slope;               // linear
```

**Why averaging must precede calibration:** the non-linear profile maps an
*averaged-raw* count to a true dB step. Applying the profile per-sample and then
averaging — `mean(interpolate(raw))` — biases each plateau off its true step
value, because interpolation is non-linear across a noisy raw spread. Averaging
first (`interpolate(mean(raw))`) lands on the step. For the linear model the two
orders are algebraically identical, so doing it this way is correct for both and
keeps a single code path.

**Empty windows:** `n_samples == 0` (a recording gap, or an SNR window entirely
out of lock) emits `0.0` rather than calibrating a meaningless zero average —
never a divide-by-zero or NaN.

---

## 4. Non-Linear Step Calibration (US3.2)

A non-linear `CalibrationProfile` is an optional, **session-only** per-channel
correction. It is never written to disk; it lives only for the run in which it
was extracted and is keyed by word index.

### 4.1 Lifecycle

```
[ Calibration CH10 file ]   [ Step-config TOML ]
            │                        │
            ▼                        ▼
     CalibrationExtractor  (raw extraction run: unit slope, zero offset)
            │  per-channel raw-count series (one value per 10 ms window)
            ▼
        StepDetector.detect()   ── per channel ──►  CalibrationProfile
            │
            ▼
   StreamConfig.calibrationByWord  (word index → profile)
            │
            ▼
   MainViewModel::buildStreamJob()  attaches valid profiles onto ParameterInfo
            │
            ▼
   FrameProcessor main run  ── interpolateCalibration(mean_raw, profile) ──► dB
```

### 4.2 Extraction Run (`CalibrationExtractor`)

The extractor drives the **same** reader + FrameProcessor pipeline used for a real
run, over the calibration file, with two deliberate configuration choices:

- **Unit slope / zero offset** on every parameter (`slope = 1.0`, `scale = 0.0`,
  `profile` cleared) so the FrameProcessor emits **raw counts** (averaged per 10 ms
  window) instead of dB. See `buildFrameSetup()`.
- **Data-rate clock** (`useDataRateClock = true`): step timing is measured over
  elapsed stream time from zero, independent of the cal file's IRIG time (which may
  be large or absent and would otherwise derail the per-window sampling).

> **Critical invariant:** the extraction run's word map and every raw-count-affecting
> flag (`isRandomized`, `isInverted`, sync pattern/mask, frame geometry) must match
> the real run's. If the extractor decommutates differently from the main run, each
> profile is keyed to a word — or built from a raw range — that the main run never
> reproduces, and calibration silently misattaches or extrapolates. This has bitten
> us twice: a word-map off-by-one in the extractor mis-keyed every profile, and a
> missing `isInverted` flag produced offset steps despite profiles attaching cleanly.

The extraction sample period is fixed at `kExtractSamplePeriodSec` (10 ms / 100 Hz).

### 4.3 Step-Config TOML

An array-of-tables listing the **true dB value** injected during each step, in the
order the technician runs them:

```toml
[[Step]]
db = 0.0

[[Step]]
db = 3.0
```

Parsed by `StepDetector::parseStepConfig()`. There is **no dwell/duration** per
step — the detector confirms a step by requiring the level to hold steady for a
fixed window (`kStepConfirmSeconds`), so technicians need not know or configure how
long each step lasts. An empty file (no `[[Step]]` entries) fails the parse.

### 4.4 Plateau Detection (`StepDetector::detect()`)

Operating on the per-channel raw-count series:

1. **Derivative** — sample-to-sample difference of the raw series.
2. **Adaptive edge threshold** — `max(kEdgeSigmaMultiple × σ, kMinEdgeRawCounts)`,
   where σ is a robust (MAD-based, ×1.4826) spread of the derivative. In-plateau
   noise sets the baseline; real transitions are large outliers above it. The
   absolute floor keeps noise-free flat data from producing a zero threshold.
3. **Mark transitions** — any `|derivative|` over threshold marks both endpoints
   unstable, so plateaus exclude the moving region.
4. **Collect stable runs** — maximal runs of stable samples that last at least
   `confirm_samples = max(kMinConfirmSamples, ceil(kStepConfirmSeconds / period))`.
   Shorter runs are transients/partial jumps and are dropped.
5. **Validity gate** — need at least as many confirmed plateaus as expected steps;
   otherwise the profile stays invalid → linear fallback.
6. **Pairing** — the first N confirmed plateaus (time order) pair positionally with
   the N steps. Extra plateaus (e.g. a technician running the sequence twice) are
   discarded, not averaged in. Each point's `rawAvg` is the mean of the
   `confirm_samples` at the **end** of the plateau (any post-jump settling has died
   out by then), paired with that step's `trueDb`.
7. **Sort** ascending by `rawAvg` so `interpolateCalibration()` can binary-walk it.
8. **Monotonicity guard** — a real receiver response is monotonic in one direction
   (dB strictly rises or falls with raw count); it does not zigzag. If sorting by
   `rawAvg` does *not* also yield a monotonic dB sequence, the positional pairing
   aligned the wrong plateaus with the wrong steps — the profile is rejected (→ linear
   fallback) rather than handing the interpolator two near-identical raw values
   pinned to opposite dB extremes (which would produce a near-vertical, wildly wrong
   slope).

Each channel is evaluated independently; success on one says nothing about another.

### 4.5 Interpolation (`interpolateCalibration()`)

Given `mean_raw` and a valid profile (≥ 2 points, sorted ascending by `rawAvg`):

- **Within range** — piecewise-linear interpolation between the two bracketing points.
- **Below the first / above the last point** — *clamped* to that endpoint's dB (the
  response flatlines at the calibrated limits, it does NOT extrapolate). A receiver
  driven outside its calibrated range — e.g. badly out of calibration, where the raw
  AGC count runs past the last step — would otherwise extrapolate to wildly divergent
  values that differ per channel and spread the plotted plateaus apart. Clamping pegs
  such readings at the nearest calibrated limit (0 dB or the top step) so an
  out-of-range value is bounded and obvious.
- **Coincident raw values** (`dr == 0`) — return the lower point's dB rather than
  divide by zero.

Degenerate guards: an empty profile returns the raw value unchanged; a single point
returns that point's dB constant. Neither occurs for a `valid` profile but both are
handled defensively.

### 4.6 Attachment and Fallback

In `buildStreamJob()`, profiles are matched onto the word map **by word index**:

```cpp
auto it = cfg.calibrationByWord.constFind(param->word);
if (it != cfg.calibrationByWord.constEnd() && it.value().valid)
    param->profile = it.value();          // else the linear slope/scale stays in force
```

A channel uses non-linear math **only** if a profile is found for its word *and* the
profile is `valid`. Every other channel — no profile, or a profile that failed
detection/monotonicity — silently uses the linear model from §2. The extractor's
summary message box reports how many channels calibrated vs. fell back.

Two diagnostic logs aid the common "calibration not applied / offset dB" failure:
the per-word profile points are dumped at attach time, and the main run logs the
actual raw range each calibrated channel saw vs. the profile's point range — if the
run's range sits outside the profile, `interpolateCalibration()` is extrapolating
and the plotted dB will be offset.

---

## 5. Tuning Constants (`CalibrationConstants`)

| Constant | Meaning | Value |
|---|---|---|
| `kExtractSamplePeriodSec` | Extraction-run window period | 0.01 s (100 Hz) |
| `kEdgeSigmaMultiple` | Edge threshold as a multiple of robust σ | 6.0 |
| `kMinEdgeRawCounts` | Absolute floor on the edge threshold | 2.0 counts |
| `kStepConfirmSeconds` | Min stable duration to confirm a plateau | 1.0 s |
| `kMinConfirmSamples` | Absolute floor on the confirmation window | 3 samples |

---

## 6. Known Limitations & Deliberate Tradeoffs

- **Profiles are session-only.** A `CalibrationProfile` is never persisted; reopening
  the data file requires re-running extraction. This is intentional — a profile is
  tied to the exact decommutation parameters of the run that built it.
- **Positional pairing assumes a clean step sequence.** Detection tolerates extra
  runs of the sequence (uses the first N) and rejects mis-aligned pairings via the
  monotonicity guard, but it cannot recover a profile from a partial/garbled run — it
  falls back to linear rather than guessing.
- **Extraction must mirror the main run exactly.** Any divergence in the word map or
  raw-count-affecting flags between the extractor and the main run misattaches or
  mis-scales calibration. This is the most common real-world failure and the reason
  for the two diagnostic logs in §4.6.
- **No per-step dwell/quality metric.** A plateau either confirms (held ≥
  `kStepConfirmSeconds`) or it doesn't; the profile carries no confidence weight per
  point.
