"""Convert a newer-season particle material uber buffer (240 bytes) to the S21
layout (144 bytes) so Repak can pack it as a v23 PTCU / PTCS material.

Usage:
    py -3 convert_particle_uber.py <file.uber | dir> [more ...] [-o out_dir] [--in-place]

Each input is one .uber sidecar as exported by RSX, or a directory that is
walked for *.uber. A file that is already 144 bytes is left alone.

The 144 bytes are the leading prefix of the 240-byte buffer, patched in three
slots. Copying the prefix unpatched ships a value the S21 shader treats as a
real float and the client crashes in the render thread.
"""
import argparse
import os
import struct
import sys

S30_SIZE = 240
S21_SIZE = 144
FLOAT_INF = 0x7F800000
FLOAT_HALF = 0x3F000000


def convert_uber(u30):
    if len(u30) < S21_SIZE:
        raise ValueError("uber is %d bytes, need at least %d" % (len(u30), S21_SIZE))
    out = bytearray(u30[:S21_SIZE])
    # +0x2C is 0xFFFFFFFF in every newer-season particle uber; S21 stores 0.5 there.
    struct.pack_into("<I", out, 0x2C, FLOAT_HALF)
    # +0x20 uses inf as the unused marker; S21 stores 0. Other infs in the prefix stay.
    v20, = struct.unpack_from("<I", out, 0x20)
    if v20 == FLOAT_INF:
        struct.pack_into("<I", out, 0x20, 0)
    # +0x5C holds packed integers in the newer layout; S21 ships 0.
    struct.pack_into("<I", out, 0x5C, 0)
    return bytes(out)


def collect(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, _dirs, files in os.walk(p):
                for f in files:
                    if f.lower().endswith(".uber"):
                        yield os.path.join(root, f)
        else:
            yield p


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("inputs", nargs="+", help=".uber files or directories")
    ap.add_argument("-o", "--out", help="output directory (mirrors input basenames)")
    ap.add_argument("--in-place", action="store_true", help="overwrite the input files")
    args = ap.parse_args(argv)
    if not args.out and not args.in_place:
        ap.error("give -o <dir> or --in-place")

    converted = skipped = failed = 0
    for src in collect(args.inputs):
        data = open(src, "rb").read()
        if len(data) == S21_SIZE:
            skipped += 1
            continue
        if len(data) != S30_SIZE:
            print("skip %s: %d bytes is not a particle uber" % (src, len(data)))
            failed += 1
            continue
        out = convert_uber(data)
        if args.in_place:
            dst = src
        else:
            os.makedirs(args.out, exist_ok=True)
            dst = os.path.join(args.out, os.path.basename(src))
        open(dst, "wb").write(out)
        converted += 1
        print("ok   %s -> %s" % (src, dst))
    print("converted %d, already S21 %d, rejected %d" % (converted, skipped, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
