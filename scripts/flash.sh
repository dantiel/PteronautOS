#!/usr/bin/env bash
# =============================================================================
# PteronautOS Flash Script — build (WebUI + firmware) then flash.
# -----------------------------------------------------------------------------
# A command-line counterpart to the browser Fossil Etcher (docs/flasher). It
# walks the same configuration surface as the web flasher + cloud-build.yml:
# device (PWMP7 / EP2-PWM / MUSHIN), mixer profile (0..12), regulatory domain,
# Zephyrus gyro, binding phrase, WiFi fallback, I²C pins, CRSF baud, locales.
#
# Builds by writing src/user_defines.txt (gitignored) + PLATFORMIO_BUILD_FLAGS,
# then verifies the image with --expect_product before flashing — the exact
# same pipeline as the GitHub Actions cloud build, run locally.
#
# Interactive (no flags) → a guided wizard. Every option can also be passed
# directly as a flag for scripting.
#
#   Usage:
#     ./scripts/flash.sh                        # wizard: device → mixer → domain → gyro → langs
#     ./scripts/flash.sh --device mushin --mixer 5
#     ./scripts/flash.sh --mixer 12 --gyro rate
#     ./scripts/flash.sh --lang de,en --no-build --port /dev/cu.usbserial-XXX
#     ./scripts/flash.sh --check                # bootloader probe only
#     ./scripts/flash.sh --list-mixers --list-devices
#
# Legacy aliases still work: --target pwmp7|ep2|ep2-pwm, --mode none|rate|horizon.
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

# ---- Catalogs -----------------------------------------------------------------
ALL_LANGS=(en pt de es fr hi ja ko ru zh ar)
declare -A LANG_NAMES=(
  [en]="English (en)"     [pt]="Português (pt)"
  [de]="Deutsch (de)"     [es]="Español (es)"
  [fr]="Français (fr)"    [hi]="हिन्दी (hi)"
  [ja]="日本語 (ja)"        [ko]="한국어 (ko)"
  [ru]="Русский (ru)"     [zh]="中文 (zh)"
  [ar]="العربية (ar)"
)

# Device keys mirror cloud-build.yml `hardware_target` (single source of truth).
DEVICE_KEYS=(pwmp7 ep2pwm mushin)
DEVICE_LABELS=(
  "pwmp7  · PWMP7 — 3/6 local servo pads (native PWM)"
  "ep2pwm · EP2 PWM — standalone, RX/TX (GPIO3/1) as 2 servos"
  "mushin · EP2 + RP2040 — MUSHIN companion bridge (9 ch)"
)

# Mixer profiles (OrnithopterConfig.h enum MixerProfile, 0..12).
MIXER_LABELS=(
  "0  · 2 wings (flapping waveform)"
  "1  · 2 wings + crest rudder"
  "2  · 4 wings (front/back flapping)"
  "3  · V-tail + head rudder (gearbox)"
  "4  · motor + V-tail (gearbox)"
  "5  · motor + V-tail + head rudder (gearbox)"
  "6  · elevator + rudder (gearbox)"
  "7  · motor + elevator + rudder (gearbox)"
  "8  · motor + rudder + 3-servo rotation tail (gearbox)"
  "9  · motor + rudder + 2-servo split elevon tail (gearbox)"
  "10 · motor + rudder + 1-pitch 1-rotate tail (gearbox)"
  "11 · motor + rudder + merged 2-servo tail (gearbox)"
  "12 · Z-Wing 9-PWM (motor + rudder + 4 wing + 3 tail)"
)

DOMAIN_KEYS=(EU_CE_2400 ISM_2400)
DOMAIN_LABELS=(
  "EU CE 2400 — LBT (listen-before-talk, limited)"
  "ISM 2400 — full ISM band"
)

GYRO_KEYS=(none rate ahrs)
GYRO_LABELS=(
  "none — no gyro (default)"
  "rate — rate-only damper (MESOZOIC)"
  "ahrs — full self-level (Mahony AHRS)"
)

