#!/usr/bin/env python3
"""Check every IL2CPP class lookup in the native sources against dump.cs.

Catches the classic mistake of passing the wrong namespace: il2cpp_class_from_name
matches namespace + name exactly, so a wrong namespace silently returns null.

Usage: python check_lookups.py
Exit code 1 when a lookup cannot be satisfied.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
NATIVE = ROOT / "mobilebpp/native"
DUMP = pathlib.Path(__file__).resolve().parents[2] / "work/il2cpp-dump/dump.cs"

DECL = re.compile(
    r"^(?:public |internal |private |protected |sealed |abstract |static |partial |readonly )*"
    r"(class|struct|enum|interface) ([A-Za-z0-9_.]+)"
)
CALL = re.compile(r'\b(Klass|KlassAny|KlassByName)\(([^;]*?)\)\s*;')


def load_types():
    types = set()      # (ns, name)
    by_name = {}       # name -> set(ns)
    cur = None
    lines = DUMP.read_text(encoding="utf-8", errors="replace").split("\n")
    for i, line in enumerate(lines):
        m = DECL.match(line)
        if m:
            name = m.group(2)
            ns = ""
            for j in range(i - 1, max(0, i - 40), -1):
                s = lines[j]
                if s.startswith("// Namespace: "):
                    ns = s[len("// Namespace: "):].strip()
                    break
                if s.startswith("// Image "):
                    break
            types.add((ns, name))
            by_name.setdefault(name, set()).add(ns)
    return types, by_name


def main():
    types, by_name = load_types()
    print(f"dump types: {len(types)}")
    problems = []
    seen = set()
    for src in sorted(NATIVE.glob("*.cpp")) + sorted(NATIVE.glob("*.h")):
        text = src.read_text(encoding="utf-8", errors="replace")
        for m in CALL.finditer(text):
            kind, raw = m.group(1), m.group(2)
            args = re.findall(r'"([^"]*)"', raw)
            if not args:
                continue
            key = (kind, tuple(args))
            if key in seen:
                continue
            seen.add(key)
            if kind == "Klass" and len(args) == 3:
                image, ns, name = args
                ok = (ns, name) in types
                detail = f"{ns}.{name} in {image}"
            elif kind == "KlassAny" and len(args) == 2:
                ns, name = args
                ok = (ns, name) in types or (name in by_name)
                detail = f"{ns}.{name}" + (
                    "" if (ns, name) in types else f" (fallback: namespaces {sorted(by_name.get(name, []))})"
                )
            elif kind == "KlassByName" and len(args) == 1:
                name = args[0]
                ok = name in by_name
                detail = name
            else:
                continue
            print(("OK   " if ok else "FAIL ") + f"{kind}({', '.join(args)}) -> {detail}")
            if not ok:
                problems.append(detail)
    if problems:
        print("\nunsatisfied lookups:")
        for p in problems:
            print("  -", p)
        return 1
    print("\nall class lookups satisfied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
