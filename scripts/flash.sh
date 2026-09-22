#!/usr/bin/env bash
# =============================================================================
# PteronautOS Flash Script — build (WebUI + firmware) then flash.
# -----------------------------------------------------------------------------
# Auto-discovers: esptool (PlatformIO-bundled), firmware binary, USB port.
# Verifies the chip is in bootloader mode, then flashes without auto-reset.
#
# Languages: the WebUI ships 11 locales baked into the firmware. A smaller
# locale set is a cheaper first load on the ESP8285's ~23 KB heap.
#
#   Usage:
#     ./scripts/flash.sh                       # interactive: pick languages
#     ./scripts/flash.sh --lang de             # bake German only, build + flash
#     ./scripts/flash.sh -l de,en              # German + English
#     ./scripts/flash.sh -l all                # all 11 languages
#     ./scripts/flash.sh --no-build            # skip rebuild, flash existing .bin
#     ./scripts/flash.sh --check               # check bootloader only
#     ./scripts/flash.sh --port /dev/cu.usbserial-XXX
#     ./scripts/flash.sh --list-langs          # list available language codes
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

# ---- Language catalog (mirrors the i18n-locales-plugin locale set) ----------
ALL_LANGS=(en pt de es fr hi ja ko ru zh ar)
declare -A LANG_NAMES=(
  [en]="English (en)"
  [pt]="Português (pt)"
  [de]="Deutsch (de)"
  [es]="Español (es)"
  [fr]="Français (fr)"
  [hi]="हिन्दी (hi)"
  [ja]="日本語 (ja)"
  [ko]="한국어 (ko)"
  [ru]="Русский (ru)"
  [zh]="中文 (zh)"
  [ar]="العربية (ar)"
)

# ---- Usage ------------------------------------------------------------------
usage() {
  say "PteronautOS Flash Script — build WebUI + firmware, then flash."
  say ""
  say "Languages (WebUI locales baked into the firmware):"
  say "  -l, --lang <codes>   comma-separated locale codes, or 'all'"
  say "                       (e.g. -l de   or   -l de,en   or   -l all)"
  say "                       omitted → interactive prompt (all / some / skip)"
  say ""
  say "Build control:"
  say "  --no-build           skip WebUI+firmware rebuild, flash existing .bin"
  say ""
  say "Flash options:"
  say "  --port <device>      force a USB port"
  say "  --baud <rate>        serial rate (default 115200, try 9600 if flaky)"
  say "  --check              check bootloader (chip id) only, no build/flash"
  say ""
  say "Info:"
  say "  --list-langs         list available language codes and exit"
  say "  --help, -h           show this help"
}

# ---- Argument parsing -------------------------------------------------------
LANG_SEL=""        # raw --lang value ("" = not given → interactive)
BUILD=1            # build WebUI + firmware before flashing
CHECK_ONLY=0
LIST_LANGS=0
PORT=""
BAUD=115200

while [[ $# -gt 0 ]]; do
  case "$1" in
    -l|--lang)     LANG_SEL="$2"; shift 2 ;;
    --no-build)    BUILD=0; shift ;;
    --check)       CHECK_ONLY=1; BUILD=0; shift ;;
    --list-langs)  LIST_LANGS=1; shift ;;
    --port)        PORT="$2"; shift 2 ;;
    --baud)        BAUD="$2"; shift 2 ;;
    --help|-h)     usage; exit 0 ;;
    *) err "Unknown argument: $1"; usage; exit 1 ;;
  esac
done

# ---- Info: list languages ---------------------------------------------------
if [[ "$LIST_LANGS" == "1" ]]; then
  say "${C_BOLD}Available languages${C_RESET} (use with -l / --lang, comma-separated):"
  for code in "${ALL_LANGS[@]}"; do
    printf '  %-4s %s\n' "$code" "${LANG_NAMES[$code]}"
  done
  say ""
  say "Examples:  -l de      -l de,en      -l all"
  exit 0
fi

# ---- Normalize a comma-separated locale list → canonical codes --------------
normalize_langs() {
  local input="$1"
  input="$(echo "$input" | tr '[:upper:]' '[:lower:]')"
  if [[ "$input" == "all" ]]; then
    echo "all"; return 0
  fi
  local out="" code
  for code in $(echo "$input" | tr ',' ' '); do
    code="${code//[[:space:]]/}"
    [[ -z "$code" ]] && continue
    if [[ " ${ALL_LANGS[*]} " == *" $code "* ]]; then
      out="${out}${out:+,}$code"
    else
      warn "Unknown language code '$code' — ignored." >&2
    fi
  done
  if [[ -z "$out" ]]; then
    err "No valid language codes in '$input'." >&2
    say "Known codes: ${ALL_LANGS[*]}  (or use 'all')" >&2
    return 1
  fi
  echo "$out"
}

