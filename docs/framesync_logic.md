# Frame Sync Lock — Processing Logic & Design Notes

## Objective

Calculate per-stream frame sync lock statistics over a decoded PCM telemetry bitstream. The sync pattern, sync mask, and frame length are user-supplied via `StreamConfig`. Ch10 packet parsing and PCM payload extraction are handled upstream by the `irig106` library and are out of scope here.

This document describes both the **current implementation** (what the code does today) and **proposed optimizations** (speed/robustness improvements not yet implemented). Each section is labeled accordingly.

---

## 1. Derandomization

**Current behavior:** If `is_randomized` is true, the entire PCM packet payload is derandomized in-place **before** any sync searching begins. The LFSR state is preserved across packets so the pseudorandom sequence remains continuous at packet seams.

The RNRZ-L LFSR uses a 15-bit shift register with taps at positions 14 and 13 (2¹⁵−1 period for PRN-15; PRN-11 uses an 11-bit variant). Each incoming bit clocks the register; the register output XOR'd with the input bit is the descrambled output. Because the LFSR is driven by the *received* bits, it self-synchronizes without requiring a separate sync-acquisition step on the scrambler itself.

**Consequence for sync scanning:** In this implementation, the sync word is searched in the **post-derandomization** bitstream — not in the raw stream. This differs from some hardware implementations where the sync word is excluded from randomization. Do not assume the raw bitstream contains a detectable static sync pattern on randomized streams.

```text
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

### Current Behavior — Acquire / Lock with off-phase rejection

The scanner is a single-pass bit-serial loop, but it distinguishes two phases by tracking a running in-phase sync count (`sync_count`). It still reads every bit and maintains a sliding `test_word`, but it no longer blindly reacts to every pattern match.

**Acquire phase** (`sync_count < min_syncs`): every sync-pattern match is honored. Each match resets the frame-bit counter (`minor_frame_bit_count`) and starts collecting a candidate frame. Matches that land exactly `bits_in_frame` apart are *in-phase* and increment `sync_count`.

**Lock phase** (`sync_count >= min_syncs`): the frame boundary is known, so the scanner only honors a sync match that lands exactly `bits_in_frame` after the previous one (`minor_frame_bit_count == bits_in_frame`). Any *off-phase* match — a false positive produced by data that coincidentally equals the sync pattern, which is common on PRN/randomized streams — is tallied in `total_syncs_found` for diagnostics but otherwise **ignored**: the frame-collection state is left completely intact.

**Lock loss**: if, while locked, a sync match arrives *after* the expected boundary (`minor_frame_bit_count > bits_in_frame`), the genuine sync at the boundary was missed. The scanner drops lock (`sync_count = 0`) and re-enters the acquire phase, where it will re-confirm `min_syncs` in-phase syncs before locking again.

**Why this fixed the spiky lock:** before off-phase rejection, every false-positive match inside the pseudo-random data words reset the frame collector mid-frame. The real sync that followed then arrived with an incomplete frame (`save_data == 1`), so that frame was discarded — producing the characteristic ~98–100% oscillation on otherwise clean PRN streams. Holding the collector to the true frame grid while locked removes that self-inflicted frame loss.

### Target: Flywheel State Machine

Off-phase rejection above is the first piece of a flywheel already in place. The remaining value of the full state machine below is the **multi-frame CHECK confirmation** before declaring lock and, most importantly, the **data-word skip** in LOCK state (§5) — neither of which is implemented yet; the current loop still reads every bit even when locked.

The proposed optimization replaces the flat bit-serial loop with a three-state machine that avoids scanning data words once lock is established.

```text
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

**Implemented: single reader + per-stream queues.** A dedicated `Ch10PacketReader` thread opened the file once, read packets sequentially, and routed each packet to the appropriate stream's queue by channel ID lookup. Worker threads (one `FrameProcessor` per stream) consumed from their queues concurrently. This replaced the earlier design where each `FrameProcessor` opened the file independently and re-read it end-to-end — which on a 4-stream 24 GB file meant 96 GB of total file I/O (4× what was needed).

