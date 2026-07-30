---
name: tm-code-review
description: >-
  Review changes to the tmDataQualityAnalyzer codebase for correctness, security, and quality, with
  the project-specific domain invariants a generic reviewer cannot know. Use this WHENEVER you are
  reviewing a diff, a commit, or a file in this repo, or before committing/opening a PR — including
  when the user says "review this", "check my changes", "is this safe?", "did I break an invariant?",
  or asks about code quality of telemetry processing, frame-sync, SNR calibration, the C/irig106
  interop, or the TOML config boundaries. It layers the frame-sync, calibration, threading, and
  protected-file rules of THIS project on top of a general correctness/security pass so review catches
  the silent domain bugs (mis-calibrated SNR, double-counted missed frames, widened TOML round-trips,
  edited third-party libs) that have actually bitten this project before.
---

# Project Code Review — tmDataQualityAnalyzer

This skill reviews changes the way a maintainer of *this* codebase would. Run a general correctness +
security + quality pass, then explicitly check the project invariants below — they are where real
bugs hide and where a generic reviewer is blind.

## How to run it

1. Get the diff under review. Default to the working tree / current branch vs `main`:
   `git diff main...HEAD` and `git status`. If the user named files or a PR, scope to those.
2. Read each changed file with enough surrounding context to judge it — not just the hunk.
3. For broader, adversarially-verified correctness coverage you can also invoke the native
   `/code-review` skill; this skill is the project-aware layer that complements it. It supersedes the
   ad-hoc generic review prompts this project used before, which targeted web concerns like SQL/XSS
   that don't apply to a Qt desktop app.
4. Report findings grouped by severity (Critical / High / Medium / Low). For each: **file:line**,
   **what's wrong**, **why it matters here**, **concrete fix**. List real issues only — do not pad.

## Protected files — a hard stop

`lib/irig106/**` is third-party and MUST NOT be modified. If the diff
touches them, that is a Critical finding on its own: the fix belongs in the wrapping app code
(`chapter10reader.cpp`, `ch10packetreader.cpp`, `frameprocessor.cpp`, `plotwidget.cpp`). Flag it and
stop endorsing the change until it moves.

## Domain invariants to verify (the high-value checks)

### SNR / calibration path (`frameprocessor.cpp`, `calibrationextractor.cpp`, `stepdetector.cpp`)
- **Average raw first, then calibrate.** Within an output window the code accumulates *raw counts*
  (`param->sample_sum += raw_value`) and applies calibration once to the windowed mean in
  `recordTimeSample()`. Reject any change that calibrates per-sample then averages —
  `mean(interpolate(raw))` biases every non-linear plateau off its true step. See `docs/SNR_logic.md` §3.
- **The extractor must mirror the main run exactly.** A calibration extraction run must use the same
  word map and every raw-count-affecting flag as the real run (`isRandomized`, `isInverted`, sync
  pattern/mask, frame geometry) — only `slope=1.0 / scale=0.0 / profile cleared` and the data-rate
  clock differ. A divergence silently mis-keys or mis-scales every profile. This has bitten the
  project twice (word-map off-by-one; missing `isInverted`). See `docs/SNR_logic.md` §4.2.
- **Out-of-range interpolation clamps, never extrapolates** — readings past the calibrated range peg
  to the nearest end-step dB (`interpolateCalibration()`), so an out-of-cal receiver reads the
  ceiling/floor instead of diverging per channel.
- **Empty window emits 0.0**, never a divide-by-zero or a calibrated meaningless average
  (`n_samples == 0`).

### Frame-sync / lock path (`frameprocessor.cpp`)
- **Lock % is a bit-span quantity**, not `time × bitrate`: `valid_bits_in_window /
  total_bits_in_window`. Time only delimits the averaging window. Don't reintroduce a data-rate
  dependency into the numerator/denominator. See `docs/framesync_logic.md` §6.
- **Off-phase sync matches are rejected while locked** — a sync match that isn't at the exact frame
  boundary is counted for diagnostics but must leave frame-collection state intact, or PRN data
  re-introduces the spiky 98–100% lock artifact.
- **Missed-frame accounting must not double-count.** Windows that contain real bits already counted
  their misses bit-by-bit (the rollover at `minor_frame_bit_count > bits_in_frame`); only a genuine
  recording gap (no bits at all) extrapolates from the expected data rate. Watch any edit near
  `frameprocessor.cpp:307-349` — this guard was added to fix a real bias on long-frame streams.
- **Missed frames are monotonic** and counted only within `[start, stop]`. They are a loss-of-lock
  event count, NOT a bit-level/BER metric.

### TOML config boundaries (`tomlconfighelper.cpp`, `streamsubdialogs.h`)
- **Frame-sync Load/Save round-trips ONLY** frame sync pattern, sync mask, and words/frame.
  `Randomized`, `Data Rate`, and `Sample Rate` are per-session operator inputs and are intentionally
  excluded (the separator line in the setup dialog = this boundary). Do not widen
  `loadFrameSyncToml`/`saveFrameSyncToml` past it. See US1.0 scope in `docs/CLAUDE.md`.
- Calibration profiles are **session-only** and never serialized to disk.

### Threading / lifecycle (`processingcoordinator.cpp`, `ch10packetreader.cpp`, `packetqueue.h`)
- One `Ch10PacketReader` thread reads the file **once** and routes packets to bounded per-stream
  queues; one `FrameProcessor` worker per stream consumes concurrently. Don't reintroduce per-worker
  file opens (the old 4× I/O bug).
- Abort must stay cooperative and deadlock-free: workers/reader check `m_abort_requested`
  (`memory_order_relaxed`) and queues must be closed so no thread blocks forever on enqueue/dequeue.
- `PacketItem.payload` is a COW `QByteArray`; a worker that mutates bytes (swap/invert/derandomize)
  must operate on its own detached copy. Verify any new mutate-in-place path detaches first.

### C / irig106 interop (`ch10packetreader.cpp`, `chapter10reader.cpp`)
- Validate at the trust boundary: bound track numbers (negative `atoi` → skip), check
  `ulDataLen > data_offset` before pointer arithmetic, and cap allocations via
  `ensureBufferCapacity` (rejects > `kMaxPacketBufferSize`, catches `bad_alloc`). New parsing code
  must keep these guards.
- Every `calloc`/`malloc` from the C layer needs a matching free path (`freeChanInfoTable`), and
  `FreeOutputBuffers_PcmF1` before `free` for PCM attributes. Flag any leak or double-free.
- No C++ exceptions may propagate across the C boundary.

## General pass (still do this)
Correctness/edge cases (off-by-one, overflow in the bit math — note `uint64_t` shift registers and
`UINT64_MAX` sentinels), input validation, Qt parent/ownership and `delete` correctness, naming and
convention adherence (`docs/CLAUDE.md` "Coding Conventions"), and DRY against the single-source
helpers (`FrameSetup::buildDefaultReceiverMap`, `channelPrefix`, `receiverParameterName`).

## After review
If asked to fix, apply the changes and re-verify with the **build-and-test** skill (a green full
suite is the bar). Security-sensitive interop changes also warrant the native `/security-review`.
