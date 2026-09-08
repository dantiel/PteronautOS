#!/usr/bin/env python3
"""Negative tests for the YOSHIMITSU compile-time guards.

Edits values INSIDE the BOARD_CUSTOM block (the single source of truth),
then expects the matching #error to fire. Run from the repo root:

    python3 tools/yoshi_guard_test.py

Note: pin RANGE and COLLISION checks are a runtime boot POST (validatePins()),
not #error — only the platform and UART-number invariants are compile-time.
"""
import re, subprocess, sys

SKETCH = "sketches/yoshimitsu/yoshimitsu.ino"

CASES = [
    # (key, value, expected #error needle, extra defines)
    ("CRSF_UART_NUM", "7", "CRSF_UART_NUM must be 0 or 1", ["-DARDUINO_ARCH_RP2040"]),
    ("BRIDGE_UART_NUM", "3", "BRIDGE_UART_NUM must be 0 or 1", ["-DARDUINO_ARCH_RP2040"]),
    ("CRSF_UART_NUM", "1", "CRSF and BRIDGE need two DIFFERENT UARTs", ["-DARDUINO_ARCH_RP2040"]),
]

def run_case(key, value, needle, defines):
    txt = open(SKETCH).read()
    m = re.search(r"(#ifdef BOARD_CUSTOM\n)(.*?)(\n#endif\n)", txt, re.S)
    assert m, "BOARD_CUSTOM block not found"
    b2 = re.sub(rf"(#define {key}\s+)\S+", rf"\g<1>{value}", m.group(2))
    if b2 == m.group(2):
        print(f"SKIP: {key} not editable in BOARD_CUSTOM block")
        return None
    out = f"/tmp/yoshi_guard_{abs(hash((key, value)))}.ino"
    open(out, "w").write(txt[:m.start(2)] + b2 + txt[m.end(2):])
    r = subprocess.run(["clang++", "-fsyntax-only", "-std=gnu++17", "-I", "tools/stub",
                        "-DBOARD_CUSTOM", *defines, "-x", "c++", out],
                       capture_output=True, text=True)
    ok = needle in r.stderr
    print(f"{'PASS' if ok else 'FAIL'} {key}={value}: '{needle}' "
          + ("fired" if ok else "NOT fired"))
    if not ok:
        print("   stderr head:", r.stderr[:400].replace("\n", " | "))
    return ok

def run_platform_guard():
    out = "/tmp/yoshi_guard_platform.ino"
    open(out, "w").write(open(SKETCH).read())
    r = subprocess.run(["clang++", "-fsyntax-only", "-std=gnu++17", "-I", "tools/stub",
                        "-x", "c++", out], capture_output=True, text=True)
    needle = "YOSHIMITSU targets ESP32-S3 or RP2040 only"
    ok = needle in r.stderr
    print(f"{'PASS' if ok else 'FAIL'} no-arch: '{needle}' "
          + ("fired" if ok else "NOT fired"))
    if not ok:
        print("   stderr head:", r.stderr[:400].replace("\n", " | "))
    return ok

if __name__ == "__main__":
    results = [run_case(*c) for c in CASES] + [run_platform_guard()]
    fails = [r for r in results if r is False]
    print(f"\n{len(results) - len(fails)}/{len(results)} guards fired correctly")
    sys.exit(1 if fails else 0)