# ---- Usage ------------------------------------------------------------------
usage() {
  say "PteronautOS Flash Script — wizard or flags; build WebUI + firmware, then flash."
  say ""
  say "Device / airframe:"
  say "  --device <k>       receiver hardware: pwmp7 | ep2pwm | mushin"
  say "  --mixer <0-12>     mixer profile (see --list-mixers)"
  say "  --domain <k>       regulatory domain: eu | ism"
  say "  --gyro <k>         gyro: none | rate | ahrs"
  say "  --horizon          experimental Mesozoic HORIZON env (pwmp7 only)"
  say "                     (legacy: --target pwmp7|ep2|ep2-pwm, --mode none|rate|horizon)"
  say ""
  say "Radio / network / gyro detail:"
  say "  --binding-phrase <s>   hardcoded binding phrase (empty = traditional bind)"
  say "  --wifi-interval <n>    WiFi auto-on seconds (-1 disables, default 30)"
  say "  --device-name <s>      device hostname on the network"
  say "  --wifi-ssid <s>        home WiFi SSID (auto-join)"
  say "  --wifi-pass <s>        home WiFi password"
  say "  --i2c-sda <gpio>       MPU6050 SDA (default 1)"
  say "  --i2c-scl <gpio>       MPU6050 SCL (default 3)"
  say "  --rotation <0-6>       MPU6050 board rotation"
  say "  --pre-detect <0|1>     probe SCL before I²C init (default 1)"
  say "  --uart-baud <rate>     CRSF UART baud (default 420000)"
  say ""
  say "WebUI languages:"
  say "  -l, --lang <codes>   comma-separated locale codes, or 'all' (default all)"
  say ""
  say "Build / flash control:"
  say "  --no-build           skip rebuild, flash existing .bin"
  say "  --port <device>      force USB serial port"
  say "  --baud <rate>        serial rate (default 115200, try 9600 if flaky)"
  say "  --check              check bootloader (chip id) only, no build/flash"
  say ""
  say "Info:"
  say "  --list-mixers        list mixer profiles and exit"
  say "  --list-devices       list receiver devices and exit"
  say "  --list-langs         list available language codes and exit"
  say "  --help, -h           show this help"
}

# ---- Argument parsing -------------------------------------------------------
# Defaults mirror cloud-build.yml.
LANG_SEL=""           # "" → interactive prompt
BUILD=1
CHECK_ONLY=0
LIST_MIXERS=0
LIST_DEVICES=0
LIST_LANGS=0
PORT=""
BAUD=115200
# Config (single source of truth == web flasher collectParams).
DEVICE=pwmp7
MIXER_PROFILE=1
DOMAIN=EU_CE_2400
BINDING_PHRASE=""
WIFI_INTERVAL=30
ZEPHYR_ENABLED=0
ZEPHYR_MODE=1          # 1=rate-only (MESOZOIC), 0=AHRS self-level
ZEPHYR_SDA=1
ZEPHYR_SCL=3
ZEPHYR_ROTATION=0
ZEPHYR_PRE_DETECT=1
UART_BAUD=420000
DEVICE_NAME=""
WIFI_SSID=""
WIFI_PASS=""
HORIZON=0

ORIG_ARGS=("$@")   # snapshot before the parser shift()s them away

