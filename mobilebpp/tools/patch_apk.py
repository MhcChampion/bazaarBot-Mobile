#!/usr/bin/env python3
"""Repack the official The Bazaar APK with the BPP mobile native mod.

Usage:
  python patch_apk.py --apk TheBazaar.apk --lib build/libbpp.so \
      --il2cpp-patched work/official/libil2cpp.bpp.so --out out/TheBazaar-bpp.apk

The patched libil2cpp.so must already carry DT_NEEDED=libbpp.so (see inject_needed.py).
Signing is done by sign_apk.ps1 (zipalign + apksigner).
"""
import argparse
import os
import shutil
import sys
import zipfile
from pathlib import Path

from apk_identity import BinaryManifest, mobile_app_label, patch_manifest, patch_resource_package

STORE = zipfile.ZIP_STORED
# Android zipalign does not understand ZIP64 end records; Python is conservative
# and switches to ZIP64 above 2 GiB. All entries here are < 4 GiB, so raise the
# limit to the real 32-bit field maximum and keep a plain EOCD.
zipfile.ZIP64_LIMIT = 0xFFFFFFFF


def copy_entry(src, dst, info, replace_bytes=None):
    # Preserve metadata; write raw for stored entries, deflate otherwise.
    zi = zipfile.ZipInfo(info.filename, date_time=info.date_time)
    zi.compress_type = info.compress_type
    zi.external_attr = info.external_attr
    zi.internal_attr = info.internal_attr
    zi.create_system = info.create_system
    zi.comment = info.comment
    if replace_bytes is not None:
        zi.compress_type = STORE
        dst.writestr(zi, replace_bytes)
        return
    with src.open(info, "r") as fsrc, dst.open(zi, "w") as fdst:
        shutil.copyfileobj(fsrc, fdst, 1 << 22)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apk", required=True)
    ap.add_argument("--lib", required=True, help="libbpp.so")
    ap.add_argument("--il2cpp-patched", default=None,
                    help="patched libil2cpp.so carrying DT_NEEDED=libbpp.so")
    ap.add_argument("--libmain-patched", default=None,
                    help="fallback: patched libmain.so carrying DT_NEEDED=libbpp.so")
    ap.add_argument("--out", required=True)
    ap.add_argument("--lib-name", default="libbpp.so")
    ap.add_argument("--package-name", help="independent Android application ID; preserves Java class names")
    ap.add_argument("--app-label", help="literal application label; package rename defaults to original label + ' - bazaarBot Mobile'")
    ap.add_argument("--version-name", help="Android versionName")
    ap.add_argument("--version-code", type=int, help="Android versionCode")
    args = ap.parse_args()

    if Path(args.apk).resolve() == Path(args.out).resolve():
        ap.error("--out must not overwrite the source APK")
    manifest_bytes = None
    resource_bytes = None
    if any(value is not None for value in (args.package_name, args.app_label, args.version_name, args.version_code)):
        label = args.app_label
        if args.package_name and label is None:
            label = mobile_app_label(args.apk)
        with zipfile.ZipFile(args.apk) as source:
            original_manifest = source.read('AndroidManifest.xml')
            manifest_bytes = patch_manifest(original_manifest, package_name=args.package_name,
                                            app_label=label, version_name=args.version_name, version_code=args.version_code)
            if args.package_name is not None:
                resource_bytes = patch_resource_package(source.read('resources.arsc'),
                                                        BinaryManifest(original_manifest).package, args.package_name)
        identity = BinaryManifest(manifest_bytes)
        print(f"manifest package: {identity.package}; label: {label or '(unchanged)'}")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    if not args.il2cpp_patched and not args.libmain_patched:
        sys.exit("need --il2cpp-patched or --libmain-patched")
    lib_bytes = Path(args.lib).read_bytes()
    il2cpp_bytes = Path(args.il2cpp_patched).read_bytes() if args.il2cpp_patched else None
    libmain_bytes = Path(args.libmain_patched).read_bytes() if args.libmain_patched else None

    with zipfile.ZipFile(args.apk, "r") as src, zipfile.ZipFile(args.out, "w", allowZip64=False) as dst:
        entries = src.infolist()
        names = {e.filename for e in entries}
        replaced = False
        for info in entries:
            if info.filename == "AndroidManifest.xml" and manifest_bytes is not None:
                copy_entry(src, dst, info, manifest_bytes)
                continue
            if info.filename == "resources.arsc" and resource_bytes is not None:
                copy_entry(src, dst, info, resource_bytes)
                continue
            if info.filename == "lib/arm64-v8a/libil2cpp.so" and il2cpp_bytes is not None:
                copy_entry(src, dst, info, il2cpp_bytes)
                replaced = True
                continue
            if info.filename == "lib/arm64-v8a/libmain.so" and libmain_bytes is not None:
                copy_entry(src, dst, info, libmain_bytes)
                replaced = True
                continue
            # Drop stale signature files; apksigner will regenerate them.
            if info.filename.upper().startswith("META-INF/") and info.filename.upper().endswith(
                (".RSA", ".DSA", ".EC", ".SF")
            ):
                continue
            if info.filename.upper() == "META-INF/MANIFEST.MF":
                continue
            copy_entry(src, dst, info)
        if not replaced:
            sys.exit("target library not found in APK")

        lib_path = "lib/arm64-v8a/" + args.lib_name
        if lib_path not in names:
            zi = zipfile.ZipInfo(lib_path, date_time=(2026, 1, 1, 0, 0, 0))
            zi.compress_type = STORE
            zi.external_attr = 0o644 << 16
            dst.writestr(zi, lib_bytes)
            print(f"added {lib_path} ({len(lib_bytes)} bytes)")
        else:
            print(f"warning: {lib_path} already existed")

        art_dir = Path(__file__).resolve().parents[1] / 'web/art'
        art_files = sorted(art_dir.glob('*.webp'))
        for art in art_files:
            # Android's built-in file:///android_asset URL works without copying
            # assets to external storage or granting extra permissions.
            dst.write(art, 'assets/bpp/art/' + art.name, compress_type=STORE)
        print(f'added card pictures: {len(art_files)}')
        for frame in sorted((art_dir.parent / 'frames').glob('*.webp')):
            dst.write(frame, 'assets/bpp/frames/' + frame.name, compress_type=STORE)
        for asset in sorted((art_dir.parent / 'theme').iterdir()):
            if asset.suffix in ('.webp', '.woff2'):
                dst.write(asset, 'assets/bpp/theme/' + asset.name, compress_type=STORE)

    print(f"wrote {args.out} ({os.path.getsize(args.out)} bytes)")


if __name__ == "__main__":
    main()
