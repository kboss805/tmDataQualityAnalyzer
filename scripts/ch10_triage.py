#!/usr/bin/env python3
"""Triage a Chapter 10 recording that the app finds no frame syncs in.

Standalone: no Qt, no irig106, no build. Walks the .ch10 packet stream directly
and answers "why does this file not sync?" in three passes:

  1. Packet census + the gates `Ch10PacketReader::prepare()` applies — first
     packet must be TMATS, TMATS must carry a G-record and an R-record, and
     R-x\\TK1-n track numbers must be numeric (they index channel_info[] via
     atoi(), so non-numeric text silently collapses every channel to index 0).
  2. A 16-row transform matrix: {byte swap} x {invert} x {derandomize} x
     {LSB-first} — the four bit-level transforms the pipeline can apply. Only
     one combination should find the sync pattern; which one tells you what the
     file assumes. Two of those rows are the app's own behavior, marked in the
     output.
  3. Auto-discovery: which words actually recur at a constant bit period,
     ignoring the configured pattern entirely. This finds the real sync word
     and the real minor-frame length when the configured ones are wrong.

The transforms are deliberate mirrors of the shipping code, not
reimplementations: `swap_bytes` mirrors SwapBytes_PcmF1 (including its no-op on
odd lengths), `derandomize` mirrors FrameProcessor::derandomizeBitstream, and
`scan` mirrors the bit-serial MSB-first loop in FrameProcessor::scanBit. Keep
them in sync if that code changes, or the matrix stops being evidence.

Usage:

    py scripts/ch10_triage.py path/to/file.ch10 --pattern FE6B2840 --frame-bits 8000

Both --pattern and --frame-bits should be whatever the stream config had when
the run came back empty; the auto-discovery pass runs regardless and does not
trust either.
"""

from __future__ import annotations

import argparse
import struct
import sys
from collections import Counter, defaultdict

HDR_LEN = 24
PACKET_SYNC = 0xEB25
DT_TMATS, DT_TIME, DT_PCM = 0x01, 0x11, 0x09

DTYPE_NAMES = {
    0x00: "Computer/User-defined", 0x01: "TMATS", 0x02: "Recording events",
    0x03: "Recording index", 0x09: "PCM Format 1", 0x11: "Time Format 1",
    0x19: "MIL-STD-1553 F1", 0x21: "Analog F1", 0x29: "Discrete F1",
    0x30: "Message F1", 0x38: "ARINC 429", 0x40: "Video F0", 0x41: "Video F1",
    0x48: "Image F0", 0x50: "UART F0", 0x59: "IEEE-1394", 0x60: "Parallel",
    0x68: "Ethernet F0", 0x70: "TSPI/CTS",
}


def walk(path: str, limit: int | None = None):
    """Yield (offset, channel_id, data_type, flags, body) per Ch10 packet.

    Resyncs on 0xEB25 rather than trusting the length field, so a file with a
    corrupt packet header still yields everything after it.
    """
    with open(path, "rb") as handle:
        blob = handle.read()

    first = blob.find(b"\x25\xeb")
    if first < 0:
        print("  !! no 0xEB25 packet sync anywhere in this file", file=sys.stderr)
        return
    if first > 0:
        print(f"  !! {first} bytes precede the first 0xEB25 packet sync", file=sys.stderr)

    off, count = first, 0
    while off + HDR_LEN <= len(blob):
        sync, chan, packet_len, data_len = struct.unpack_from("<HHII", blob, off)
        if sync != PACKET_SYNC:
            nxt = blob.find(b"\x25\xeb", off + 2)
            if nxt < 0:
                break
            off = nxt
            continue
        flags, dtype = blob[off + 14], blob[off + 15]
        # Bit 7 of the packet flags marks a 12-byte secondary header.
        body_start = off + HDR_LEN + (12 if flags & 0x80 else 0)
        body = blob[body_start:off + packet_len] if packet_len else b""
        yield off, chan, dtype, flags, (body[:data_len] if data_len else body)
        if packet_len <= 0:
            break
        off += packet_len
        count += 1
        if limit and count >= limit:
            break


