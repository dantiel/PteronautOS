#!/usr/bin/env python3
"""Negative tests for the YOSHIMITSU compile-time pin guards.

Edits values INSIDE the BOARD_CUSTOM block (the single source of truth),
then expects the matching #error to fire. Run from the repo root:

    python3 tools/yoshi_guard_test.py
"""
import re, subprocess, sys

CASES = [
    # (sketch, custom-block marker, key, value, expected #error needle)
    ("sketches/rp2040_tiny_yoshimitsu/rp2040_tiny_yoshimitsu.ino", "BOARD_CUSTOM",
     "CRSF_RX_PIN", "0", "PIN COLLISION: CRSF_TX_PIN and CRSF_RX_PIN share one GPIO"),
    ("sketches/rp2040_tiny_yoshimitsu/rp2040_tiny_yoshimitsu.ino", "BOARD_CUSTOM",
     "CRSF_UART_NUM", "7", "CRSF_UART_NUM must be 0"),
    ("sketches/rp2040_tiny_yoshimitsu/rp2040_tiny_yoshimitsu.ino", "BOARD_CUSTOM",
     "CRSF_TX_PIN", "4", "UART0 TX must be GP0, GP12, GP16 or GP28"),
    ("sketches/rp2040_tiny_yoshimitsu/rp2040_tiny_yoshimitsu.ino", "BOARD_CUSTOM",
     "RGB_LED_PIN", "31", "RGB_LED_PIN outside RP2040 GPIO range"),
    ("sketches/esp32s3_yoshimitsu/esp32s3_yoshimitsu.ino", "BOARD_CUSTOM",
     "BRIDGE_TX_PIN", "18", "PIN COLLISION: BRIDGE TX and RX share one GPIO"),
    ("sketches/esp32s3_yoshimitsu/esp32s3_yoshimitsu.ino", "BOARD_CUSTOM",
     "CRSF_UART_RX_PIN", "0", "CRSF_UART_RX_PIN invalid"),
]

def run_case(sketch, marker, key, value, needle):
    txt = open(sketch).read()
    m = re.search(rf"(#ifdef {marker}\n)(.*?)(\n#endif\n)", txt, re.S)
    assert m, f"{marker} block not found in {sketch}"
    b2 = re.sub(rf"(#define {key}\s+)\S+", rf"\g<1>{value}", m.group(2))
    if b2 == m.group(2):
        print(f"SKIP: {key} not editable in {marker} block of {sketch}")
        return None
    out = f"/tmp/yoshi_guard_{abs(hash((sketch, key, value)))}.ino"
    open(out, "w").write(txt[:m.start(2)] + b2 + txt[m.end(2):])
    r = subprocess.run(["clang++", "-fsyntax-only", "-std=gnu++17",
                        "-I", "tools/stub", f"-D{marker}", "-x", "c++", out],
                       capture_output=True, text=True)
    ok = needle in r.stderr
    print(f"{'PASS' if ok else 'FAIL'} {sketch.split('/')[-1]} · {key}={value}: "
          f"'{needle}' " + ("fired" if ok else "NOT fired"))
    if not ok:
        print("   stderr head:", r.stderr[:400].replace("\n", " | "))
    return ok

if __name__ == "__main__":
    results = [run_case(*c) for c in CASES]
    fails = [r for r in results if r is False]
    print(f"\n{len(results) - len(fails)}/{len(results)} guards fired correctly")
    sys.exit(1 if fails else 0)
