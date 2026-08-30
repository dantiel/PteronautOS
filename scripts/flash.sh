#!/usr/bin/env bash
# =============================================================================
# PteronautOS Flash Script
# ------------------------
# Auto-discovers: esptool (PlatformIO-bundled), firmware binary, USB port.
# Verifies that the chip is in bootloader mode, then flashes without auto-reset.
#
#   Usage:
#     ./scripts/flash.sh                        # fully automatic
#     ./scripts/flash.sh --port /dev/cu.usbserial-XXX   # force a port
#     ./scripts/flash.sh --check                # check only, don't flash
# =============================================================================
set -euo pipefail

# ---- Project root (script lives in scripts/) --------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# ---- Colors -----------------------------------------------------------------
C_RESET=$'\033[0m'; C_BOLD=$'\033[1m'
C_OK=$'\033[32m'; C_WARN=$'\033[33m'; C_ERR=$'\033[31m'; C_DIM=$'\033[2m'

say()   { printf '%s\n' "$*"; }
ok()    { printf '%s✓%s %s\n' "$C_OK" "$C_RESET" "$*"; }
warn()  { printf '%s⚠%s  %s\n' "$C_WARN" "$C_RESET" "$*"; }
err()   { printf '%s✗%s %s\n' "$C_ERR" "$C_RESET" "$*"; }
step()  { printf '\n%s==>%s %s%s%s\n' "$C_BOLD" "$C_RESET" "$C_BOLD" "$*" "$C_RESET"; }

# ---- Arguments --------------------------------------------------------------
CHECK_ONLY=0
PORT=""
BAUD=115200
while [[ $# -gt 0 ]]; do
  case "$1" in
    --port)   PORT="$2"; shift 2 ;;
    --baud)   BAUD="$2"; shift 2 ;;
    --check)  CHECK_ONLY=1; shift ;;
    --help|-h)
      say "PteronautOS Flash Script"
      say "  --port <device>   force a USB port"
      say "  --baud <rate>     serial rate (default 115200, try 9600 if flaky)"
      say "  --check           check only (chip id), don't flash"
      exit 0 ;;
    *) err "Unknown argument: $1"; exit 1 ;;
  esac
done

# ---- 1) Locate esptool (test candidates, take the first that runs) ----------
step "Locating a working esptool"

CANDIDATES=(
  "$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
  "$HOME/.platformio/packages/tool-esptoolpy@1.30000.201119/esptool.py"
  "$HOME/.platformio/packages/framework-arduinoespressif8266/tools/esptool/esptool.py"
)
# Also via glob, in case the version changes
while IFS= read -r f; do
  [[ -n "$f" ]] && CANDIDATES+=("$f")
done < <(find "$HOME/.platformio/packages" -maxdepth 2 -name "esptool.py" 2>/dev/null | sort -V)

ESPT=""
ESPT_VER=""
seen=""
for c in "${CANDIDATES[@]}"; do
  [[ -z "$c" || ! -f "$c" ]] && continue
  # Avoid duplicates
  [[ " $seen " == *" $c "* ]] && continue
  seen="$seen $c "
  v="$("$c" version 2>/dev/null | grep -i 'esptool' | head -1 || true)"
  if [[ -n "$v" ]]; then
    ESPT="$c"
    ESPT_VER="$v"
    break
  fi
done

if [[ -z "$ESPT" ]]; then
  err "No working esptool found (system esptool.py has a broken Python 2 shebang)."
  exit 1
fi
ok "esptool found: $C_DIM${ESPT#$HOME/}$C_RESET  ($ESPT_VER)"

# ---- 2) Locate firmware -----------------------------------------------------
step "Locating firmware binary"

FW=""
for cand in \
  "src/.pio/build/PteronautOS_ESP8285_2400_RX/firmware.bin" \
  "$(find src/.pio/build -name firmware.bin 2>/dev/null | head -1)" ; do
  [[ -n "$cand" && -f "$cand" ]] && { FW="$cand"; break; }
done

if [[ -z "$FW" ]]; then
  err "No firmware.bin found. Build first:  pio run -e PteronautOS_ESP8285_2400_RX"
  exit 1
fi
FW_SIZE=$(stat -f%z "$FW" 2>/dev/null || stat -c%s "$FW" 2>/dev/null || echo "?")
ok "Firmware: $C_DIM$FW$C_RESET  ($FW_SIZE B)"

# ---- 3) Locate USB port -----------------------------------------------------
step "Locating USB serial port"

if [[ -n "$PORT" ]]; then
  [[ -e "$PORT" ]] || { err "Port does not exist: $PORT"; exit 1; }
else
  # Only real USB serial ports (exclude Bluetooth/debug/wlan)
  PORT=$(ls /dev/cu.* 2>/dev/null | grep -Ev 'Bluetooth|debug|wlan' | head -1 || true)
  [[ -n "$PORT" ]] || { err "No USB serial port found. Plug in the FTDI adapter."; exit 1; }
fi
ok "Port: $C_DIM$PORT$C_RESET"

# ---- 4) Flash (single connection, no pre-probe) -----------------------------
# NOTE: we intentionally do NOT probe chip_id before flashing in normal mode.
# A separate chip_id probe opens an extra serial connection that can disturb
# the bootloader ROM on marginal FTDI setups. write_flash does its own
# sync + connect + verify in a single connection.

if [[ "$CHECK_ONLY" == "1" ]]; then
  step "Checking bootloader (chip id)"
  CHIP=$("$ESPT" --chip esp8266 --port "$PORT" --baud "$BAUD" --before no_reset chip_id 2>&1 | grep -oE 'Chip ID: 0x[0-9a-fA-F]+' | head -1 || true)
  if [[ -z "$CHIP" ]]; then
    warn "No chip detected in bootloader mode."
    cat <<'EOF'
  Boot cycle (FTDI has NO auto-reset):
    1. HOLD BOOT (GPIO0)
    2. Unplug FTDI/USB → plug it back in
    3. Keep holding ~1 s, then release BOOT
  Then run again: ./scripts/flash.sh --check
EOF
    exit 1
  fi
  ok "$CHIP"
  say "\n${C_OK}Chip is ready.${C_RESET} (--check only, no flash.)"
  exit 0
fi

step "Flashing firmware"

say "${C_DIM}Tip: disconnect servos / external load from the FTDI 3.3V rail while${C_RESET}"
say "${C_DIM}flashing — the ESP draws more current during flash and can brown out.${C_RESET}"
say ""

FLASH_OPTS=(--chip esp8266 --port "$PORT" --baud "$BAUD" \
  --before no_reset --after no_reset write_flash \
  --flash_mode dout --flash_size 1MB --flash_freq 40m \
  0x0 "$FW")

for attempt in 1 2 3; do
  if [[ $attempt -gt 1 ]]; then
    warn "Attempt $attempt (previous attempt failed to connect)..."
  fi
  if "$ESPT" "${FLASH_OPTS[@]}"; then
    ok "Flash complete."
    say "${C_DIM}Afterwards: unplug FTDI → plug back in (do NOT hold GPIO0) for normal boot.${C_RESET}"
    exit 0
  fi
  [[ $attempt -lt 3 ]] && sleep 1
done

err "Flash failed after 3 attempts."
cat <<'EOF'
  Possible causes & fixes:
    1. Chip not cleanly in bootloader → redo the boot cycle:
       HOLD BOOT (GPIO0) → unplug FTDI → plug in → hold ~1s → release BOOT.
    2. Servos/external load sagging the 3.3V rail → disconnect them, retry.
    3. Marginal serial → try a lower baud:  ./scripts/flash.sh --baud 9600
EOF
exit 1