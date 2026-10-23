#!/usr/bin/env python3
"""Static self-check: every game symbol referenced by the mod must exist in dump.cs.

Catches typos before a device round-trip. Exit code 1 on any miss.
"""
import re
import sys
import pathlib

DUMP = pathlib.Path(__file__).resolve().parents[2] / "work/il2cpp-dump/dump.cs"

# (kind, symbol)
CHECKS = [
    ("class", "JsonGameDataManager"),
    ("class", "CardController"),
    ("class", "CardTooltipData"),
    ("enum", "BazaarBoard.ESections"),
    ("class", "EventSystem"),
    ("class", "StandaloneInputModule"),
    ("class", "CanvasGroup"),
    ("class", "Canvas"),
    ("class", "GameObject"),
    ("method", "GetStatic"),
    ("method", "IsManagerCreated"),
    ("method", "GetCardMap"),
    ("method", "get_Id"),
    ("method", "get_InternalName"),
    ("method", "get_InternalDescription"),
    ("method", "get_Type"),
    ("method", "get_Size"),
    ("method", "get_StartingTier"),
    ("method", "get_Heroes"),
    ("method", "get_Localization"),
    ("method", "get_Title"),
    ("method", "get_Text"),
    ("method", "get_touchCount"),
    ("method", "GetTouch"),
    ("method", "get_height"),
    ("method", "set_timeScale"),
    ("method", "FindObjectsOfType"),
    ("method", "AddComponent"),
    ("method", "RenderTooltip"),
    ("method", "get_blocksRaycasts"),
    ("method", "set_blocksRaycasts"),
    ("method", "get_gameObject"),
]


def main():
    text = DUMP.read_text(encoding="utf-8", errors="replace")
    misses = []
    for kind, sym in CHECKS:
        if kind == "class":
            pat = rf"class {re.escape(sym)}\b"
        elif kind == "enum":
            pat = rf"enum {re.escape(sym)}\b"
        else:
            pat = rf"\b{re.escape(sym)}\("
        if re.search(pat, text):
            print(f"OK   {kind:6s} {sym}")
        else:
            print(f"MISS {kind:6s} {sym}")
            misses.append(sym)
    if misses:
        print("\nmissing:", ", ".join(misses))
        return 1
    print("\nall symbols present")
    return 0


if __name__ == "__main__":
    sys.exit(main())
