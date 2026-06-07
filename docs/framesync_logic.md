# Frame Sync Lock — Processing Logic & Design Notes

## Objective

Calculate per-stream frame sync lock statistics over a decoded PCM telemetry bitstream. The sync pattern, sync mask, and frame length are user-supplied via `StreamConfig`. Ch10 packet parsing and PCM payload extraction are handled upstream by the `irig106` library and are out of scope here.

This document describes both the **current implementation** (what the code does today) and **proposed optimizations** (speed/robustness improvements not yet implemented). Each section is labeled accordingly.

---

## 1. Derandomization

**Current behavior:** If `is_randomized` is true, the entire PCM packet payload is derandomized in-place **before** any sync searching begins. The LFSR state is preserved across packets so the pseudorandom sequence remains continuous at packet seams.

The RNRZ-L LFSR uses a 15-bit shift register with taps at positions 14 and 13 (2¹⁵−1 period for PRN-15; PRN-11 uses an 11-bit variant). Each incoming bit clocks the register; the register output XOR'd with the input bit is the descrambled output. Because the LFSR is driven by the *received* bits, it self-synchronizes without requiring a separate sync-acquisition step on the scrambler itself.

**Consequence for sync scanning:** In this implementation, the sync word is searched in the **post-derandomization** bitstream — not in the raw stream. This differs from some hardware implementations where the sync word is excluded from randomization. Do not assume the raw bitstream contains a detectable static sync pattern on randomized streams.

```
For each packet:
  1. derandomizeBitstream(raw_data, packet_bits, lfsr_state)   // in-place, stateful LFSR
  2. scan derandomized bits for frame sync pattern
```

---

## 2. Sync Pattern Matching

### Current: Exact Match

The current implementation uses **exact match** against the user-supplied pattern:

```cpp
if (bits_loaded >= sync_pat_len &&
    (test_word & sync_mask) == sync_pat)
    // sync found
```

`sync_mask` defaults to all-ones (all bits must match) but supports don't-care bits. There is no bit-error tolerance — a single flipped bit in the sync word causes a miss.

### Proposed: BER-Tolerant Matching

To improve robustness on noisy channels, exact match can be replaced with a Hamming-distance test using `std::popcount`:

```cpp
bool matches(uint64_t candidate, uint64_t pattern, uint64_t mask, int max_errors) {
    return std::popcount((candidate ^ pattern) & mask) <= max_errors;
}
```

**Important:** Use separate thresholds for acquisition and maintenance. A loose threshold during SEARCH increases false lock probability; a strict threshold during LOCK drops valid frames too aggressively.

| Phase | Suggested `max_errors` | Rationale |
|---|---|---|
| SEARCH / CHECK (acquire) | 0–1 | Avoid false lock on noise |
| LOCK (maintain) | 2 | Tolerate occasional channel errors |

---

## 3. Frame State Machine

### Current Behavior

The current code is a single-pass bit-serial scanner with no distinct SEARCH/CHECK/LOCK phases. It reads every bit, maintains a sliding `test_word`, and records a frame whenever the sync word is found exactly `bits_in_frame` bits after the previous sync. Lock percentage is computed per output window from the count of successfully extracted frames versus the theoretically expected count.

### Target: Flywheel State Machine

The proposed optimization replaces the flat bit-serial loop with a three-state machine that avoids scanning data words once lock is established.

```
[ SEARCH ] --(sync found)--> [ CHECK ] --(N consecutive confirmations)--> [ LOCK ]
     ^                            |                                            |
     |-----(no confirm in 2×FL)---+-----------(M consecutive misses)----------+
```
*(FL = frame length in bits)*

**SEARCH**
- Perform the full bit scan over every incoming bit.
- On any match within the acquire BER threshold: record the candidate bit offset; transition to CHECK.

**CHECK**
- Advance by exactly `bitsInMinorFrame` and look for the sync word at the predicted offset.
- Repeat for `check_confirmations` consecutive frames. If all confirm: transition to LOCK.
- If any frame fails to confirm within a tolerance window (see §5 Jitter): revert to SEARCH.
- Abort the CHECK attempt if no confirmation arrives within 2 × `bitsInMinorFrame` bits scanned.

**LOCK**
- Jump directly to the predicted sync offset — do not scan data words (see §5 LOCK-State Skip).
- Apply the maintenance BER threshold.
- On match: record locked frame; reset miss counter.
- On miss: increment consecutive-miss counter. At `lock_miss_threshold` consecutive misses, drop to SEARCH.

### Parameters

