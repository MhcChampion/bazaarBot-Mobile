#!/usr/bin/env python3
"""Verify every hard-coded field offset against dump.cs.

Offsets drift between game builds; a wrong one reads garbage and usually crashes
deep inside the game. Each entry below must match native/offsets.h.
"""
import pathlib
import re
import sys

DUMP = pathlib.Path(__file__).resolve().parents[2] / "work/il2cpp-dump/dump.cs"
OFFSETS_H = pathlib.Path(__file__).resolve().parents[1] / "native/offsets.h"

DECL = re.compile(
    r"^(?:public |internal |private |protected |sealed |abstract |static |partial |readonly )*"
    r"(class|struct|enum|interface) ([A-Za-z0-9_.]+)"
)

# (owning class, field, expected offset)
EXPECTED = [
    ("CardController", "CardData", 0x90),
    ("CardController", "boardSection", 0x98),
    ("Card", "TemplateId", 0x18),
    ("Card", "Size", 0x40),
    ("Card", "Tier", 0x50),
    ("CardTooltipData", "_cardInstance", 0x10),
    ("CardTooltipData", "_cardTemplate", 0x18),
    ("Touch", "m_Position", 0x04, "#24035"),
    ("Touch", "m_Phase", 0x24, "#24035"),
]

# constant name in offsets.h -> expected value
CONSTS = {
    "kObjectHeader": 0x10,
    "kArrayData": 0x20,
    "kCardControllerCardData": 0x90,
    "kCardControllerSection": 0x98,
    "kCardTemplateId": 0x18,
    "kCardSize": 0x40,
    "kCardTier": 0x50,
    "kTooltipCardInstance": 0x10,
    "kTooltipCardTemplate": 0x18,
    "kTouchPosition": 0x04,
    "kTouchPhase": 0x24,
}


def nearest_class(lines, idx):
    for j in range(idx, -1, -1):
        m = DECL.match(lines[j])
        if m:
            return m.group(2)
    return None


def main():
    lines = DUMP.read_text(encoding="utf-8", errors="replace").split("\n")
    problems = []
    for entry in EXPECTED:
        cls, field, want = entry[0], entry[1], entry[2]
        anchor = entry[3] if len(entry) > 3 else None
        found = []
        start = 0
        if anchor:
            want_idx = anchor.lstrip("#")
            for i, line in enumerate(lines):
                if f"TypeDefIndex: {want_idx}" in line and f"struct {cls}" in line or (
                    anchor and f"TypeDefIndex: {want_idx}" in line and f"class {cls}" in line
                ):
                    start = i
                    break
        for i, line in enumerate(lines[start:], start):
            if not re.search(rf"\b{re.escape(field)}\b", line) or "// 0x" not in line:
                continue
            if anchor:
                if i - start > 40:
                    continue
            elif nearest_class(lines, i) != cls:
                continue
            m = re.search(r"// 0x([0-9A-Fa-f]+)", line)
            if not m:
                continue
            found.append((int(m.group(1), 16), i + 1, line.strip()))
        if not found:
            problems.append(f"{cls}.{field}: not found in dump")
            continue
        for off, lineno, text in found:
            ok = off == want
            print(("OK   " if ok else "FAIL ") + f"{cls}.{field} = {off:#x} (want {want:#x}) line {lineno}")
            if not ok:
                problems.append(f"{cls}.{field}: dump says {off:#x}, code says {want:#x}")

    # Cross-check offsets.h constants.
    src = OFFSETS_H.read_text(encoding="utf-8")
    for name, want in CONSTS.items():
        m = re.search(rf"constexpr size_t {name}\s*=\s*(0x[0-9A-Fa-f]+)", src)
        if not m:
            problems.append(f"offsets.h missing {name}")
            continue
        got = int(m.group(1), 16)
        ok = got == want
        print(("OK   " if ok else "FAIL ") + f"offsets.h {name} = {got:#x}")
        if not ok:
            problems.append(f"offsets.h {name}={got:#x}, dump expects {want:#x}")

    if problems:
        print("\nproblems:")
        for p in problems:
            print("  -", p)
        return 1
    print("\nall offsets verified against dump.cs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