# ---- Interactive language selection -----------------------------------------
prompt_languages() {
  say "" >&2
  say "${C_BOLD}No language selected.${C_RESET} How should the WebUI be baked in?" >&2
  say "" >&2
  say "  ${C_BOLD}1${C_RESET}  All ${#ALL_LANGS[@]} languages  (biggest payload — default)" >&2
  say "  ${C_BOLD}2${C_RESET}  A selection  (comma-separated codes, e.g. de,en)" >&2
  say "  ${C_BOLD}3${C_RESET}  Skip rebuild   (flash existing firmware.bin as-is)" >&2
  say "" >&2
  local choice
  while true; do
    printf 'Choose [1/2/3] (default: 1) > ' >&2
    read -r choice || { say "" >&2; echo "all"; return 0; }   # EOF → default
    choice="${choice:-1}"
    case "$choice" in
      1) echo "all"; return 0 ;;
      3) echo "__SKIP__"; return 0 ;;
      2)
        local sel
        printf 'Languages (e.g. de,en) > ' >&2
        read -r sel || { echo "all"; return 0; }
        if [[ -z "${sel//[[:space:]]/}" ]]; then
          warn "Empty selection — using all languages." >&2
          echo "all"; return 0
        fi
        echo "$sel"; return 0 ;;
      *) warn "Please enter 1, 2 or 3." >&2 ;;
    esac
  done
}

# ---- Build pipeline ---------------------------------------------------------
build_webui() {
  local list="$1"
  step "Building WebUI"
  (
    cd "$PROJECT_ROOT/src/html" || exit 1
    # Load nvm (if present) and switch to the project's Node version (.nvmrc).
    local NVM_DIR="${NVM_DIR:-$HOME/.nvm}"
    if [[ -s "$NVM_DIR/nvm.sh" ]]; then
      # shellcheck disable=SC1091
      . "$NVM_DIR/nvm.sh" >/dev/null 2>&1 || true
      nvm use >/dev/null 2>&1 || true   # reads src/html/.nvmrc
    fi
    say "${C_DIM}Using Node $(node --version 2>/dev/null || echo '?') for the WebUI build.${C_RESET}" >&2

    if [[ "$list" == "all" || -z "$list" ]]; then
      ok "Baking in all ${#ALL_LANGS[@]} languages (I18N_LOCALES unset)."
      npm run build:pteronautos
    else
      ok "Baking in languages: ${list}."
      I18N_LOCALES="$list" npm run build:pteronautos
    fi
  )
}

build_firmware() {
  step "Building firmware"
  (
    cd "$PROJECT_ROOT/src" || exit 1
    pio run -e PteronautOS_ESP8285_2400_RX
  )
}

# ---- Resolve language + build ----------------------------------------------
if [[ "$BUILD" == "1" ]]; then
  if [[ -n "$LANG_SEL" ]]; then
    if ! LANGLIST="$(normalize_langs "$LANG_SEL")"; then
      exit 1
    fi
  else
    local_raw="$(prompt_languages)"
    if [[ "$local_raw" == "__SKIP__" ]]; then
      BUILD=0
      warn "Skipping rebuild — flashing the existing firmware.bin."
    else
      LANGLIST="$(normalize_langs "$local_raw")"
    fi
  fi

  if [[ "$BUILD" == "1" ]]; then
    build_webui "${LANGLIST:-}"
    build_firmware
  fi
fi

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
FW=""
for cand in \
  "src/.pio/build/PteronautOS_ESP8285_2400_RX/firmware.bin" \
  "$(find src/.pio/build -name firmware.bin 2>/dev/null | head -1)" ; do
  [[ -n "$cand" && -f "$cand" ]] && { FW="$cand"; break; }
done

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

# ---- 4) Check mode (no firmware needed, no flash) ---------------------------
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

# ---- 5) Firmware must exist for flashing ------------------------------------
step "Locating firmware binary"
if [[ -z "$FW" ]]; then
  err "No firmware.bin found. Build first:  ./scripts/flash.sh --lang <code>  (or pio run -e PteronautOS_ESP8285_2400_RX)"
  exit 1
fi
FW_SIZE=$(stat -f%z "$FW" 2>/dev/null || stat -c%s "$FW" 2>/dev/null || echo "?")
ok "Firmware: $C_DIM$FW$C_RESET  ($FW_SIZE B)"
python3 "$PROJECT_ROOT/src/python/verify_pteronautos_image.py" "$FW"

# ---- 6) Flash (single connection, no pre-probe) -----------------------------
# NOTE: we intentionally do NOT probe chip_id before flashing in normal mode.
# A separate chip_id probe opens an extra serial connection that can disturb
# the bootloader ROM on marginal FTDI setups. write_flash does its own
# sync + connect + verify in a single connection.

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