while [[ $# -gt 0 ]]; do
  case "$1" in
    # --- primary wizard knobs ---
    --device)      DEVICE="${2:?--device requires pwmp7, ep2pwm or mushin}"; shift 2 ;;
    --mixer)       MIXER_PROFILE="${2:?--mixer requires 0..12}"; shift 2 ;;
    --domain)
      case "$2" in
        eu|EU|EU_CE_2400) DOMAIN=EU_CE_2400 ;;
        ism|ISM|ISM_2400) DOMAIN=ISM_2400 ;;
        *) err "--domain requires eu or ism"; exit 1 ;;
      esac; shift 2 ;;
    --gyro)
      case "$2" in
        none) ZEPHYR_ENABLED=0 ;;
        rate) ZEPHYR_ENABLED=1; ZEPHYR_MODE=1 ;;
        ahrs) ZEPHYR_ENABLED=1; ZEPHYR_MODE=0 ;;
        *) err "--gyro requires none, rate or ahrs"; exit 1 ;;
      esac; shift 2 ;;
    --horizon) HORIZON=1; shift ;;
    # --- detail ---
    --binding-phrase) BINDING_PHRASE="$2"; shift 2 ;;
    --wifi-interval) WIFI_INTERVAL="$2"; shift 2 ;;
    --device-name)   DEVICE_NAME="$2"; shift 2 ;;
    --wifi-ssid)     WIFI_SSID="$2"; shift 2 ;;
    --wifi-pass)     WIFI_PASS="$2"; shift 2 ;;
    --i2c-sda)       ZEPHYR_SDA="$2"; shift 2 ;;
    --i2c-scl)       ZEPHYR_SCL="$2"; shift 2 ;;
    --rotation)      ZEPHYR_ROTATION="$2"; shift 2 ;;
    --pre-detect)    ZEPHYR_PRE_DETECT="$2"; shift 2 ;;
    --uart-baud)     UART_BAUD="$2"; shift 2 ;;
    # --- legacy aliases ---
    --target)
      case "$2" in
        pwmp7)   DEVICE=pwmp7 ;;
        ep2)     DEVICE=mushin ;;
        ep2-pwm) DEVICE=ep2pwm ;;
        *) err "--target requires pwmp7, ep2 or ep2-pwm"; exit 1 ;;
      esac; shift 2 ;;
    --mode)
      case "$2" in
        none)    ZEPHYR_ENABLED=0 ;;
        rate)    ZEPHYR_ENABLED=1; ZEPHYR_MODE=1 ;;
        horizon) HORIZON=1; ZEPHYR_ENABLED=1 ;;
        *) err "--mode requires none, rate or horizon"; exit 1 ;;
      esac; shift 2 ;;
    # --- build / flash ---
    -l|--lang)      LANG_SEL="$2"; shift 2 ;;
    --no-build)     BUILD=0; shift ;;
    --check)        CHECK_ONLY=1; BUILD=0; shift ;;
    --port)         PORT="$2"; shift 2 ;;
    --baud)         BAUD="$2"; shift 2 ;;
    # --- info ---
    --list-mixers)  LIST_MIXERS=1; shift ;;
    --list-devices) LIST_DEVICES=1; shift ;;
    --list-langs)   LIST_LANGS=1; shift ;;
    --help|-h)      usage; exit 0 ;;
    *) err "Unknown argument: $1"; usage; exit 1 ;;
  esac
done

# ---- Info: list -------------------------------------------------------------
if [[ "$LIST_MIXERS" == "1" ]]; then
  say "${C_BOLD}Mixer profiles${C_RESET} (--mixer):"
  for i in "${!MIXER_LABELS[@]}"; do printf '  %s\n' "${MIXER_LABELS[$i]}"; done
fi
if [[ "$LIST_DEVICES" == "1" ]]; then
  say "${C_BOLD}Receiver devices${C_RESET} (--device):"
  for i in "${!DEVICE_LABELS[@]}"; do printf '  %s\n' "${DEVICE_LABELS[$i]}"; done
fi
if [[ "$LIST_MIXERS" == "1" || "$LIST_DEVICES" == "1" ]]; then
  exit 0
fi
if [[ "$LIST_LANGS" == "1" ]]; then
  say "${C_BOLD}Available languages${C_RESET} (use with -l / --lang, comma-separated):"
  for code in "${ALL_LANGS[@]}"; do printf '  %-4s %s\n' "$code" "${LANG_NAMES[$code]}"; done
  say ""
  say "Examples:  -l de      -l de,en      -l all"
  exit 0
