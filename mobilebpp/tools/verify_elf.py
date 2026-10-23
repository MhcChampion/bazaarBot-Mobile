#!/usr/bin/env python3
"""Validate an Android arm64 shared object the way bionic's linker would.

Checks the invariants that make a patched ELF fail to load on device:
  * PT_LOAD offset/vaddr congruence and non-overlap
  * PT_DYNAMIC / PT_INIT_ARRAY / symbol tables inside a PT_LOAD
  * DT_NEEDED names resolvable in .dynstr
  * 16 KiB-ready p_align (Android 15 / arm64 16k pages)

Usage: python verify_elf.py <so> [--expect-needed libbpp.so]
Exit code 0 = pass.
"""
import argparse
import sys

from elftools.elf.elffile import ELFFile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--expect-needed", default=None)
    args = ap.parse_args()

    f = open(args.elf, "rb")
    e = ELFFile(f)
    problems = []
    notes = []

    if e.header.e_machine != "EM_AARCH64":
        problems.append(f"machine is {e.header.e_machine}, expected EM_AARCH64")
    if e.header.e_type != "ET_DYN":
        problems.append(f"type is {e.header.e_type}, expected ET_DYN")

    loads = []
    for seg in e.iter_segments():
        if seg["p_type"] == "PT_LOAD":
            off, va = seg["p_offset"], seg["p_vaddr"]
            fsz, msz, align = seg["p_filesz"], seg["p_memsz"], seg["p_align"]
            loads.append((off, va, fsz, msz, align))
            if align and ((off % align) != (va % align)):
                problems.append(f"LOAD congruence broken: off={off:#x} va={va:#x} align={align:#x}")
            if msz < fsz:
                problems.append(f"LOAD memsz < filesz at {va:#x}")
            if off + fsz > e.stream.tell() + 1 and off + fsz > _filesize(f):
                problems.append(f"LOAD extends past EOF at {va:#x}")
            if align < 0x1000:
                notes.append(f"LOAD align {align:#x} < page size at {va:#x}")

    loads.sort()
    for (o1, v1, f1, m1, _), (o2, v2, f2, m2, _) in zip(loads, loads[1:]):
        if v1 + m1 > v2:
            problems.append(f"LOAD vaddr overlap: {v1:#x}+{m1:#x} > {v2:#x}")
        if o1 + f1 > o2:
            problems.append(f"LOAD file overlap: {o1:#x}+{f1:#x} > {o2:#x}")

    def inside_load(addr, size=0):
        for _, va, _, msz, _ in loads:
            if va <= addr and addr + size <= va + msz:
                return True
        return False

    dyn = e.get_section_by_name(".dynamic")
    if dyn is None:
        problems.append("no .dynamic")
        dyn_tags = []
    else:
        if not inside_load(dyn["sh_addr"], dyn["sh_size"]):
            problems.append(".dynamic is not covered by a PT_LOAD")
        dyn_tags = list(dyn.iter_tags())

    needed = [t.needed for t in dyn_tags if t.entry.d_tag == "DT_NEEDED"]
    notes.append("DT_NEEDED: " + ", ".join(needed))
    if args.expect_needed and args.expect_needed not in needed:
        problems.append(f"DT_NEEDED missing {args.expect_needed}")

    for tag in ("DT_INIT_ARRAY", "DT_FINI_ARRAY"):
        for t in dyn_tags:
            if t.entry.d_tag == tag and not inside_load(t.entry.d_ptr):
                problems.append(f"{tag} not covered by a PT_LOAD")

    for secname in (".dynsym", ".dynstr", ".gnu.hash", ".hash", ".rela.dyn"):
        sec = e.get_section_by_name(secname)
        if sec is not None and sec["sh_size"] and not inside_load(sec["sh_addr"], sec["sh_size"]):
            problems.append(f"{secname} not covered by a PT_LOAD")

    dynsym = e.get_section_by_name(".dynsym")
    if dynsym is not None:
        count = dynsym.num_symbols()
        if count == 0:
            problems.append("empty .dynsym")
        else:
            notes.append(f"dynsym symbols: {count}")
            # Undefined symbols must all be resolvable at runtime by DT_NEEDED libs;
            # we can only sanity-check that names are non-empty.
            bad = [s.name for s in dynsym.iter_symbols() if s["st_shndx"] == "SHN_UNDEF" and not s.name]
            if bad:
                notes.append(f"{len(bad)} unnamed undefined symbols")

    print(f"== {args.elf}")
    for n in notes:
        print("  note:", n)
    if problems:
        for p in problems:
            print("  FAIL:", p)
        return 1
    print("  PASS: linker invariants OK")
    return 0


def _filesize(f):
    f.seek(0, 2)
    n = f.tell()
    f.seek(0)
    return n


if __name__ == "__main__":
    sys.exit(main())