| Parameter | Meaning | Suggested default |
|---|---|---|
| `ber_acquire` | Max flipped bits for SEARCH/CHECK match | 0 |
| `ber_maintain` | Max flipped bits for LOCK match | 2 |
| `check_confirmations` | Consecutive frames confirmed before LOCK | 2 |
| `lock_miss_threshold` M | Consecutive missed syncs before dropping lock | 4 |

---

## 4. PCM Bitstream Continuity & Packet Boundaries

The irig106 library delivers PCM payload data one Ch10 packet at a time, but the PCM bitstream is **continuous across packet boundaries**. A frame sync word can straddle the seam between two consecutive packets.

**Current behavior:** The bit-serial scanner already handles this correctly. The shift-register state (`test_word`, `bits_loaded`, `minor_frame_bit_count`) is declared *outside* the packet loop and is never reset between packets — every bit of every packet shifts into the same continuous register. A sync word straddling a seam is therefore detected normally, with no special handling required.

**Why the carry buffer matters for the optimization:** The carry buffer below is **only** needed if the bit-serial scan is replaced by the proposed word-parallel SWAR scan (§5). A word-parallel scan consumes whole 32-bit words and would lose the trailing partial word of each packet plus any sync straddling the seam. The carry buffer reconstructs that boundary window. If you keep the bit-serial loop, you do **not** need a carry buffer.

### Carry Buffer (required only for the SWAR optimization)

At the end of each packet, save the final `sync_pattern_length - 1` bits into a carry buffer. At the start of the next packet, prepend the carry to form a complete initial scan window before advancing into the packet body.

**32-bit sync pattern (e.g. `0xA345CA5C`):** carry is 31 bits, fits in a `uint32_t`. The window at the seam is a `uint64_t`:

```cpp
uint64_t window = ((uint64_t)carry << 33) | ((uint64_t)first_word_of_new_packet << 1);
// scan the low 32 bits of the window at each of the 31 carry-bit offsets
```

**64-bit sync pattern:** carry is 63 bits. Forming the window requires 127 bits, which overflows `uint64_t`. Use `__uint128_t` (GCC/Clang) or two adjacent `uint64_t` values:

```cpp
uint64_t window_hi = carry;
uint64_t window_lo = first_64_bits_of_new_packet;
// extract a 64-bit candidate at shift s: (window_hi << s) | (window_lo >> (64 - s))
// for s in 1..63
```

The derandomizer LFSR state (`lfsr_state`) must also persist across packet boundaries so the carry bits and the new packet bits share the same LFSR phase — the current implementation already does this correctly.

### End-of-Stream Flush

When the last PCM packet for a channel is consumed, run the carry buffer through the state machine with padding zeros. This ensures partial frames at the end of file are counted as misses rather than ignored, giving correct final lock statistics.

---

## 5. Performance Optimizations

### SWAR Bit-Parallel Scanning (SEARCH State)

**Current situation:** The hot loop reads one bit at a time (bit-serial). For a 2047-bit frame at 5 Mbps the scanner touches ~312 bytes per frame, but in SEARCH state it does so through a per-bit conditional branch, which is branch-predictor-hostile and throughput-limited.

**Proposed:** Replace the bit-serial SEARCH scan with SWAR (SIMD Within A Register) 32-bit parallel matching. Load two adjacent byte-swapped 32-bit words, form a 64-bit sliding window, and test all 32 bit-offsets per iteration:

```cpp
// Byte-swap to convert little-endian memory order to MSB-first PCM bit order.
uint32_t w0 = __builtin_bswap32(words[i]);
uint32_t w1 = __builtin_bswap32(words[i + 1]);
uint64_t window = ((uint64_t)w0 << 32) | w1;

for (int shift = 0; shift < 32; ++shift) {
    uint32_t candidate = (uint32_t)(window >> (32 - shift));
    if (matches(candidate, sync_pattern, sync_mask, ber_acquire))
        // sync candidate at bit (i * 32 + shift)
}
// Guard: ensure i+1 < word_count to avoid out-of-bounds read on the last word.
// Handle the final partial word via the carry buffer mechanism (§4).
```

For 64-bit patterns, use a `__uint128_t` window and slide a 64-bit mask across it.

**Byte order note:** PCM is MSB-first; x86 is little-endian. `__builtin_bswap32` is required before any bitwise comparison — without it every comparison silently fails.

### Buffer Alignment

The irig106 payload pointer is `uint8_t*` with no alignment guarantee. Do not `reinterpret_cast` it to `uint32_t*` or `uint64_t*` — that is undefined behavior and may fault on strict-alignment platforms. Instead, read individual words via `memcpy` into a local `uint32_t` (the compiler reduces this to a single unaligned load on x86):

