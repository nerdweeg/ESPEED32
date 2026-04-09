#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARDUINO_JSON="$ROOT_DIR/.vscode/arduino.json"
SPIFFS_SCRIPT="$ROOT_DIR/scripts/upload_spiffs.sh"
DOCS_REFRESH_SCRIPT="$ROOT_DIR/scripts/refresh_generated_docs.sh"

PORT=""
BAUD=""
SENSOR=""
CURRENT_SENSE=""
BOARD_PROFILE_NAME=""
FIRMWARE_ONLY=0
SPIFFS_ONLY=0
COMPILE_ONLY=0

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--firmware-only] [--spiffs-only] [--compile-only] [--sensor NAME] [--current-sense NAME] [--board NAME] [-p PORT] [-u BAUD]

Default flow:
  1) Compile firmware
  2) Upload firmware
  3) Upload SPIFFS image (calls scripts/upload_spiffs.sh)

Options:
  --firmware-only    Compile + upload firmware, skip SPIFFS upload
  --spiffs-only      Only upload SPIFFS image (skip firmware compile/upload)
  --compile-only     Compile firmware only
  --sensor NAME      Trigger sensor family override: tle493d, as5600, as5600l, mt6701, analog
  --current-sense NAME
                     Motor current-sense profile override: btn99x0, none, bts7960
  --board NAME       Board profile override: standard, custom-template
  -p, --port PORT    Serial port override (default: read from .vscode/arduino.json)
  -u, --baud BAUD    SPIFFS upload baud override (default: UploadSpeed from .vscode/arduino.json)
  -h, --help         Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --firmware-only)
      FIRMWARE_ONLY=1
      shift
      ;;
    --spiffs-only)
      SPIFFS_ONLY=1
      shift
      ;;
    --compile-only)
      COMPILE_ONLY=1
      shift
      ;;
    --sensor)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
      SENSOR="$2"
      shift 2
      ;;
    --current-sense)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
      CURRENT_SENSE="$2"
      shift 2
      ;;
    --board)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
      BOARD_PROFILE_NAME="$2"
      shift 2
      ;;
    -p|--port)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
      PORT="$2"
      shift 2
      ;;
    -u|--baud)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
      BAUD="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ "$SPIFFS_ONLY" -eq 1 && ( "$FIRMWARE_ONLY" -eq 1 || "$COMPILE_ONLY" -eq 1 ) ]]; then
  echo "Cannot combine --spiffs-only with firmware-related modes." >&2
  exit 1
fi

if [[ "$COMPILE_ONLY" -eq 1 && "$FIRMWARE_ONLY" -eq 1 ]]; then
  echo "Cannot combine --compile-only and --firmware-only." >&2
  exit 1
fi

if [[ ! -f "$ARDUINO_JSON" ]]; then
  echo "Missing $ARDUINO_JSON" >&2
  exit 1
fi

if [[ ! -x "$SPIFFS_SCRIPT" ]]; then
  echo "Missing executable SPIFFS script: $SPIFFS_SCRIPT" >&2
  exit 1
fi

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli not found in PATH." >&2
  exit 1
fi

# Return the first available serial port for the current OS, or empty string.
find_serial_port() {
  local os
  os="$(uname -s)"
  case "$os" in
    Darwin) ls -1 /dev/cu.usbserial* /dev/cu.usbmodem* /dev/tty.usbserial* 2>/dev/null | head -n 1 || true ;;
    Linux)  ls -1 /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n 1 || true ;;
    *)      true ;;
  esac
}

# Return an OS-appropriate hint for specifying the serial port.
port_hint() {
  local os
  os="$(uname -s)"
  case "$os" in
    Darwin)  printf 'run with -p /dev/cu.usbserial-XXXX' ;;
    Linux)   printf 'run with -p /dev/ttyUSB0 or /dev/ttyACM0' ;;
    MINGW*|MSYS*|CYGWIN*) printf 'run with -p COM3 (check Device Manager for the port number)' ;;
    *)       printf 'run with -p /dev/ttyUSB0 or similar' ;;
  esac
}

json_value() {
  local key="$1"
  sed -n "s/.*\"$key\"[[:space:]]*:[[:space:]]*\"\\([^\"]*\\)\".*/\\1/p" "$ARDUINO_JSON" | head -n 1
}

