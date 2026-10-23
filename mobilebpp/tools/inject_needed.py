#!/usr/bin/env python3
"""Add DT_NEEDED=<lib> to an ELF shared object (LIEF).

LIEF cannot open paths containing non-ASCII characters on Windows, so the
input is staged through an ASCII work directory.
"""
import argparse
import os
import shutil
import sys

import lief


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--lib", default="libbpp.so")
    ap.add_argument("--out", required=True)
    ap.add_argument("--stage", default=r"C:\bppwork")
    args = ap.parse_args()

    os.makedirs(args.stage, exist_ok=True)
    stage_in = os.path.join(args.stage, "in.so")
    stage_out = os.path.join(args.stage, "out.so")
    shutil.copyfile(args.elf, stage_in)

    binary = lief.parse(stage_in)
    if binary is None:
        sys.exit("LIEF failed to parse input")
    libs = list(binary.libraries)
    if args.lib in libs:
        print(f"{args.lib} already present")
    else:
        binary.add_library(args.lib)
        binary.write(stage_out)
        shutil.copyfile(stage_out, args.out)
        print(f"added DT_NEEDED {args.lib} -> {args.out}")

    # verify
    check = lief.parse(os.path.join(args.stage, "out.so" if args.lib not in libs else "in.so"))
    if check and args.lib in list(check.libraries):
        print("verified DT_NEEDED:", list(check.libraries))
    else:
        sys.exit("verification failed")


if __name__ == "__main__":
    main()