fi

# ---- Interactive helpers -----------------------------------------------------
# choose <prompt> <opt...> → 1-based index (arrow keys ↑/↓ + Enter, or type a number).
# Non-TTY stdin → returns 1 (first option).
choose() {
  local prompt="$1"; shift
  local -a opts=("$@")
  local n=${#opts[@]}
  local idx=0 key kseq i drawn=0

  if [[ ! -t 0 ]]; then echo 1; return 0; fi

  printf '%s\n' "$prompt" >&2
  printf '\033[?25l' >&2

  while true; do
    if [[ $drawn -eq 1 ]]; then printf '\033[%dA' "$((n+1))" >&2; fi
    for ((i=0; i<n; i++)); do
      if [[ $i -eq $idx ]]; then
        printf '\033[7m  %-46s\033[0m\033[K\n' "${opts[$i]}" >&2
      else
        printf '   %-46s\033[K\n' "${opts[$i]}" >&2
      fi
    done
    printf '\033[K  ↑/↓ move · Enter confirm · or type 1-%d > ' "$n" >&2
    drawn=1

    IFS= read -rsn1 key || { printf '\033[?25h\n' >&2; echo 1; return 0; }
    case "$key" in
      $'\x1b')
        IFS= read -rsn2 -t 0.1 kseq || true
        case "$kseq" in
          '[A') [[ $idx -gt 0 ]] && ((idx--)) ;;
          '[B') [[ $idx -lt $((n-1)) ]] && ((idx++)) ;;
        esac ;;
      ''|$'\r'|$'\n')
        printf '\033[?25h\n' >&2
        echo $((idx+1)); return 0 ;;
      [0-9])
        if [[ $key -ge 1 && $key -le $n ]]; then
          printf '\033[?25h\n' >&2
          echo "$key"; return 0
        fi ;;
    esac
  done
}

ask_text() {  # ask_text <prompt> <default> → value (empty input = default)
  local prompt="$1" default="$2" val
  if [[ ! -t 0 ]]; then echo "$default"; return 0; fi
  printf '%s [%s] > ' "$prompt" "$default" >&2
  read -r val
  if [[ -z "$val" ]]; then echo "$default"; else echo "$val"; fi
}

# ---- Interactive wizard -----------------------------------------------------
wizard_primary() {
  local di mi oi gi
  di="$(choose "Device (receiver hardware):" "${DEVICE_LABELS[@]}")"
  DEVICE="${DEVICE_KEYS[$((di-1))]}"

  mi="$(choose "Mixer profile (airframe):" "${MIXER_LABELS[@]}")"
  MIXER_PROFILE="$((mi-1))"

  oi="$(choose "Regulatory domain (2.4 GHz):" "${DOMAIN_LABELS[@]}")"
  DOMAIN="${DOMAIN_KEYS[$((oi-1))]}"

  gi="$(choose "Gyro (Zephyrus):" "${GYRO_LABELS[@]}")"
  case "${GYRO_KEYS[$((gi-1))]}" in
    none) ZEPHYR_ENABLED=0 ;;
    rate) ZEPHYR_ENABLED=1; ZEPHYR_MODE=1 ;;
    ahrs) ZEPHYR_ENABLED=1; ZEPHYR_MODE=0 ;;
  esac
}