sensor_build_flag() {
  local sensor_name
  sensor_name="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
  case "$sensor_name" in
    tle|tle493d)
      printf '%s' '-DTRIGGER_SENSOR_FAMILY=TRIGGER_SENSOR_FAMILY_TLE493D'
      ;;
    as5600)
      printf '%s' '-DTRIGGER_SENSOR_FAMILY=TRIGGER_SENSOR_FAMILY_AS5600'
      ;;
    as5600l)
      printf '%s' '-DTRIGGER_SENSOR_FAMILY=TRIGGER_SENSOR_FAMILY_AS5600L'
      ;;
    mt6701)
      printf '%s' '-DTRIGGER_SENSOR_FAMILY=TRIGGER_SENSOR_FAMILY_MT6701'
      ;;
    analog|analog_trig)
      printf '%s' '-DTRIGGER_SENSOR_FAMILY=TRIGGER_SENSOR_FAMILY_ANALOG'
      ;;
    *)
      echo "Unsupported --sensor value: $1" >&2
      echo "Supported values: tle493d, as5600, as5600l, mt6701, analog" >&2
      exit 1
      ;;
  esac
}

current_sense_build_flag() {
  local profile_name
  profile_name="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
  case "$profile_name" in
    btn99x0|btn99)
      printf '%s' '-DCURRENT_SENSE_PROFILE=CURRENT_SENSE_PROFILE_BTN99X0'
      ;;
    none|off)
      printf '%s' '-DCURRENT_SENSE_PROFILE=CURRENT_SENSE_PROFILE_NONE'
      ;;
    bts7960|ibt2|ibt_2)
      printf '%s' '-DCURRENT_SENSE_PROFILE=CURRENT_SENSE_PROFILE_BTS7960'
      ;;
    *)
      echo "Unsupported --current-sense value: $1" >&2
      echo "Supported values: btn99x0, none, bts7960" >&2
      exit 1
      ;;
  esac
}

board_profile_build_flag() {
  local board_name
  board_name="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
  case "$board_name" in
    standard|default)
      printf '%s' '-DESPEED_BOARD_PROFILE=ESPEED_BOARD_PROFILE_STANDARD'
      ;;
    custom|custom-template|template)
      printf '%s' '-DESPEED_BOARD_PROFILE=ESPEED_BOARD_PROFILE_CUSTOM_TEMPLATE'
      ;;
    *)
      echo "Unsupported --board value: $1" >&2
      echo "Supported values: standard, custom-template" >&2
      exit 1
      ;;
  esac
}

run_filtered_cmd() {
  local rc
  local -a cmd
  cmd=("$@")
  set +e
  if command -v stdbuf >/dev/null 2>&1; then
    stdbuf -oL -eL "${cmd[@]}" 2>&1 | awk '
      /esp_bt\.h:16,/ {next}
      /esp32-hal-bt\.c:30:/ {next}
      /esp32-hal-misc\.c:29:/ {next}
      /esp_bredr_cfg\.h:18:9: note:.*BT: forcing BR\/EDR max sync conn eff to 1/ {next}
      /#pragma message \("BT: forcing BR\/EDR max sync conn eff to 1 \(Bluedroid HFP requires SCO\/eSCO\)"\)/ {next}
      /^[[:space:]]*\|[[:space:]]+\^~~~~~~$/ {next}
      { print; fflush() }
    '
  else
    "${cmd[@]}" 2>&1 | awk '
      /esp_bt\.h:16,/ {next}
      /esp32-hal-bt\.c:30:/ {next}
      /esp32-hal-misc\.c:29:/ {next}
      /esp_bredr_cfg\.h:18:9: note:.*BT: forcing BR\/EDR max sync conn eff to 1/ {next}
      /#pragma message \("BT: forcing BR\/EDR max sync conn eff to 1 \(Bluedroid HFP requires SCO\/eSCO\)"\)/ {next}
      /^[[:space:]]*\|[[:space:]]+\^~~~~~~$/ {next}
      { print; fflush() }
    '
  fi
  rc=${PIPESTATUS[0]}
  set -e
  return "$rc"
}

BOARD="$(json_value board)"
BOARD_OPTIONS="$(json_value configuration)"
SKETCH_REL="$(json_value sketch)"
OUTPUT_REL="$(json_value output)"

if [[ -z "$BOARD" || -z "$SKETCH_REL" || -z "$OUTPUT_REL" ]]; then
  echo "Invalid $ARDUINO_JSON. Expected keys: board, sketch, output." >&2
  exit 1
fi