# ---------------------------------------------------------------------------
#                    TRANSFORMS — mirrors of the shipping code
# ---------------------------------------------------------------------------

def swap_bytes(data: bytes) -> bytes:
    """Mirror SwapBytes_PcmF1: swap adjacent byte pairs.

    Note the odd-length behavior: the C function returns I106_BUFFER_OVERRUN
    *before* touching the buffer, and frameprocessor.cpp ignores that return —
    so an odd-length payload is silently left unswapped. Reproduced here.
    """
    if len(data) & 1:
        return bytes(data)
    out = bytearray(data)
    out[0::2], out[1::2] = data[1::2], data[0::2]
    return bytes(out)


def invert(data: bytes) -> bytes:
    """Mirror FrameProcessor::invertBits."""
    return bytes(~b & 0xFF for b in data)


def derandomize(data: bytes, taps: tuple[int, int] = (13, 14), width: int = 15) -> bytes:
    """Mirror FrameProcessor::derandomizeBitstream — RNRZ-L, MSB-first.

    Defaults are the PRN-15 taps the app uses. Pass taps=(8, 10), width=11 to
    test a PRN-11 stream, which the app cannot currently decode.
    """
    mask, lfsr = (1 << width) - 1, 0
    out = bytearray(data)
    tap1, tap2 = taps
    for i in range(len(data) * 8):
        idx, shift = i >> 3, 7 - (i & 7)
        in_bit = (data[idx] >> shift) & 1
        descrambled = in_bit ^ (((lfsr >> tap1) & 1) ^ ((lfsr >> tap2) & 1))
        lfsr = ((lfsr << 1) | in_bit) & mask
        if descrambled:
            out[idx] |= 1 << shift
        else:
            out[idx] &= ~(1 << shift) & 0xFF
    return bytes(out)


def reverse_bits(data: bytes) -> bytes:
    """LSB-first bit packing (TMATS P-x\\F2 = 'L').

    The app has no LSB-first path — frameprocessor.cpp walks bits MSB-first
    unconditionally — so a hit in an lsb1st row means the file is unreadable
    as shipped, not merely misconfigured.
    """
    return bytes(int(f"{b:08b}"[::-1], 2) for b in data)


# ---------------------------------------------------------------------------
#                              SYNC ANALYSIS
# ---------------------------------------------------------------------------

def scan(data: bytes, pattern: int, pattern_len: int, mask: int | None = None) -> list[int]:
    """Mirror FrameProcessor::scanBit's sliding register. Returns hit offsets."""
    if mask is None:
        mask = (1 << pattern_len) - 1
    target = pattern & mask
    test, loaded, hits = 0, 0, []
    for i in range(len(data) * 8):
        test = ((test << 1) | ((data[i >> 3] >> (7 - (i & 7))) & 1)) & 0xFFFFFFFFFFFFFFFF
        loaded += 1
        if loaded >= pattern_len and (test & mask) == target:
            hits.append(i - pattern_len + 1)
    return hits


def grid(hits: list[int], frame_bits: int | None = None):
    """Measure how well hits fall on a regular frame grid.

    Counting raw hits is misleading on randomized streams, where data words
    coincidentally equal the sync pattern (see docs/framesync_logic.md §3).
    What matters is how many share one residue mod the frame length — that is
    the set the scanner would accept as in-phase.

    Returns (period, hits_on_that_grid, top_5_consecutive_deltas).
    """
    if len(hits) < 2:
        return None, 0, []
    deltas = Counter(b - a for a, b in zip(hits, hits[1:]))
    top = deltas.most_common(5)
    periods = [frame_bits] if frame_bits else [d for d, _ in top if d > 0]
    best_period, best_count = None, 0
    for period in periods:
        if not period or period <= 0:
            continue
        count = max(Counter(h % period for h in hits).values())
        if count > best_count:
            best_period, best_count = period, count
    return best_period, best_count, top


