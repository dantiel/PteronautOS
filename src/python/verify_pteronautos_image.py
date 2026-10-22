"""Fail closed if a PteronautOS receiver image loses its project hardware map.

Standard library only; safe to run in CI or before flashing. Never prints UID,
binding phrase, Wi-Fi credentials, or the options JSON.
"""
import argparse
import json
import struct
from pathlib import Path

HARDWARE_PATH = Path(__file__).resolve().parents[1] / "targets/hardware/pteronautos-pwmp7.json"


def derive_pwm_outputs(kind, gyro_on):
    """Canonical PteronautOS pin-map derivation, shared by the build and the verifier.

    Order is the natural (silkscreen / GPIO-ascending) order a hand reads across
    the board: CH1=GPIO0, CH2=GPIO1, CH3=GPIO3, CH7=GPIO9, GPIO10, GPIO5. The
    firmware's channel index maps straight onto the physical pad, so "channel 1 =
    CH1" needs no translation cipher. GPIO0 (BOOT strap, also the left-wing pad
    upstream) and GPIO5 are valid PWM pins and are exposed; GPIO2 is radio reset
    and must never appear.

    Gyro ON holds GPIO1/GPIO3 (the MPU6050 I²C SDA/SCL lines) so those two pads
    drop out of the middle, leaving [0, 9, 10, 5]. Gyro OFF frees them back in,
    restoring the full six-pad natural order. EP2 standalone drives its two
    broken-out UART pads; the MUSHIN companion has no local PWM at all.
    """
    if kind == "pwmp7":
        return [0, 9, 10, 5] if gyro_on else [0, 1, 3, 9, 10, 5]
    if kind == "ep2pwm":
        return [3, 1]
    return None


def verify_image(path, expected_pwm_outputs=None, expected_product=None):
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
    product = data[end:end + 128].split(b"\0", 1)[0]
    if product not in (b"PteronautOS PWMP7", b"PteronautOS EP2", b"PteronautOS EP2 PWM"):
        raise ValueError("Unexpected receiver product")
    if expected_product is not None and product != expected_product.encode():
        raise ValueError(f"Product {product!r} does not match expected {expected_product!r}")
    try:
        options = json.loads(data[end + 144:end + 656].split(b"\0", 1)[0])
        hardware = json.loads(data[end + 656:end + 2704].split(b"\0", 1)[0])
    except (ValueError, UnicodeError):
        raise ValueError("Invalid embedded options/hardware JSON") from None
    if product == b"PteronautOS EP2":
        layout = HARDWARE_PATH.with_name('pteronautos-ep2.json')
    elif product == b"PteronautOS EP2 PWM":
        layout = HARDWARE_PATH.with_name('pteronautos-ep2-pwm.json')
    else:
        layout = HARDWARE_PATH
    expected = json.loads(layout.read_text())
    if expected_pwm_outputs is not None:
        expected["pwm_outputs"] = expected_pwm_outputs
    if hardware != expected or hardware.get("radio_rst") != 2 or 2 in hardware.get("pwm_outputs", []):
        raise ValueError("Hardware differs from project map (GPIO2 is radio reset, not PWM)")
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
    parser.add_argument(
        "--pwm_outputs",
        help="Expected pwm_outputs (comma-separated); overrides the static project map.",
    )
    parser.add_argument(
        "--expect_product",
        help="Expected product name (e.g. 'PteronautOS EP2 PWM'); fails the build on mismatch.",
    )
    args = parser.parse_args()
    expected_pwm = None
    if args.pwm_outputs:
        expected_pwm = [int(x) for x in args.pwm_outputs.split(",") if x.strip() != ""]
    try:
        verify_image(
            args.image,
            expected_pwm_outputs=expected_pwm,
            expected_product=args.expect_product,
        )
    except (ValueError, OSError) as error:
        parser.exit(1, f"Firmware verification failed: {error}\n")
    print("Verified PteronautOS receiver: hardware, options, DOUT / 1 MB / 40 MHz")