wizard_advanced() {
  local c
  c="$(choose "Configure advanced options (binding, WiFi, I²C, baud)?" \
        "No — use defaults" \
        "Yes — customize")"
  [[ "$c" != "2" ]] && return 0

  BINDING_PHRASE="$(ask_text "Binding phrase (empty = traditional bind)" "$BINDING_PHRASE")"
  WIFI_INTERVAL="$(ask_text "WiFi auto-on interval seconds (-1 disables)" "$WIFI_INTERVAL")"
  DEVICE_NAME="$(ask_text "Device hostname (empty = default)" "$DEVICE_NAME")"
  WIFI_SSID="$(ask_text "Home WiFi SSID (empty = none)" "$WIFI_SSID")"
  if [[ -n "$WIFI_SSID" ]]; then
    WIFI_PASS="$(ask_text "Home WiFi password" "$WIFI_PASS")"
  fi
  ZEPHYR_SDA="$(ask_text "MPU6050 I²C SDA GPIO" "$ZEPHYR_SDA")"
  ZEPHYR_SCL="$(ask_text "MPU6050 I²C SCL GPIO" "$ZEPHYR_SCL")"
  ZEPHYR_ROTATION="$(ask_text "MPU6050 board rotation (0-6)" "$ZEPHYR_ROTATION")"
  ZEPHYR_PRE_DETECT="$(ask_text "Probe SCL before I²C init (1=on)" "$ZEPHYR_PRE_DETECT")"
  UART_BAUD="$(ask_text "CRSF UART baud" "$UART_BAUD")"
}

# ---- Normalize a comma-separated locale list → canonical codes --------------
normalize_langs() {
  local input="$1"
  input="$(echo "$input" | tr '[:upper:]' '[:lower:]')"
  if [[ "$input" == "all" ]]; then echo "all"; return 0; fi
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
  local choice
  choice="$(choose "How should the WebUI be baked in?" \
    "All ${#ALL_LANGS[@]} languages  (biggest payload — default)" \
    "A selection  (comma-separated codes, e.g. de,en)" \
    "Skip rebuild   (flash existing firmware.bin as-is)")" || { echo "all"; return 0; }

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
    *) echo "all"; return 0 ;;
  esac
}

# ---- Validate config --------------------------------------------------------
validate_config() {
  [[ "$MIXER_PROFILE" =~ ^[0-9]+$ && "$MIXER_PROFILE" -ge 0 && "$MIXER_PROFILE" -le 12 ]] \
    || { err "Invalid mixer profile: $MIXER_PROFILE (0..12)"; exit 1; }
  [[ "$ZEPHYR_ROTATION" =~ ^[0-9]+$ && "$ZEPHYR_ROTATION" -le 6 ]] \
    || { err "Invalid board rotation: $ZEPHYR_ROTATION (0..6)"; exit 1; }
  [[ "$ZEPHYR_PRE_DETECT" == "0" || "$ZEPHYR_PRE_DETECT" == "1" ]] \
    || { err "Invalid --pre-detect: $ZEPHYR_PRE_DETECT (0|1)"; exit 1; }
  if [[ "$HORIZON" == "1" && "$DEVICE" != "pwmp7" ]]; then
    err "--horizon is pwmp7-only (got --device $DEVICE)"; exit 1
  fi
}

# ---- Resolve target env / product / pwm outputs -----------------------------
resolve_target() {
  case "$DEVICE" in
    ep2pwm) TARGET_ENV=PteronautOS_ESP8285_EP2_2400_RX_PWM ;;
    mushin) TARGET_ENV=PteronautOS_ESP8285_EP2_2400_RX ;;
    *)      TARGET_ENV=PteronautOS_ESP8285_2400_RX ;;
  esac
  [[ "$HORIZON" == "1" ]] && TARGET_ENV=PteronautOS_ESP8285_2400_RX_HORIZON

  case "$DEVICE" in
    ep2pwm) EXPECT_PRODUCT="PteronautOS EP2 PWM" ;;
    mushin) EXPECT_PRODUCT="PteronautOS EP2" ;;
    *)      EXPECT_PRODUCT="PteronautOS PWMP7" ;;
  esac

  PWM_OUT=""
  if [[ "$DEVICE" == "pwmp7" ]]; then
    local gyro_active=0
    [[ "$HORIZON" == "1" || "$ZEPHYR_ENABLED" == "1" ]] && gyro_active=1
    if [[ "$gyro_active" == "1" ]]; then PWM_OUT="0,9,10,5"; else PWM_OUT="0,1,3,9,10,5"; fi
  fi
}