def discover(data: bytes, word_len: int = 32, min_reps: int = 6,
             max_bits: int = 400_000) -> list[tuple[int, int, int, int]]:
    """Find words recurring at a constant bit period.

    Independent of any configured pattern: whatever repeats on a fixed grid is
    almost certainly the frame sync, and its period is the minor-frame length.
    Capped at max_bits because this is O(bits) in both time and memory.

    Returns [(hits_on_grid, word, period_bits, total_hits)], best first.
    """
    nbits = min(len(data) * 8, max_bits)
    positions: defaultdict[int, list[int]] = defaultdict(list)
    test, mask = 0, (1 << word_len) - 1
    for i in range(nbits):
        test = ((test << 1) | ((data[i >> 3] >> (7 - (i & 7))) & 1)) & mask
        if i >= word_len - 1:
            positions[test].append(i - word_len + 1)

    found = []
    for word, offsets in positions.items():
        if len(offsets) < min_reps:
            continue
        deltas = Counter(b - a for a, b in zip(offsets, offsets[1:]))
        period, reps = deltas.most_common(1)[0]
        if reps >= min_reps - 1 and period > word_len:
            on_grid = max(Counter(o % period for o in offsets).values())
            found.append((on_grid, word, period, len(offsets)))
    found.sort(reverse=True)
    return found[:8]


# ---------------------------------------------------------------------------
#                                   TMATS
# ---------------------------------------------------------------------------

def parse_tmats(body: bytes) -> tuple[dict[str, str], str]:
    """TMATS body is a 4-byte CSDW followed by ASCII 'CODE:VALUE;' attributes."""
    text = body[4:].decode("latin-1", errors="replace")
    attrs = {}
    for line in text.replace("\r", "\n").split("\n"):
        line = line.strip()
        if not line or ":" not in line:
            continue
        code, _, value = line.partition(":")
        attrs[code.strip()] = value.strip().rstrip(";")
    return attrs, text


def report_tmats(attrs: dict[str, str]) -> None:
    print("\n--- TMATS: what prepare() depends on ---")
    g_rec = {k: v for k, v in attrs.items() if k.startswith("G\\")}
    r_rec = {k: v for k, v in attrs.items() if k.startswith("R-")}
    p_rec = {k: v for k, v in attrs.items() if k.startswith("P-")}
    print(f"  G-record entries: {len(g_rec)}   R-record: {len(r_rec)}   P-record: {len(p_rec)}")
    if not g_rec or not r_rec:
        print("  !! assembleAttributesFromTMATS() requires BOTH a G-record and an R-record;")
        print("     without them it returns I106_INVALID_DATA and prepare() fails outright.")

    tracks = {k: v for k, v in r_rec.items() if "\\TK1" in k}
    print("\n  Track numbers (R-x\\TK1-n) -> channel_info[] index:")
    if not tracks:
        print("    !! none present — nothing populates channel_info[], so every")
        print("       configured PCM channel fails the 'Channel info not set up' check")
    for key, value in sorted(tracks.items()):
        try:
            idx = int(value)
            note = "ok" if 0 <= idx < 0x10000 else "OUT OF RANGE -> I106_BUFFER_TOO_SMALL"
        except ValueError:
            note = "NON-NUMERIC -> atoi() yields 0, channel collapses to index 0"
        print(f"    {key} = {value!r:>10}  ({note})")

    for label, needle in (("Channel data type (must be PCMIN)", "\\CDT"),
                          ("Data source ID", "\\DSI"),
                          ("Channel enabled", "\\CHE")):
        matches = {k: v for k, v in r_rec.items() if needle in k}
        if matches:
            print(f"\n  {label}:")
            for key, value in sorted(matches.items())[:12]:
                print(f"    {key} = {value}")

    print("\n  P-record PCM attributes (Set_Attributes_PcmF1 reads these):")
    codes = {
        "\\D2": "bit rate", "\\F1": "common word len", "\\F2": "transfer order (M/L)",
        "\\F3": "parity", "\\MF1": "words in minor frame", "\\MF2": "bits in minor frame",
        "\\MF3": "sync type (must be FPT)", "\\MF4": "sync pattern length",
        "\\MF5": "sync pattern", "\\MF\\N": "num minor frames", "\\SYNC1": "min syncs",
    }
    found_any = False
    for key in sorted(p_rec):
        for suffix, desc in codes.items():
            if not key.endswith(suffix):
                continue
            print(f"    {key:<16} = {p_rec[key]:<36} [{desc}]")
            found_any = True
            if suffix == "\\F2" and p_rec[key].upper().startswith("L"):
                print("      !! LSB-FIRST. Set_Attributes_PcmF1 returns I106_UNSUPPORTED and")
                print("         bails BEFORE populating any frame attributes — and")
                print("         ch10packetreader.cpp discards that return with (void).")
            if suffix == "\\MF3" and p_rec[key].upper() != "FPT":
                print("      !! sync type is not FPT; the library only handles FPT")
    if not found_any:
        print("    (no PCM attribute codes present — an old or minimal TMATS)")


