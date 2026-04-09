# ESPEED32
Cutting edge slot car controller with an ESP32 at its heart.
How to build it — video: https://www.youtube.com/watch?v=JtMKeiguHKI
Hosted Web UI/docs: https://espeed32.com (best in Chrome/Edge or another WebSerial-capable browser)
![ThumbV2](https://github.com/user-attachments/assets/9b7e1479-4882-4ed7-93d0-4a9a81be73fc)

## Quick build examples

Default firmware (standard hardware):

```bash
./scripts/flash_all.sh --compile-only
```

Build for another magnetic trigger sensor:

```bash
./scripts/flash_all.sh --compile-only --sensor as5600
./scripts/flash_all.sh --compile-only --sensor mt6701
./scripts/flash_all.sh --compile-only --sensor analog
```

Build for another motor-controller current-sense profile:

```bash
./scripts/flash_all.sh --compile-only --current-sense bts7960
./scripts/flash_all.sh --compile-only --current-sense none
```

Combine both in one build:

```bash
./scripts/flash_all.sh --compile-only --sensor as5600 --current-sense bts7960
```

Build against the committed custom board template:

```bash
./scripts/flash_all.sh --compile-only --board custom-template
./scripts/flash_all.sh --compile-only --board custom-template --sensor as5600
```

## Magnetic Trigger Sensor

The trigger position is read from a magnetic angle sensor over I2C. Several sensors are supported.

Default build uses `TLE493D`.

Recommended selection method is the helper script:

```bash
./scripts/flash_all.sh --compile-only --sensor as5600
```

Supported `--sensor` values:

- `tle493d`
- `as5600`
- `as5600l`
- `mt6701`
- `analog`

You can also pass the family directly to `arduino-cli`:

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32 \
  --board-options "JTAGAdapter=default,PSRAM=disabled,PartitionScheme=default,CPUFreq=240,FlashMode=qio,FlashFreq=80,FlashSize=4M,UploadSpeed=115200,LoopCore=1,EventsCore=1,DebugLevel=none,EraseFlash=none,ZigbeeMode=default" \
  --build-path build-as5600 \
  --build-property build.extra_flags="-DTRIGGER_SENSOR_FAMILY=TRIGGER_SENSOR_FAMILY_AS5600" \
  source/ESPEED32
```

Legacy manual selection in `source/ESPEED32/HAL.h` still works if you prefer editing the header directly.

### Windows / Arduino IDE

`flash_all.sh` is a Bash helper around `arduino-cli`. On Windows that usually means Git Bash or WSL.

If you prefer Arduino IDE:

- open `source/ESPEED32/ESPEED32.ino`
- select the normal ESP32 board target
- choose the trigger sensor family before compiling
- then use Arduino IDE's normal verify/upload flow

Important:

- the trigger sensor family is still a compile-time choice
- Arduino IDE does not provide a firmware menu for sensor family selection
- for Arduino IDE users, the practical workaround is usually the legacy manual selection in `source/ESPEED32/HAL.h`
- if you compile the default build unchanged, it targets `TLE493D`

## Board Profiles

Board routing is now also a compile-time choice.

Supported `--board` values:

- `standard`
- `custom-template`

Examples:

```bash
./scripts/flash_all.sh --compile-only --board standard
./scripts/flash_all.sh --compile-only --board custom-template --sensor as5600
```

The standard official controller hardware remains the default build.

`custom-template` is a committed starting point for custom builders. Edit:

- `source/ESPEED32/board_profiles/custom_template.h`

before compiling with `--board custom-template`.

This is meant for local/manual custom builds. It does not change the official OTA path.

### Official releases, manual sensor builds, and OTA

Automatic paired OTA installs the standard official release pair. That standard firmware build remains the default `TLE493D` variant.

If your controller uses another trigger sensor family such as `AS5600`, `AS5600L`, `MT6701`, or `ANALOG`, use the matching firmware build for that sensor family together with the SPIFFS image from the same release.

In other words:

- automatic paired OTA = standard `TLE493D` release path
- automatic paired OTA also assumes the standard official board profile
- sensor-specific firmware builds = manual firmware selection/upload unless explicitly stated otherwise
- custom board profiles = manual firmware selection/upload
- SPIFFS/UI images remain version-matched per release and are shared across sensor variants

### Supported sensors

| Define | Sensor |
|---|---|
| `TLE493D_MAG` | Infineon TLE493D *(active by default)* |
| `AS5600_MAG` | AMS AS5600 |
| `AS5600L_MAG` | AMS AS5600L (different I2C address variant) |
| `MT6701_MAG` | MagnTek MT6701 |
| `ANALOG_TRIG` | Analog potentiometer (no I2C) |

I2C pins for the sensor: **SDA = GPIO21, SCL = GPIO22**

---

### TLE493D — address variants

Current HAL behavior (`source/ESPEED32/HAL.cpp`):

- auto-detects supported TLE493D variants/addresses at boot
- caches detected variant + address in NVS (`sensor_cfg`) for faster next boot
- no manual `ADDRESS` edit is normally required

Supported auto-detect variants:

| Family | Part / suffix example | 7-bit I2C address | Notes |
|---|---|---|---|
| W2B6 | `TLE493D-W2B6 A0` / `TLE493D-W2B6 A0 HTSA1` | `0x35` | Newly supported W2B6 A0 variant |
| W2B6 | `TLE493D-W2B6 A3` | `0x44` | Existing W2B6 support |
| P3B6 | `TLE493D-P3B6 A0` | `0x5D` | Version 3 silicon |
| P3B6 | `TLE493D-P3B6 A1` | `0x13` |  |
| P3B6 | `TLE493D-P3B6 A2` | `0x29` |  |
| P3B6 | `TLE493D-P3B6 A3` | `0x46` |  |

If detection fails, firmware continues and prints a warning on serial output.

> **Silicon revision note:** `TLE493D-P3B6 A0` is version 3 silicon and is available on Digikey/Mouser. The older P2 revision has known issues; some users have managed to get it working, but it is not guaranteed.

## Motor Current Sense Profiles

The default build is still for the original `BTN99X0` motor-control hardware.

What can be changed at compile time today is the **motor current-sense profile**. This is the practical way to build for other motor-controller boards right now.

Important:

- this is supported today
- it is **not** yet a full alternate motor-driver / PWM implementation switch
- `BTS7960` currently changes **current measurement only**
- the BTS7960 values are starting points and may need tuning for the exact module and resistor/filter network used

Current profile defines live in `source/ESPEED32/HAL.h`:

- `CURRENT_SENSE_PROFILE_BTN99X0` = original/default hardware behavior
- `CURRENT_SENSE_PROFILE_NONE` = disables current-sense-dependent behavior
- `CURRENT_SENSE_PROFILE_BTS7960` = initial BTS7960 / IBT_2 current-sense profile

### Default build

Nothing needs to be changed for the standard controller hardware.

Use the normal flow:

- `./scripts/flash_all.sh`
- or `./scripts/flash_all.sh --compile-only`

### Simple current-sense override with `flash_all.sh`

Supported `--current-sense` values:

- `btn99x0`
- `none`
- `bts7960`

Examples:

```bash
./scripts/flash_all.sh --compile-only --current-sense btn99x0
./scripts/flash_all.sh --compile-only --current-sense none
./scripts/flash_all.sh --compile-only --current-sense bts7960
```

You can combine sensor + current-sense override:

```bash
./scripts/flash_all.sh --compile-only --sensor tle493d --current-sense bts7960
./scripts/flash_all.sh --compile-only --sensor mt6701 --current-sense none
```

### Advanced `arduino-cli` example

If you want to compile directly with `arduino-cli`, override it like this:

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32 \
  --board-options "JTAGAdapter=default,PSRAM=disabled,PartitionScheme=default,CPUFreq=240,FlashMode=qio,FlashFreq=80,FlashSize=4M,UploadSpeed=115200,LoopCore=1,EventsCore=1,DebugLevel=none,EraseFlash=none,ZigbeeMode=default" \
  --build-path build-bts7960 \
  --build-property build.extra_flags="-DCURRENT_SENSE_PROFILE=CURRENT_SENSE_PROFILE_BTS7960" \
  source/ESPEED32
```

Optional tuning overrides for the BTS7960 profile:

```bash
--build-property build.extra_flags="-DCURRENT_SENSE_PROFILE=CURRENT_SENSE_PROFILE_BTS7960 -DBTS7960_CURRENT_SENSE_KILIS=8500 -DBTS7960_CURRENT_SENSE_EFFECTIVE_R_OHMS=1000 -DBTS7960_CURRENT_SENSE_OFFSET_MV=0"
```

What the BTS7960 tuning values mean:

- `BTS7960_CURRENT_SENSE_KILIS`: datasheet current-sense ratio starting point
- `BTS7960_CURRENT_SENSE_EFFECTIVE_R_OHMS`: effective load resistance seen by the `IS` pin at the ADC input
- `BTS7960_CURRENT_SENSE_OFFSET_MV`: optional offset trim if the module shows idle bias

Current status of the BTS7960 profile:

- it compiles successfully
- it uses a first-pass `kILIS` / resistor-based conversion model
- it likely still needs validation and tuning on real IBT_2 / BTS7960 hardware
- if the module does not expose a usable `IS` signal, use `CURRENT_SENSE_PROFILE_NONE` instead

Behavior when current sense is unavailable:

- current readout is shown as `N/A`
- lap detection stays disabled
- self-test skips current-specific checks cleanly

## External Pot Overrides

The firmware also has an initial **external potentiometer override** feature for live control of:

- `BRAKE`
- `SENSI`

Current hardware mapping in firmware:

- `POT1` = `GPIO35`
- `POT2` = `GPIO15`

Current behavior:

- `POT1` and `POT2` are configured from `Settings -> Hardware -> Ext Pot`
- each pot can be assigned to `OFF`, `BRAKE`, or `SENSI`
- the pots act as **live global overrides**
- they do **not** overwrite the stored car values in flash
- the UI/race screen shows `POT1` / `POT2` when an override is active
- if both pots are assigned to the same target, the older/conflicting assignment is forced back to `OFF`

Important hardware note:

- `GPIO15` is an `ADC2` pin on ESP32, so it may be unreliable for analog reading while WiFi is active

Please verify the actual board routing against:

- `ESPEED32_V3_0_schematic_PCB.pdf`

before soldering to spare pads or assuming a given pad name matches the expected ESP32 GPIO.

## On-Device Documentation (NO/EN/NL/PT/ES/DE/IT)

User documentation served by the controller lives in:

- generated at `source/ESPEED32/data/docs/en/index.html`
- generated at `source/ESPEED32/data/docs/no/index.html`
- generated at `source/ESPEED32/data/docs/nl/index.html`
- generated at `source/ESPEED32/data/docs/pt/index.html`
- generated at `source/ESPEED32/data/docs/es/index.html`
- generated at `source/ESPEED32/data/docs/de/index.html`
- generated at `source/ESPEED32/data/docs/it/index.html`
- `source/ESPEED32/data/ui/index.html` (backup/restore web UI)

Recommended location is exactly this path under `source/ESPEED32/data/docs/` (do not move to repo root),
because ESP32 filesystem tooling expects the `data/` folder next to the sketch.

Docs source now lives in:

- `source/ESPEED32/docs_src/shared/`
- `source/ESPEED32/docs_src/en/main.html`
- `source/ESPEED32/docs_src/en/page.json`
- `source/ESPEED32/docs_src/no/main.html`
- `source/ESPEED32/docs_src/no/page.json`
- `source/ESPEED32/docs_src/nl/main.html`
- `source/ESPEED32/docs_src/nl/page.json`
- `source/ESPEED32/docs_src/pt/main.html`
- `source/ESPEED32/docs_src/pt/page.json`
- `source/ESPEED32/docs_src/es/main.html`
- `source/ESPEED32/docs_src/es/page.json`
- `source/ESPEED32/docs_src/de/main.html`
- `source/ESPEED32/docs_src/de/page.json`
- `source/ESPEED32/docs_src/it/main.html`
- `source/ESPEED32/docs_src/it/page.json`

Generate docs output locally with:

- `python3 scripts/build_docs.py`
- `./scripts/refresh_generated_docs.sh`

GitHub Pages also regenerates docs from `source/ESPEED32/docs_src/` in CI before publishing.

The firmware serves these files from SPIFFS at `/docs` in WiFi backup mode.
The WiFi backup root page (`/`) is also served from SPIFFS (`/ui/index.html`) with a minimal built-in fallback if the file is missing.

The docs include:

- full controller usage guide
- full UI overview
- full menu tree (main menu and all submenus)
- Norwegian, English, Spanish, German, and Italian versions

### Update workflow

1. Flash firmware + SPIFFS in one go (recommended after firmware/docs changes):
   - VS Code task: `ESPEED32: Flash firmware + SPIFFS`
   - or terminal: `./scripts/flash_all.sh`
2. Upload filesystem image (SPIFFS) only (when firmware is unchanged):
   - VS Code task: `ESPEED32: Upload SPIFFS docs`
   - or terminal: `./scripts/upload_spiffs.sh`
   - optional: purge SPIFFS region first: `./scripts/upload_spiffs.sh --purge` (asks for confirmation)
3. Open `http://192.168.4.1/docs` while the controller is in WiFi mode.

## Troubleshooting

### Web UI shows a blank page or 404 after firmware update

The firmware and filesystem (SPIFFS) must match. If you updated firmware only, the old SPIFFS is serving stale files.

Check `SETTINGS → ABOUT` on the controller:
- **Firmware** and **FS/UI** should show the same version number.
- If they differ, flash the SPIFFS image for the same release as the firmware.

The easiest fix is to use the automatic paired update in the Manage Controller page, which flashes both in one go.

### Current sense always reads zero with IBT-2 / BTS7960

The IBT-2 module does not include a sense resistor. The BTS7960 IS pin is a current output, not a voltage — connecting it directly to the ESP32 ADC gives nothing to measure.

Add a resistor externally:

```
IBT-2 IS_R pin → 1 kΩ → GND
                     ↑
                 GPIO25
```

Compile with `CURRENT_SENSE_PROFILE=CURRENT_SENSE_PROFILE_BTS7960`. The default tuning assumes a 1 kΩ sense resistor. Values may need tuning for your specific module — see [Motor Current Sense Profiles](#motor-current-sense-profiles).

### WiFi page loads but controller panel or docs return 404

Try these paths in order and note which ones work:

- `http://<ip>/` — public landing page
- `http://<ip>/docs` — user guide
- `http://<ip>/ui` — controller panel (requires login)

If `/` works but `/ui` does not, check that you are using the correct login. Default credentials are `espeed32 / espeed32` unless changed. The current password is always shown on the controller WiFi info screen.

If none of the paths load, the IP shown on the OLED is the most reliable address to use — avoid saved bookmarks or mDNS hostnames until basic connectivity is confirmed.
