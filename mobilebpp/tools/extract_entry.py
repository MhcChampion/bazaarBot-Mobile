#!/usr/bin/env python3
"""Extract a single entry from the official APK to a file (ASCII-safe staging)."""
import argparse
import shutil
import zipfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apk", required=True)
    ap.add_argument("--entry", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    with zipfile.ZipFile(args.apk) as z, z.open(args.entry) as src, open(args.out, "wb") as dst:
        shutil.copyfileobj(src, dst, 1 << 22)
    print(f"extracted {args.entry} -> {args.out}")


if __name__ == "__main__":
    main()