# ---- user_defines.txt (mirrors cloud-build.yml "Write user defines") --------
write_user_defines() {
  local out="$PROJECT_ROOT/src/user_defines.txt"
  {
    echo "-DAUTO_WIFI_ON_INTERVAL=$WIFI_INTERVAL"
    echo "-DLOCK_ON_FIRST_CONNECTION"
    echo "-DMIXER_PROFILE=$MIXER_PROFILE"
    echo "-DZEPHYR_I2C_SDA=$ZEPHYR_SDA"
    echo "-DZEPHYR_I2C_SCL=$ZEPHYR_SCL"
    echo "-DZEPHYR_BOARD_ROTATION=$ZEPHYR_ROTATION"
    echo "-DRCVR_UART_BAUD=$UART_BAUD"
    if [[ "$ZEPHYR_ENABLED" == "1" ]]; then
      echo "-DZEPHYRUS_ENABLED=1"
    fi
    if [[ "$ZEPHYR_MODE" == "0" ]]; then
      echo "!-D MESOZOIC_ONLY=1"
    fi
    if [[ "$ZEPHYR_PRE_DETECT" == "0" ]]; then
      echo "-DZEPHYR_I2C_PRE_DETECT=0"
    fi
    if [[ -n "$DEVICE_NAME" ]]; then
      echo "-DDEVICE_NAME=\"$DEVICE_NAME\""
    fi
    if [[ -n "$WIFI_SSID" ]]; then
      echo "-DHOME_WIFI_SSID=\"$WIFI_SSID\""
    fi
    if [[ -n "$WIFI_PASS" ]]; then
      echo "-DHOME_WIFI_PASSWORD=\"$WIFI_PASS\""
    fi
    if [[ -n "$BINDING_PHRASE" ]]; then
      echo "-DMY_BINDING_PHRASE=\"$BINDING_PHRASE\""
    fi
  } > "$out"
}

# ---- Build pipeline ---------------------------------------------------------
build_webui() {
  local list="$1"
  step "Building WebUI"
  (
    cd "$PROJECT_ROOT/src/html" || exit 1
    local NVM_DIR="${NVM_DIR:-$HOME/.nvm}"
    if [[ -s "$NVM_DIR/nvm.sh" ]]; then
      # shellcheck disable=SC1091
      . "$NVM_DIR/nvm.sh" >/dev/null 2>&1 || true
      nvm use >/dev/null 2>&1 || true
    fi
    say "${C_DIM}Using Node $(node --version 2>/dev/null || echo '?') for the WebUI build.${C_RESET}" >&2

    if [[ "$list" == "all" ]]; then
      ok "Baking in all ${#ALL_LANGS[@]} languages."
      I18N_LOCALES=all npm run build:pteronautos
    elif [[ -z "$list" ]]; then
      ok "Baking in English only (smallest bundle)."
      npm run build:pteronautos
    else
      ok "Baking in languages: ${list}."
      I18N_LOCALES="$list" npm run build:pteronautos
    fi
  )
}

build_firmware() {
  step "Building firmware ($TARGET_ENV)"
  write_user_defines
  local DOMAIN_FLAG
  if [[ "$DOMAIN" == "EU_CE_2400" ]]; then
    DOMAIN_FLAG="-DRegulatory_Domain_EU_CE_2400"
  else
    DOMAIN_FLAG="-DRegulatory_Domain_ISM_2400"
  fi
  (
    cd "$PROJECT_ROOT/src" || exit 1
    PLATFORMIO_BUILD_FLAGS="$DOMAIN_FLAG" pio run -e "$TARGET_ENV"
  )
}

verify_firmware() {
  local fw="$1" args=()
  args+=(--expect_product "$EXPECT_PRODUCT")
  [[ -n "$PWM_OUT" ]] && args+=(--pwm_outputs "$PWM_OUT")
  python3 "$PROJECT_ROOT/src/python/verify_pteronautos_image.py" "${args[@]}" "$fw"
}

