#!/usr/bin/env python3
"""Verify that every IL2CPP inline hook in the native sources matches the
managed method it patches.

IL2CPP compiles an instance method as
    Ret Method(Class* __this, const MethodInfo* method, <params...>)
so a correct C++ hook must declare exactly params + 2 pointer parameters, and
must return void* (not void) whenever the managed method returns a value.

Usage: python check_hooks.py
Exit code 1 on any mismatch.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
NATIVE = ROOT / "mobilebpp/native"
DUMP = pathlib.Path(__file__).resolve().parents[2] / "work/il2cpp-dump/dump.cs"

# (dump owner class, method, expected param count, managed return kind, c++ hook name)
HOOKS = [
    ("EventSystem", "Update", 0, "void", "TickHook"),
    ("CardTooltipData", "RenderTooltip", 1, "value", "RenderTooltipHook"),
]

DECL = re.compile(
    r"^(?:public |internal |private |protected |sealed |abstract |static |partial |readonly )*"
    r"(class|struct|enum|interface) ([A-Za-z0-9_.]+)"
)


def split_top(s, sep=","):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "<([{":
            depth += 1
        elif ch in ">)]}":
            depth -= 1
        if ch == sep and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return out


def find_method(lines, cls, name):
    start = None
    for i, line in enumerate(lines):
        m = DECL.match(line)
        if m and m.group(2) == cls:
            start = i
            break
    if start is None:
        return None
    for i in range(start + 1, min(len(lines), start + 3000)):
        line = lines[i]
        if line.startswith("// Namespace: ") or line.startswith("// Image "):
            break
        if re.search(rf"\b{re.escape(name)}\s*\(", line):
            modifiers = r"(?:(?:public|private|protected|internal|static|virtual|override|sealed|abstract|new|extern|unsafe|async|partial|readonly)\s+)*"
            m = re.match(rf"^\s*(?:\[[^\]]*\]\s*)*{modifiers}(?P<ret>[\w\.<>\?\[\]]+)\s+{re.escape(name)}\s*\((?P<params>.*)\)\s*\{{", line)
            if not m:
                continue
            ret = m.group("ret").strip()
            params = split_top(m.group("params"))
            if not params or params == [""]:
                params = []
            return ret, params, i + 1
    return None


def cpp_hook_params(src, hook_name):
    m = re.search(rf"^[\w\*\s]+?\b{re.escape(hook_name)}\s*\(([^)]*)\)\s*\{{", src, re.M)
    if not m:
        return None
    params = [p.strip() for p in split_top(m.group(1)) if p.strip()]
    return params


def main():
    lines = DUMP.read_text(encoding="utf-8", errors="replace").split("\n")
    srcs = {p.name: p.read_text(encoding="utf-8", errors="replace") for p in NATIVE.glob("*.cpp")}
    problems = []
    for cls, name, want_params, ret_kind, hook in HOOKS:
        found = find_method(lines, cls, name)
        if not found:
            problems.append(f"{cls}.{name}: not found in dump")
            continue
        ret, params, lineno = found
        print(f"dump  {cls}.{name} -> returns '{ret}', {len(params)} param(s) [line {lineno}]")
        if len(params) != want_params:
            problems.append(f"{cls}.{name}: dump has {len(params)} params, table says {want_params}")
        is_void = ret == "void"
        if ret_kind == "void" and not is_void:
            problems.append(f"{cls}.{name}: expected void, dump says {ret}")
        if ret_kind == "value" and is_void:
            problems.append(f"{cls}.{name}: expected a value return, dump says void")

        hook_src = next((s for s in srcs.values() if re.search(rf"\b{hook}\s*\(", s)), None)
        if hook_src is None:
            problems.append(f"{hook}: hook implementation not found")
            continue
        hp = cpp_hook_params(hook_src, hook)
        if hp is None:
            problems.append(f"{hook}: cannot parse signature")
            continue
        print(f"cpp   {hook}({', '.join(hp)})")
        if len(hp) != want_params + 2:
            problems.append(
                f"{hook}: {len(hp)} params, expected {want_params + 2} (this + MethodInfo + {want_params})"
            )
        cpp_ret = re.search(rf"^([\w\*\s]+?)\b{re.escape(hook)}\s*\(", hook_src, re.M).group(1).strip()
        cpp_ret = cpp_ret.split()[-1]  # drop storage/static qualifiers
        print(f"      return type: {cpp_ret}")
        if ret_kind == "value" and cpp_ret != "void*":
            problems.append(f"{hook}: managed method returns a value, hook must return void* (is '{cpp_ret}')")
        if ret_kind == "void" and cpp_ret != "void":
            problems.append(f"{hook}: managed method returns void, hook should return void (is '{cpp_ret}')")

    # C-level EGL hooks: fixed well-known signatures.
    egl_src = srcs.get("ui.cpp", "")
    for hook, args in (("HookSwap", 2), ("HookSwapDamage", 4), ("HookGetProcAddress", 1)):
        hp = cpp_hook_params(egl_src, hook)
        if hp is None:
            problems.append(f"{hook}: not found in ui.cpp")
            continue
        ok = len(hp) == args
        print(("OK   " if ok else "FAIL ") + f"egl {hook}({', '.join(hp)})")
        if not ok:
            problems.append(f"{hook}: {len(hp)} params, expected {args}")

    if problems:
        print("\nproblems:")
        for p in problems:
            print("  -", p)
        return 1
    print("\nall hook ABIs consistent with dump.cs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
