"""Fail closed if a PteronautOS PWMP7 image loses its project hardware map.

Standard library only; safe to run in CI or before flashing. Never prints UID,
binding phrase, Wi-Fi credentials, or the options JSON.
"""
import argparse
import json
import struct
from pathlib import Path

HARDWARE_PATH = Path(__file__).resolve().parents[1] / "targets/hardware/pteronautos-pwmp7.json"


def verify_image(path):
    data = Path(path).read_bytes()
    if len(data) < 0x1008 or data[:2] != b"\xe9\x02":
        raise ValueError("Not a complete ESP8285 image at offset zero")
    if data[2] != 3 or data[3] != 0x20:
        raise ValueError("Expected DOUT, 1 MB, 40 MHz flash header")
    if data[0x1000] != 0xe9:
        raise ValueError("Missing ESP8285 application header")
    pos = 0x1008
    for _ in range(data[0x1001]):
        if pos + 8 > len(data):
            raise ValueError("Truncated application segment header")
        size = struct.unpack_from("<I", data, pos + 4)[0]
        pos += 8 + size
        if pos > len(data):
            raise ValueError("Truncated application segment")
    end = (pos + 16) & ~15
    if end + 128 + 16 + 512 + 2048 > len(data):
        raise ValueError("Missing embedded product/options/hardware blocks")
    if data[end:end + 128].split(b"\0", 1)[0] != b"PteronautOS PWMP7":
        raise ValueError("Unexpected receiver product")
    try:
        options = json.loads(data[end + 144:end + 656].split(b"\0", 1)[0])
        hardware = json.loads(data[end + 656:end + 2704].split(b"\0", 1)[0])
    except (ValueError, UnicodeError):
        raise ValueError("Invalid embedded options/hardware JSON") from None
    expected = json.loads(HARDWARE_PATH.read_text())
    if hardware != expected or hardware.get("radio_rst") != 2 or 2 in hardware.get("pwm_outputs", []):
        raise ValueError("Hardware differs from project PWMP7 map (GPIO2 is radio reset, not PWM)")
    if not isinstance(options, dict):
        raise ValueError("Options must be a JSON object")
    interval = options.get("wifi-on-interval", -1)
    if type(interval) is not int or not -1 <= interval <= 2147483:
        raise ValueError("Invalid Wi-Fi fallback interval")
    if "uid" in options and (not isinstance(options["uid"], list) or
                             len(options["uid"]) != 6 or
                             any(type(v) is not int or not 0 <= v <= 255 for v in options["uid"])):
        raise ValueError("Invalid binding UID")
    return options


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    try:
        verify_image(args.image)
    except (ValueError, OSError) as error:
        parser.exit(1, f"Firmware verification failed: {error}\n")
    print("Verified PteronautOS PWMP7: hardware, options, DOUT / 1 MB / 40 MHz")
