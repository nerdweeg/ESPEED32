#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA_DIR="$ROOT_DIR/source/ESPEED32/data"
BUILD_DIR="$ROOT_DIR/build"
IMAGE_PATH="$BUILD_DIR/espeed32_spiffs.bin"
ARDUINO_JSON="$ROOT_DIR/.vscode/arduino.json"
DOCS_REFRESH_SCRIPT="$ROOT_DIR/scripts/refresh_generated_docs.sh"
MANIFEST_SCRIPT="$ROOT_DIR/scripts/generate_spiffs_release_manifest.sh"

PARTITION_OFFSET="0x290000"
PARTITION_SIZE="0x160000"
BLOCK_SIZE="4096"
PAGE_SIZE="256"

PORT=""
BAUD=""
BUILD_ONLY=0
PURGE=0
ASSUME_YES=0

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--build-only] [--purge] [--yes] [-p PORT] [-u BAUD]

Options:
  --build-only      Build SPIFFS image only (no upload)
  --purge           Erase SPIFFS partition only (no build, no upload)
  -y, --yes         Skip interactive confirmation for --purge
  -p, --port PORT   Serial port override (default: read from .vscode/arduino.json)
  -u, --baud BAUD   Upload baud override (default: UploadSpeed from .vscode/arduino.json)
  -h, --help        Show this help
USAGE
}

# Detect the Arduino15 data directory for the current OS.
arduino_data_dir() {
  local os
  os="$(uname -s)"
  case "$os" in
    Darwin)
      printf '%s' "$HOME/Library/Arduino15"
      ;;
    Linux)
      printf '%s' "${ARDUINO_DATA_DIR:-$HOME/.arduino15}"
      ;;
    MINGW*|MSYS*|CYGWIN*)
      local appdata="${LOCALAPPDATA:-${APPDATA:-}}"
      if [[ -n "$appdata" ]]; then
        # Convert Windows path to Unix style for bash
        appdata="$(cygpath -u "$appdata" 2>/dev/null || printf '%s' "$appdata" | sed 's|\\|/|g; s|^\([A-Za-z]\):|/\1|')"
        printf '%s' "$appdata/Arduino15"
      else
        printf '%s' "$HOME/AppData/Local/Arduino15"
      fi
      ;;
    *)
      printf '%s' "${ARDUINO_DATA_DIR:-$HOME/.arduino15}"
      ;;
  esac
}

# Return the first available serial port for the current OS, or empty string.
find_serial_port() {
  local os
  os="$(uname -s)"
  case "$os" in
    Darwin)
      ls -1 /dev/cu.usbserial* /dev/cu.usbmodem* /dev/tty.usbserial* 2>/dev/null | head -n 1 || true
      ;;
    Linux)
      ls -1 /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n 1 || true
      ;;
    MINGW*|MSYS*|CYGWIN*)
      # COM ports on Windows — auto-detection is unreliable from bash; require -p
      true
      ;;
    *)
      ls -1 /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n 1 || true
      ;;
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

# macOS only: prefer /dev/cu.* over /dev/tty.* for reliable upload.
matching_cu_port() {
  local port="${1:-}"
  [[ "$(uname -s)" == Darwin ]] || return 1
  if [[ "$port" == /dev/tty.* ]]; then
    local cu_port="/dev/cu.${port#/dev/tty.}"
    if [[ -e "$cu_port" ]]; then
      printf '%s' "$cu_port"
      return 0
    fi
  fi
  return 1
}

preferred_serial_port() {
  local port="${1:-}"
  local cu_port=""
  if cu_port="$(matching_cu_port "$port")"; then
    printf '%s' "$cu_port"
  else
    printf '%s' "$port"
  fi
}

wait_for_serial_port() {
  local port="${1:-}"
  local retries="${2:-10}"
  local delay_s="${3:-1}"
  local candidate=""

  for ((attempt = 0; attempt < retries; ++attempt)); do
    candidate="$(preferred_serial_port "$port")"
    if [[ -n "$candidate" && -e "$candidate" ]]; then
      printf '%s' "$candidate"
      return 0
    fi
    sleep "$delay_s"
  done

  return 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-only)
      BUILD_ONLY=1
      shift
      ;;
    --purge)
      PURGE=1
      shift
      ;;
    -y|--yes)
      ASSUME_YES=1
      shift
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

if [[ "$PURGE" -eq 1 && "$BUILD_ONLY" -eq 1 ]]; then
  echo "Cannot combine --purge and --build-only." >&2
  exit 1
fi

if [[ "$PURGE" -eq 0 && ! -d "$DATA_DIR" ]]; then
  echo "Data directory not found: $DATA_DIR" >&2
  exit 1
fi

if [[ -z "$PORT" && -f "$ARDUINO_JSON" ]]; then
  PORT="$(sed -n 's/.*"port"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$ARDUINO_JSON" | head -n 1)"
fi

if [[ -z "$PORT" ]]; then
  PORT="$(find_serial_port || true)"
fi

if [[ ! -e "$PORT" ]]; then
  ALT_PORT="$(find_serial_port || true)"
  if [[ -n "$ALT_PORT" && "$ALT_PORT" != "$PORT" ]]; then
    echo "[SPIFFS] Port $PORT not found, using $ALT_PORT"
    PORT="$ALT_PORT"
  fi
fi

NORMALIZED_PORT="$(preferred_serial_port "$PORT")"
if [[ -n "$NORMALIZED_PORT" && "$NORMALIZED_PORT" != "$PORT" ]]; then
  echo "[SPIFFS] Using $NORMALIZED_PORT instead of $PORT"
  PORT="$NORMALIZED_PORT"