```cpp
uint32_t w;
memcpy(&w, &raw_bytes[i * 4], 4);
w = __builtin_bswap32(w);
```

### LOCK-State Data Skip

**This is the largest single throughput gain available.** In LOCK state the sync position is known precisely. The scanner should:

1. Compute the byte/bit offset of the predicted sync word within the current packet.
2. Load and check only those `sync_pattern_length` bits.
3. Advance the file pointer by `bitsInMinorFrame` to the next predicted sync position.

Data words between sync positions are **not read or touched at all** for Frame Sync Lock statistics. For Receiver SNR mode, data words are read after lock is confirmed as a separate pass.

The current bit-serial loop reads every bit even when locked — the skip optimization reduces LOCK-state data touching from `bitsInMinorFrame` bits per frame to `sync_pattern_length` bits per frame. For a 2047-bit frame with a 32-bit sync word that is a ~64× reduction in bytes read per locked frame.

### Prefetch (SEARCH State Only)

In SEARCH state, insert a read prefetch hint one cache line (16 × `uint32_t`) ahead of the current scan position to hide memory latency:

```cpp
__builtin_prefetch(&words[i + 16], 0 /*read*/, 0 /*no temporal locality*/);
```

Do not prefetch in LOCK state — the access pattern is a large stride (jumping by `bitsInMinorFrame`), which the hardware prefetcher cannot predict and software hints will not help.

### Per-Stream Parallelism & I/O Architecture

**Current situation:** `ProcessingCoordinator` runs streams one at a time. Each `FrameProcessor` opens the Ch10 file independently and reads the entire file from start to finish to filter for its channel ID. With four streams on a 24 GB file, that is 96 GB of total file I/O — 4× what is needed.

**Proposed: single reader + per-stream queues.** A dedicated reader thread opens the file once, reads packets sequentially, and routes each packet to the appropriate stream's queue by channel ID lookup. Four worker threads consume from their queues concurrently.

```
[ File Reader Thread ]   — reads the file once (24 GB total I/O)
    routes by channel ID → per-stream bounded PacketQueue
         |            |            |            |
  [ Stream 0 ]  [ Stream 1 ]  [ Stream 2 ]  [ Stream 3 ]
   worker thread  worker thread  worker thread  worker thread
```

Key design constraints:
- **Bounded queues** (e.g., 64 packets per stream): the reader blocks when a queue is full, preventing the whole file from being buffered in RAM.
- **Sentinel packet**: the reader posts an empty end-of-stream marker to each queue when the file is exhausted so workers know when to stop.
- **TMATS parsed once** by the reader and distributed to workers before the packet loop starts.
- **Time reference**: time packets are decoded by the reader and bundled into the dispatched packet struct so each worker has the correct timestamp without needing its own file handle.
- **Progress tracking** moves from the per-FrameProcessor file position to the reader's file position, since only the reader has the handle.

**Qt signal emission** is already windowed correctly: `recordTimeSample()` emits once per output window (at 10 Hz, that is one emission per ~970 frames at 20 Mbps / 2047 bits per frame). No batching change is needed there.

---

## 6. Lock Statistics

**Lock percentage definition (current implementation):**

```
lock_pct = (frames_locked_in_window / expected_frames_in_window) × 100
```

Where `expected_frames_in_window` is the *theoretical* frame count derived from the data bit rate and frame length:

```
expected_frames_in_window = (bit_rate_bps / bits_in_frame) × sample_period_seconds
```

The bit rate comes from TMATS unless the user provides an explicit override via `StreamConfig::dataRateMbps`. Lock percentage can slightly exceed 100% due to false syncs; the output is capped at 100%.

**Per output window (set by `sample_rate_hz`):**

* Frames locked in window (`n_samples`)
* Expected frames in window (`expected_frames_per_window`)
* Lock percentage (capped at 100%)

These map to `ProcessedStreamData` series entries emitted by `FrameProcessor`.

---

## 7. Known Limitations & Deliberate Tradeoffs

- **No bit-slip tolerance in LOCK state.** The current implementation (and the proposed flywheel) jumps to a fixed predicted offset without a ±N-bit search window. Real channels can insert or drop bits; a small jitter window (±2–4 bits) around the predicted offset would improve robustness on degraded links, at the cost of some complexity and a small number of additional bit reads per frame.
- **No cross-frame BER tracking.** The current lock statistic is binary per frame (locked or not). A richer implementation could track cumulative bit errors within locked frames to characterize channel quality beyond lock percentage.
- **Derandomization is all-or-nothing per stream.** If only some frames in a stream are randomized (e.g. due to a link anomaly), the stateful LFSR will produce incorrect descrambled data and sync will be lost until the LFSR re-synchronizes naturally.