# ---------------------------------------------------------------------------
#                                    MAIN
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Diagnose why a Chapter 10 file yields no frame syncs.")
    parser.add_argument("file")
    parser.add_argument("--pattern", default="FE6B2840",
                        help="frame sync pattern in hex (default: FE6B2840)")
    parser.add_argument("--pattern-len", type=int, default=None,
                        help="sync pattern length in bits (default: 4 * hex digits)")
    parser.add_argument("--frame-bits", type=int, default=None,
                        help="bits in minor frame, as configured in the stream")
    parser.add_argument("--packets", type=int, default=40,
                        help="PCM packets to include in the bit-level scan (default: 40)")
    parser.add_argument("--max-packets", type=int, default=4000,
                        help="packets to walk for the census (default: 4000)")
    parser.add_argument("--dump-tmats", action="store_true",
                        help="print the full TMATS text")
    args = parser.parse_args()

    pattern = int(args.pattern, 16)
    pattern_len = args.pattern_len or (len(args.pattern.strip()) * 4)

    print(f"=== {args.file} ===")
    print(f"sync pattern 0x{pattern:X} ({pattern_len} bits), "
          f"frame bits: {args.frame_bits or 'unspecified'}")

    census: Counter[int] = Counter()
    chan_by_type: defaultdict[int, Counter[int]] = defaultdict(Counter)
    first_packet = None
    pcm_payloads: list[bytes] = []
    tmats_attrs: dict[str, str] | None = None
    tmats_text = ""

    for off, chan, dtype, _flags, body in walk(args.file, args.max_packets):
        if first_packet is None:
            first_packet = (dtype, chan, off)
        census[dtype] += 1
        chan_by_type[dtype][chan] += 1
        if dtype == DT_TMATS and tmats_attrs is None:
            tmats_attrs, tmats_text = parse_tmats(body)
        elif dtype == DT_PCM and len(pcm_payloads) < args.packets:
            pcm_payloads.append(body[4:])  # skip the 4-byte channel-specific data word

    if first_packet is None:
        print("\n  !! no readable Ch10 packets — this may not be a Chapter 10 file")
        return 1

    print("\n--- Packet census ---")
    for dtype, count in census.most_common():
        name = DTYPE_NAMES.get(dtype, "unknown")
        chans = ", ".join(f"ch{c}({n})" for c, n in chan_by_type[dtype].most_common(8))
        print(f"  0x{dtype:02X} {name:<24} {count:>6}  [{chans}]")

    print("\n--- prepare() gate checks ---")
    dtype, chan, off = first_packet
    if dtype == DT_TMATS:
        print(f"  First packet is TMATS (ch{chan}) -> passes the first-packet gate")
    else:
        print(f"  !! First packet is 0x{dtype:02X} ({DTYPE_NAMES.get(dtype, 'unknown')}) "
              f"on ch{chan} at offset {off}, not TMATS")
        print("     -> prepare() fails with 'Failed to find TMATS message.'")
    if DT_PCM not in census:
        print("  !! no PCM Format 1 packets in this file at all")
    if DT_TIME not in census:
        print("  !! no IRIG Time F1 packets -> enI106_SyncTime may fail")

    if tmats_attrs is not None:
        report_tmats(tmats_attrs)
        if args.dump_tmats:
            print("\n--- TMATS raw ---")
            print(tmats_text)
    else:
        print("\n  !! no TMATS packet found")

    if not pcm_payloads:
        print("\nNo PCM payloads to scan. Stopping.")
        return 1

    blob = b"".join(pcm_payloads)
    odd = sum(1 for p in pcm_payloads if len(p) & 1)
    print(f"\n--- Sync search over {len(pcm_payloads)} PCM packets "
          f"({len(blob)} bytes, {len(blob) * 8} bits) ---")
    print(f"  payload lengths: min={min(len(p) for p in pcm_payloads)} "
          f"max={max(len(p) for p in pcm_payloads)} odd={odd}/{len(pcm_payloads)}")
    if odd:
        print("  !! odd-length payloads are silently NOT byte-swapped while their")
        print("     even-length neighbors are — an inconsistent bitstream")

    results = []
    for swap in (False, True):
        for inv in (False, True):
            for rand in (False, True):
                for lsb in (False, True):
                    bits = blob
                    if swap:
                        bits = swap_bytes(bits)
                    if inv:
                        bits = invert(bits)
                    if rand:
                        bits = derandomize(bits)
                    if lsb:
                        bits = reverse_bits(bits)
                    hits = scan(bits, pattern, pattern_len)
                    period, on_grid, top = grid(hits, args.frame_bits)
                    results.append({"swap": swap, "inv": inv, "rand": rand, "lsb": lsb,
                                    "hits": len(hits), "period": period,
                                    "on_grid": on_grid, "top": top})

    print(f"\n  {'swap':<6}{'invert':<8}{'derand':<8}{'lsb1st':<8}"
          f"{'hits':>7}{'on-grid':>9}{'period':>9}  note")
    for row in results:
        key = (row["swap"], row["inv"], row["rand"], row["lsb"])
        note = ""
        if key == (True, False, False, False):
            note = "<- app default (unrandomized)"
        elif key == (True, False, True, False):
            note = "<- app default (randomized)"
        print(f"  {str(row['swap']):<6}{str(row['inv']):<8}{str(row['rand']):<8}"
              f"{str(row['lsb']):<8}{row['hits']:>7}{row['on_grid']:>9}"
              f"{str(row['period']) if row['period'] else '-':>9}  {note}")

    best = max(results, key=lambda r: (r["on_grid"], r["hits"]))
    print()
    if not best["hits"]:
        print("  RESULT: the pattern never appears under ANY transform combination.")
        print("          The configured sync pattern is likely wrong for this file —")
        print("          see the auto-discovery pass below for what actually repeats.")
    else:
        print(f"  RESULT: best = swap={best['swap']} invert={best['inv']} "
              f"derand={best['rand']} lsb_first={best['lsb']}")
        print(f"          {best['hits']} raw hits, {best['on_grid']} on a "
              f"{best['period']}-bit grid")
        print(f"          consecutive-hit deltas (top 5): {best['top']}")
        if not best["swap"]:
            print("          !! Needs NO byte swap, but the app always swaps:")
            print("             ch10packetreader.cpp passes lNoByteSwap=-1 to")
            print("             Set_Attributes_Ext_PcmF1, so bDontSwapRawData stays 0")
            print("             from the calloc and needsSwap is unconditionally true.")
        if best["lsb"]:
            print("          !! Needs LSB-first bit order; the scanner is hardcoded MSB-first.")
        if args.frame_bits and best["period"] and best["period"] != args.frame_bits:
            print(f"          !! Frame grid is {best['period']} bits, not the "
                  f"configured {args.frame_bits}.")

    print("\n--- Auto-discovery: words recurring at a constant period ---")
    print("    (ignores the configured pattern entirely)")
    word_len = pattern_len if pattern_len in (16, 24, 32) else 32
    for label, bits in (("raw", blob),
                        ("swapped", swap_bytes(blob)),
                        ("swapped+derand", derandomize(swap_bytes(blob)))):
        found = discover(bits, word_len=word_len)
        print(f"\n  {label}:")
        if not found:
            print("    (nothing recurs at a constant period)")
        for on_grid, word, period, total in found:
            marker = "  <-- matches configured pattern" if word == pattern else ""
            print(f"    0x{word:0{(word_len + 3) // 4}X}  period {period:>7} bits   "
                  f"{on_grid}/{total} on grid{marker}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
