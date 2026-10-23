#!/usr/bin/env python3
"""Post-build APK checks: contents, DT_NEEDED, 16 KiB alignment of stored libs."""
import argparse
import io
from pathlib import Path
import re
import struct
import subprocess
import sys
import zipfile

from elftools.elf.elffile import ELFFile


def local_data_offset(zf, info):
    f = zf.fp
    f.seek(info.header_offset)
    hdr = f.read(30)
    if hdr[:4] != b"PK\x03\x04":
        return None
    name_len, extra_len = struct.unpack_from("<HH", hdr, 26)
    return info.header_offset + 30 + name_len + extra_len


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apk", required=True)
    ap.add_argument("--expect-lib", default="libbpp.so")
    ap.add_argument("--page", type=int, default=16384)
    ap.add_argument("--expect-package")
    ap.add_argument("--expect-label")
    ap.add_argument("--expect-version-name")
    ap.add_argument("--expect-version-code", type=int)
    ap.add_argument("--expect-game-version-from", help="reference APK whose Android version fields must be preserved")
    args = ap.parse_args()

    problems = []
    expected = {'package': args.expect_package, 'label': args.expect_label,
                'version_name': args.expect_version_name, 'version_code': args.expect_version_code}
    if args.expect_game_version_from or any(value is not None for value in expected.values()):
        aapt = Path(__file__).resolve().parents[2] / 'tools/buildtools/android-15/aapt.exe'
        if args.expect_game_version_from:
            source_apk = Path(args.expect_game_version_from).resolve()
            source = subprocess.run([str(aapt), 'dump', 'badging', source_apk.name], cwd=source_apk.parent,
                                    capture_output=True, encoding='utf-8', errors='replace')
            version = re.search(r"^package: name='[^']+' versionCode='([0-9]+)' versionName='([^']*)'", source.stdout, re.M)
            if source.returncode or not version:
                problems.append('aapt could not resolve reference game version')
            else:
                for key, value in [('version_code', int(version[1])), ('version_name', version[2])]:
                    if expected[key] is not None and expected[key] != value:
                        problems.append(f'{key}: explicit expectation conflicts with reference game APK')
                    expected[key] = value
                print('reference game version:', {key: expected[key] for key in ['version_name', 'version_code']})
        apk_path = Path(args.apk).resolve()
        result = subprocess.run([str(aapt), 'dump', 'badging', apk_path.name], cwd=apk_path.parent,
                                capture_output=True, encoding='utf-8', errors='replace')
        if result.returncode:
            problems.append('aapt could not parse APK identity: ' + result.stderr[:300])
        else:
            package = re.search(r"^package: name='([^']+)' versionCode='([^']+)' versionName='([^']*)'", result.stdout, re.M)
            label = re.search(r"^application-label:'([^']*)'", result.stdout, re.M)
            actual = {'package': package[1] if package else None,
                      'version_code': int(package[2]) if package and package[2].isdigit() else None,
                      'version_name': package[3] if package else None, 'label': label[1] if label else None}
            print('identity:', actual)
            for key, value in expected.items():
                if value is not None and actual[key] != value:
                    problems.append(f'{key}: expected {value!r}, got {actual[key]!r}')
            if args.expect_package:
                resources = subprocess.run([str(aapt), 'dump', 'resources', apk_path.name], cwd=apk_path.parent,
                                           capture_output=True, encoding='utf-8', errors='replace')
                resource_packages = re.findall(r'^\s+Package \d+ id=0x7f name=(\S+)', resources.stdout, re.M)
                print('resource packages:', resource_packages)
                if resources.returncode or resource_packages != [args.expect_package]:
                    problems.append(f'application resource namespace must equal {args.expect_package!r}')
    with zipfile.ZipFile(args.apk) as z:
        names = z.namelist()
        print(f"entries: {len(names)}")
        lib_entries = [n for n in names if n.startswith("lib/arm64-v8a/")]
        print("libs:", ", ".join(sorted(lib_entries)))
        for name in lib_entries:
            info = z.getinfo(name)
            if name.endswith('.so') and info.compress_type == zipfile.ZIP_STORED:
                offset = local_data_offset(z, info)
                if offset is None or offset % args.page:
                    problems.append(f'{name} is not {args.page}-byte aligned')

        target = f"lib/arm64-v8a/{args.expect_lib}"
        if target not in names:
            problems.append(f"{target} missing")
        else:
            info = z.getinfo(target)
            print(f"{target}: {info.file_size} bytes, compress_type={info.compress_type}")
            off = local_data_offset(z, info)
            if info.compress_type == 0:
                print(f"  data offset {off:#x} (page {args.page:#x})", end="")
                ok = off is not None and off % args.page == 0
                print("  ALIGNED" if ok else "  MISALIGNED")
                if not ok:
                    problems.append(f"{target} not {args.page}-byte aligned (offset {off})")

        # DT_NEEDED present on the patched host library?
        host = None
        for cand in ("libil2cpp.so", "libmain.so"):
            path = f"lib/arm64-v8a/{cand}"
            if path in names:
                data = z.read(path)
                elf = ELFFile(io.BytesIO(data))
                dyn = elf.get_section_by_name(".dynamic")
                needed = [t.needed for t in dyn.iter_tags() if t.entry.d_tag == "DT_NEEDED"] if dyn else []
                if args.expect_lib in needed:
                    host = cand
                    print(f"{cand}: DT_NEEDED includes {args.expect_lib}")
                    break
        if host is None:
            problems.append(f"no library declares DT_NEEDED {args.expect_lib}")

        # The bionic linker needs the phdr table inside the file and mapped.
        for cand in ("libil2cpp.so", "libmain.so", args.expect_lib):
            path = f"lib/arm64-v8a/{cand}"
            if path not in names:
                continue
            elf = ELFFile(io.BytesIO(z.read(path)))
            phoff = elf.header.e_phoff
            phnum = elf.header.e_phnum
            phentsize = elf.header.e_phentsize
            end = phoff + phnum * phentsize
            print(f"{cand}: phdr {phoff:#x}..{end:#x} phnum={phnum}")
            size = z.getinfo(path).file_size
            if end > size:
                problems.append(f'{cand}: program headers extend past EOF')
            if not any(seg['p_type'] == 'PT_LOAD' and seg['p_offset'] <= phoff and end <= seg['p_offset'] + seg['p_filesz'] for seg in elf.iter_segments()):
                problems.append(f'{cand}: program headers not mapped by a LOAD segment')

    if problems:
        for p in problems:
            print("FAIL:", p)
        return 1
    print("APK checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