if [[ "$SKETCH_REL" = /* ]]; then
  SKETCH_PATH="$SKETCH_REL"
else
  SKETCH_PATH="$ROOT_DIR/$SKETCH_REL"
fi
SKETCH_DIR="$(cd "$(dirname "$SKETCH_PATH")" && pwd)"

if [[ "$OUTPUT_REL" = /* ]]; then
  BUILD_DIR="$OUTPUT_REL"
else
  BUILD_DIR="$ROOT_DIR/$OUTPUT_REL"
fi

if [[ ! -f "$SKETCH_PATH" ]]; then
  echo "Sketch not found: $SKETCH_PATH" >&2
  exit 1
fi

if [[ -z "$PORT" ]]; then
  PORT="$(json_value port)"
fi
if [[ -z "$PORT" ]]; then
  PORT="$(find_serial_port || true)"
fi

if [[ ! -e "$PORT" ]]; then
  ALT_PORT="$(find_serial_port || true)"
  if [[ -n "$ALT_PORT" && "$ALT_PORT" != "$PORT" ]]; then
    echo "[FLASH] Port $PORT not found, using $ALT_PORT"
    PORT="$ALT_PORT"
  fi
fi

if [[ -z "$BAUD" ]]; then
  BAUD="$(sed -n 's/.*UploadSpeed=\([0-9][0-9]*\).*/\1/p' "$ARDUINO_JSON" | head -n 1)"
fi
if [[ -z "$BAUD" ]]; then
  BAUD="115200"
fi

if [[ "$SPIFFS_ONLY" -eq 0 && "$COMPILE_ONLY" -eq 0 && ! -e "$PORT" ]]; then
  echo "Serial port not found: ${PORT:-<none detected>}" >&2
  echo "Tip: $(port_hint)" >&2
  exit 1
fi

if [[ "$FIRMWARE_ONLY" -eq 0 && "$COMPILE_ONLY" -eq 0 && -x "$DOCS_REFRESH_SCRIPT" ]]; then
  "$DOCS_REFRESH_SCRIPT"
  export ESPEED32_DOCS_REFRESHED=1
fi

if [[ "$SPIFFS_ONLY" -eq 0 ]]; then
  mkdir -p "$BUILD_DIR"

  echo "[FLASH] Compiling firmware"
  compile_cmd=(arduino-cli compile --fqbn "$BOARD" --build-path "$BUILD_DIR")
  if [[ -n "$BOARD_OPTIONS" ]]; then
    compile_cmd+=(--board-options "$BOARD_OPTIONS")
  fi
  EXTRA_FLAGS=()
  if [[ -n "$BOARD_PROFILE_NAME" ]]; then
    BOARD_PROFILE_FLAG="$(board_profile_build_flag "$BOARD_PROFILE_NAME")"
    echo "[FLASH] Board profile override: $BOARD_PROFILE_NAME"
    EXTRA_FLAGS+=("$BOARD_PROFILE_FLAG")
  fi
  if [[ -n "$SENSOR" ]]; then
    SENSOR_FLAG="$(sensor_build_flag "$SENSOR")"
    echo "[FLASH] Trigger sensor family override: $SENSOR"
    EXTRA_FLAGS+=("$SENSOR_FLAG")
  fi
  if [[ -n "$CURRENT_SENSE" ]]; then
    CURRENT_SENSE_FLAG="$(current_sense_build_flag "$CURRENT_SENSE")"
    echo "[FLASH] Current-sense profile override: $CURRENT_SENSE"
    EXTRA_FLAGS+=("$CURRENT_SENSE_FLAG")
  fi
  if [[ ${#EXTRA_FLAGS[@]} -gt 0 ]]; then
    EXTRA_FLAGS_STRING="${EXTRA_FLAGS[*]}"
    compile_cmd+=(--build-property "build.extra_flags=$EXTRA_FLAGS_STRING")
  fi
  compile_cmd+=("$SKETCH_DIR")
  run_filtered_cmd "${compile_cmd[@]}"

  if [[ "$COMPILE_ONLY" -eq 1 ]]; then
    echo "[FLASH] Compile complete"
    exit 0
  fi

  echo "[FLASH] Uploading firmware to $PORT"
  upload_cmd=(arduino-cli upload --fqbn "$BOARD" --port "$PORT" --build-path "$BUILD_DIR")
  if [[ -n "$BOARD_OPTIONS" ]]; then
    upload_cmd+=(--board-options "$BOARD_OPTIONS")
  fi
  upload_cmd+=("$SKETCH_DIR")
  "${upload_cmd[@]}"

  echo "[FLASH] Firmware upload complete"
fi

if [[ "$FIRMWARE_ONLY" -eq 1 ]]; then
  exit 0
fi

sleep 2
echo "[FLASH] Uploading SPIFFS image"
"$SPIFFS_SCRIPT" -p "$PORT" -u "$BAUD"

echo "[FLASH] All done"
