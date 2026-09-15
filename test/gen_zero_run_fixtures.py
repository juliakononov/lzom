#!/usr/bin/env python3
"""Regenerates the zero-run-related fixtures under test_files/.

These target the LZO-RLE zero-run instruction in lzom_compress.c
(MIN_ZERO_RUN_LENGTH=4, MAX_ZERO_RUN_LENGTH=2051) and the compressor's
internal per-chunk offset limit (M4_MAX_OFFSET_V1+1 = 0xbfff = 49151
bytes), which run_tests.sh cannot reach with the general-purpose
fixtures alone since it now writes/reads each file as a single bio.

Padding uses a non-zero fill byte and is rounded up to a 512-byte
(sector) boundary so the O_DIRECT write in run_tests.sh doesn't pick up
extra unintended zero runs and stays alignment-safe. Re-run this script
after changing it; it overwrites its four output files deterministically
(fixed RNG seed) so the diff stays reviewable.
"""
import os
import random

OUT_DIR = os.path.join(os.path.dirname(__file__), "test_files")
FILL = 0xAA


def nonzero_pad(data, align=512, fill=FILL):
    rem = len(data) % align
    if rem == 0:
        return data
    return data + bytes([fill]) * (align - rem)


def write(name, data):
    path = os.path.join(OUT_DIR, name)
    with open(path, "wb") as f:
        f.write(data)
    print(f"wrote {path} ({len(data)} bytes)")


def main():
    rng = random.Random(10)

    # Exactly MIN_ZERO_RUN_LENGTH (4) zero bytes, placed at offset 5 -
    # where the compressor's very first dv==0 probe lands (after the
    # mandatory initial 4-byte skip plus one "literal:" advance step).
    head = bytes(rng.randrange(1, 256) for _ in range(5))
    run = bytes(4)
    tail = bytes(rng.randrange(1, 256) for _ in range(600))
    write("zero_run_min.bin", nonzero_pad(head + run + tail))

    # One byte short of MIN_ZERO_RUN_LENGTH: must NOT take the RLE path
    # at all (dv==0 requires a full 4-byte zero window).
    head = bytes(rng.randrange(1, 256) for _ in range(5))
    run = bytes(3)
    tail = bytes(rng.randrange(1, 256) for _ in range(600))
    write("zero_run_below_min.bin", nonzero_pad(head + run + tail))

    # MAX_ZERO_RUN_LENGTH + 1 (2052) zero bytes: must clamp to a single
    # 2051-byte instruction, with the one leftover zero byte falling
    # back to ordinary literal/match encoding.
    head = bytes(rng.randrange(1, 256) for _ in range(5))
    run = bytes(2052)
    tail = bytes(rng.randrange(1, 256) for _ in range(600))
    write("zero_run_max_boundary.bin", nonzero_pad(head + run + tail))

    # A zero run straddling the compressor's internal per-chunk offset
    # limit (M4_MAX_OFFSET_V1 + 1 = 49151 bytes into the buffer), so the
    # run gets split across two separate lzo1x_1_do_compress() chunks
    # with independent dictionaries/offsets.
    before = bytes(rng.randrange(1, 256) for _ in range(49140))
    run = bytes(40)
    after = bytes(rng.randrange(1, 256) for _ in range(20000))
    write("zero_run_chunk_boundary.bin", nonzero_pad(before + run + after))


if __name__ == "__main__":
    main()