```text
[ Ch10PacketReader Thread ]   — reads the file once (24 GB total I/O)
    routes by channel ID → per-stream bounded PacketQueue
         |            |            |            |
  [ Stream 0 ]  [ Stream 1 ]  [ Stream 2 ]  [ Stream 3 ]
   worker thread  worker thread  worker thread  worker thread
```

Design points as built:

- **Bounded queues** (64 packets per stream): the reader blocked when a queue was full, preventing the whole file from being buffered in RAM.
- **Sentinel packet**: the reader posted an empty end-of-stream marker to each queue when the file was exhausted so workers knew when to stop.
- **TMATS parsed once** by the reader in `prepare()`, which also resolved each stream's PCM attributes (`Set_Attributes_Ext_PcmF1`) into a `ResolvedPcmAttrs` struct before any worker started — so workers never needed the file handle or the irig106 attribute structs.
- **Time reference**: the reader pre-converted each PCM packet's base time to absolute seconds (`enI106_RelInt2IrigTime`, per packet, capturing the current time-sync state) and bundled it into the dispatched `PacketItem`. Workers interpolated per-frame time linearly from that anchor without a file handle.
- **Progress tracking** moved from the per-FrameProcessor file position to the reader's file position, since only the reader held the handle.
- **Cancellation**: cancel aborted the reader and all workers and closed every queue, so no thread could deadlock on a blocking enqueue/dequeue.

**Qt signal emission** was already windowed correctly: `recordTimeSample()` emits once per output window (at 10 Hz, that is one emission per ~970 frames at 20 Mbps / 2047 bits per frame). No batching change was needed there.

---

## 6. Lock Statistics

**Lock percentage definition (current implementation):**

```text
lock_pct = (frames_locked_in_window / expected_frames_in_window) × 100
```

The denominator is computed from the **actual bit span traversed**, not a theoretical time-based estimate. As each locked frame is recorded, the scanner captures the global bit offset of the first frame (`first_frame_bit_in_window`) and continuously updates the last frame (`last_frame_bit_in_window`) in the current averaging window. When the window closes:

```text
bit_span           = last_frame_bit_in_window − next_expected_frame_bit + bits_in_frame
expected_in_window = bit_span / bits_in_frame
lock_pct           = (n_samples / expected_in_window) × 100        // capped at 100%
next_expected_frame_bit = last_frame_bit_in_window + bits_in_frame  // carried to next window
```

`next_expected_frame_bit` carries across windows so there is neither a gap nor a double-count at window seams: one window's expected-frame denominator begins exactly where the previous window's last frame ended.

**Why bit-span instead of `time × bit_rate`:** the lock percentage is now a pure bit-domain quantity — locked frames divided by the number of `bits_in_frame`-sized frame slots that physically fit in the bitstream span actually covered. **Time is used only to delimit the averaging window** (`sample_rate_hz`); it no longer enters the numerator or denominator. This removes the lock figure's dependency on an accurate TMATS / `dataRateMbps` bit rate — the value is correct even if the configured rate is slightly off. Lock percentage can still nudge above 100% (e.g. a boundary frame counted twice across a seam); the output is capped at 100%.

**Per output window (set by `sample_rate_hz`):**

- Frames locked in window (`n_samples`)
- Expected frames from bit span (`bit_span / bits_in_frame`)
- Lock percentage (capped at 100%)

These map to `ProcessedStreamData` series entries emitted by `FrameProcessor`.

---

## 7. Known Limitations & Deliberate Tradeoffs

- **No bit-slip tolerance in LOCK state.** The current implementation (and the proposed flywheel) jumps to a fixed predicted offset without a ±N-bit search window. Real channels can insert or drop bits; a small jitter window (±2–4 bits) around the predicted offset would improve robustness on degraded links, at the cost of some complexity and a small number of additional bit reads per frame.
- **Derandomization is all-or-nothing per stream.** If only some frames in a stream are randomized (e.g. due to a link anomaly), the stateful LFSR will produce incorrect descrambled data and sync will be lost until the LFSR re-synchronizes naturally.