# ---- Configuration summary --------------------------------------------------
summary() {
  local gyro_lbl
  if [[ "$HORIZON" == "1" ]]; then gyro_lbl="horizon (experimental)";
  elif [[ "$ZEPHYR_ENABLED" == "0" ]]; then gyro_lbl="none";
  elif [[ "$ZEPHYR_MODE" == "1" ]]; then gyro_lbl="rate-only";
  else gyro_lbl="AHRS self-level"; fi

  say "${C_BOLD}Configuration${C_RESET}"
  say "  device        : ${DEVICE}"
  say "  mixer         : ${MIXER_LABELS[$MIXER_PROFILE]}"
  say "  domain        : ${DOMAIN}"
  say "  gyro          : ${gyro_lbl}"
  [[ -n "$BINDING_PHRASE" ]] && say "  binding       : ${BINDING_PHRASE}"
  say "  wifi interval : ${WIFI_INTERVAL}s"
  [[ -n "$DEVICE_NAME" ]] && say "  device name   : ${DEVICE_NAME}"
  [[ -n "$WIFI_SSID" ]] && say "  home wifi     : ${WIFI_SSID}"
  say "  target env    : ${TARGET_ENV}"
  say ""
}

# ---- Resolve config (interactive if no flags) -------------------------------
CONFIG_GIVEN=0
for a in "${ORIG_ARGS[@]}"; do
  case "$a" in
    --device|--mixer|--domain|--gyro|--horizon|--target|--mode|\
    --binding-phrase|--wifi-interval|--device-name|--wifi-ssid|--wifi-pass|\
    --i2c-sda|--i2c-scl|--rotation|--pre-detect|--uart-baud) CONFIG_GIVEN=1 ;;
  esac
done

if [[ "$BUILD" == "1" && "$CONFIG_GIVEN" == "0" ]]; then
  wizard_primary
  wizard_advanced
fi

validate_config
resolve_target

# ---- Resolve language + build ----------------------------------------------
if [[ "$BUILD" == "1" ]]; then
  if [[ -n "$LANG_SEL" ]]; then
    if ! LANGLIST="$(normalize_langs "$LANG_SEL")"; then exit 1; fi
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
    summary
    build_webui "${LANGLIST:-}"
    build_firmware
  fi
else
  summary
fi

# ---- 1) Locate esptool ------------------------------------------------------
step "Locating a working esptool"

CANDIDATES=(
  "$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
  "$HOME/.platformio/packages/tool-esptoolpy@1.30000.201119/esptool.py"
  "$HOME/.platformio/packages/framework-arduinoespressif8266/tools/esptool/esptool.py"
)
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
    ESPT="$c"; ESPT_VER="$v"; break
  fi
done

if [[ -z "$ESPT" ]]; then
  err "No working esptool found (system esptool.py has a broken Python 2 shebang)."
  exit 1
fi
ok "esptool found: $C_DIM${ESPT#$HOME/}$C_RESET  ($ESPT_VER)"

# ---- 2) Locate firmware -----------------------------------------------------
FW="src/.pio/build/$TARGET_ENV/firmware.bin"
[[ -f "$FW" ]] || FW=""

# ---- 3) Locate USB port -----------------------------------------------------
step "Locating USB serial port"

if [[ -n "$PORT" ]]; then
  [[ -e "$PORT" ]] || { err "Port does not exist: $PORT"; exit 1; }
else
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
  err "No firmware.bin for $TARGET_ENV. Build first: ./scripts/flash.sh --device $DEVICE --mixer $MIXER_PROFILE"
  exit 1
fi
FW_SIZE=$(stat -f%z "$FW" 2>/dev/null || stat -c%s "$FW" 2>/dev/null || echo "?")
ok "Firmware: $C_DIM$FW$C_RESET  ($FW_SIZE B)"
verify_firmware "$FW"

# ---- 6) Flash (single connection, no pre-probe) -----------------------------
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