fi

if [[ -z "$BAUD" && -f "$ARDUINO_JSON" ]]; then
  BAUD="$(sed -n 's/.*UploadSpeed=\([0-9][0-9]*\).*/\1/p' "$ARDUINO_JSON" | head -n 1)"
fi

if [[ -z "$BAUD" ]]; then
  BAUD="115200"
fi

_arduino15="$(arduino_data_dir)"
MKSPiffs_DIR="$(ls -d "$_arduino15/packages/esp32/tools/mkspiffs/"* 2>/dev/null | sort -V | tail -n 1 || true)"
ESPTOOL_DIR="$(ls -d "$_arduino15/packages/esp32/tools/esptool_py/"* 2>/dev/null | sort -V | tail -n 1 || true)"

# On Windows the bundled binaries have a .exe suffix
MKSPiffs_BIN="$MKSPiffs_DIR/mkspiffs"
ESPTOOL_BIN="$ESPTOOL_DIR/esptool"
if [[ ! -x "$MKSPiffs_BIN" && -x "${MKSPiffs_BIN}.exe" ]]; then MKSPiffs_BIN="${MKSPiffs_BIN}.exe"; fi
if [[ ! -x "$ESPTOOL_BIN" && -x "${ESPTOOL_BIN}.exe" ]]; then ESPTOOL_BIN="${ESPTOOL_BIN}.exe"; fi

if [[ "$PURGE" -eq 0 && ! -x "$MKSPiffs_BIN" ]]; then
  echo "mkspiffs not found. Install ESP32 platform tools first." >&2
  exit 1
fi

if [[ ! -x "$ESPTOOL_BIN" && ( "$BUILD_ONLY" -eq 0 || "$PURGE" -eq 1 ) ]]; then
  echo "esptool not found. Install ESP32 platform tools first." >&2
  exit 1
fi

if [[ ( "$BUILD_ONLY" -eq 0 || "$PURGE" -eq 1 ) && ! -e "$PORT" ]]; then
  echo "Serial port not found: ${PORT:-<none detected>}" >&2
  echo "Tip: $(port_hint)" >&2
  exit 1
fi

if [[ "$PURGE" -eq 1 ]]; then
  if [[ "$ASSUME_YES" -eq 0 ]]; then
    echo "[SPIFFS] WARNING: This will erase the SPIFFS partition."
    echo "[SPIFFS] Partition: offset $PARTITION_OFFSET, size $PARTITION_SIZE"
    echo "[SPIFFS] Port: $PORT (baud $BAUD)"
    read -r -p "[SPIFFS] Type PURGE to continue: " confirm
    if [[ "$confirm" != "PURGE" ]]; then
      echo "[SPIFFS] Purge aborted."
      exit 1
    fi
  fi

  echo "[SPIFFS] Erasing partition at $PARTITION_OFFSET (size $PARTITION_SIZE)"
  "$ESPTOOL_BIN" --chip esp32 --port "$PORT" --baud "$BAUD" --before default-reset --after hard-reset erase-region "$PARTITION_OFFSET" "$PARTITION_SIZE"
  echo "[SPIFFS] Purge complete"
  exit 0
fi

mkdir -p "$BUILD_DIR"

if [[ "${ESPEED32_DOCS_REFRESHED:-0}" != "1" && -x "$DOCS_REFRESH_SCRIPT" ]]; then
  "$DOCS_REFRESH_SCRIPT"
fi

if [[ ! -x "$MANIFEST_SCRIPT" ]]; then
  echo "Missing executable manifest script: $MANIFEST_SCRIPT" >&2
  exit 1
fi

"$MANIFEST_SCRIPT"

echo "[SPIFFS] Building image from $DATA_DIR"
"$MKSPiffs_BIN" -c "$DATA_DIR" -b "$BLOCK_SIZE" -p "$PAGE_SIZE" -s "$PARTITION_SIZE" "$IMAGE_PATH"

echo "[SPIFFS] Image ready: $IMAGE_PATH"

if [[ ! -f "$IMAGE_PATH" ]]; then
  echo "[SPIFFS] ERROR: Image file not found after build: $IMAGE_PATH" >&2
  ls -la "$BUILD_DIR" >&2 || true
  exit 1
fi

if [[ "$BUILD_ONLY" -eq 1 ]]; then
  exit 0
fi

READY_PORT="$(wait_for_serial_port "$PORT" 10 1 || true)"
if [[ -n "$READY_PORT" && "$READY_PORT" != "$PORT" ]]; then
  echo "[SPIFFS] Port became ready as $READY_PORT"
  PORT="$READY_PORT"
fi

echo "[SPIFFS] Uploading to $PORT at offset $PARTITION_OFFSET (baud $BAUD)"
if ! "$ESPTOOL_BIN" --chip esp32 --port "$PORT" --baud "$BAUD" --before default-reset --after hard-reset write-flash "$PARTITION_OFFSET" "$IMAGE_PATH"; then
  RETRY_PORT="$(matching_cu_port "$PORT" || true)"
  if [[ -n "$RETRY_PORT" && "$RETRY_PORT" != "$PORT" ]]; then
    echo "[SPIFFS] Retrying with $RETRY_PORT"
    "$ESPTOOL_BIN" --chip esp32 --port "$RETRY_PORT" --baud "$BAUD" --before default-reset --after hard-reset write-flash "$PARTITION_OFFSET" "$IMAGE_PATH"
    PORT="$RETRY_PORT"
  else
    exit 1
  fi
fi

echo "[SPIFFS] Upload complete"
