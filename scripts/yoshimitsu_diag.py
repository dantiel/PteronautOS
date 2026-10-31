#!/usr/bin/env python3
"""
YOSHIMITSU — serial console + flash assist (stdlib only, no pyserial, no pip).

Modes:
  python3 scripts/yoshimitsu_diag.py            interactive console (type FLEA, STATUS, KINCHO, HELP…)
  python3 scripts/yoshimitsu_diag.py status     print the current stance, then exit
  python3 scripts/yoshimitsu_diag.py flea       auto: STATUS + FLEA, release the port, instruct

Why the old script "did nothing" / ENXIO: it opened the port with O_NONBLOCK,
which this TinyUSB CDC device rejects with ENXIO (errno 6 "Device not configured")
on the very first write.  A normal BLOCKING open (like a real terminal) works.
"""
import os, sys, glob, time, select, termios, errno

BAUD = 115200


def find_port():
    cands = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/tty.usbmodem*")
    return sorted(set(cands))[0] if cands else None


def open_raw(port):
    # BLOCKING open — deliberately NO O_NONBLOCK (fixes ENXIO on TinyUSB CDC).
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    iflag, oflag, cflag, lflag, ispeed, ospeed, cc = termios.tcgetattr(fd)
    iflag &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP
               | termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON)
    oflag &= ~termios.OPOST
    lflag &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
    cflag &= ~(termios.CSIZE | termios.PARENB)
    cflag |= termios.CS8
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, [iflag, oflag, cflag, lflag,
                                            termios.B115200, termios.B115200, cc])
    return fd


def read_all(fd, timeout=1.0):
    out = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                d = os.read(fd, 4096)
            except OSError:
                break
            if not d:
                break
            out += d
        elif out:
            break
    return out


def send(fd, line):
    os.write(fd, (line + "\n").encode())


def main():
    mode = "console"
    for a in sys.argv[1:]:
        if a in ("flea", "status", "loopback"):
            mode = a
            break

    port = find_port()
    if not port:
        print("No usbmodem port found. Connect the RP2040 and retry.")
        return 1

    print(f"opening {port} @ {BAUD} …", flush=True)
    try:
        fd = open_raw(port)
    except OSError as e:
        if e.errno == errno.EBUSY:
            print(f"ERROR: {port} is busy — a Serial Monitor still holds it.\n"
                  f"Close Arduino IDE's Serial Monitor (or: lsof {port}).", flush=True)
        else:
            print(f"ERROR opening {port}: {e}", flush=True)
        return 1
    time.sleep(0.2)

    try:
        if mode == "status":
            send(fd, "STATUS")
            print(read_all(fd, 2.0).decode("utf-8", "replace"), flush=True)

        elif mode == "flea":
            send(fd, "STATUS")
            print("--- current stance ---", flush=True)
            print(read_all(fd, 1.2).decode("utf-8", "replace"), flush=True)
            send(fd, "FLEA")
            print("--- FLEA ---", flush=True)
            print(read_all(fd, 2.5).decode("utf-8", "replace"), flush=True)
            print("=" * 72, flush=True)
            print("FLEA sent.  The bridge is now TRANSPARENT (console goes silent).", flush=True)
            print("The RP2040 is HOLDING the EP2's BOOT line LOW.", flush=True)
            print("", flush=True)
            print("NOW, in order:", flush=True)
            print("  1) power-cycle the EP2  (unplug its 5V, wait 10 s, replug)", flush=True)
            print("     → the EP2 drops into the ROM bootloader", flush=True)
            print("  2) ./scripts/flash.sh --device mushin --no-build", flush=True)
            print("  3) RESET the RP2040  (releases BOOT → boots KINCHO)", flush=True)
            print("  4) power-cycle the EP2 once more  → runs the new firmware", flush=True)
            print("=" * 72, flush=True)

        else:
            print("interactive console — type commands (FLEA, STATUS, KINCHO, HELP…), "
                  "Ctrl-C to exit.", flush=True)
            print("NOTE: after typing FLEA the console goes silent (transparent bridge) — "
                  "that is EXPECTED.", flush=True)
            while True:
                r, _, _ = select.select([fd, sys.stdin], [], [], 0.2)
                if fd in r:
                    try:
                        d = os.read(fd, 4096)
                    except OSError:
                        break
                    if not d:
                        break
                    sys.stdout.write(d.decode("utf-8", "replace"))
                    sys.stdout.flush()
                if sys.stdin in r:
                    line = sys.stdin.readline()
                    if not line:
                        break
                    os.write(fd, line.encode())
    except KeyboardInterrupt:
        pass
    finally:
        try:
            os.close(fd)
        except OSError:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())