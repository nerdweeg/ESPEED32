#include "settings_power_menu.h"
#include <Arduino.h>
#include "slot_ESC.h"
#include "ui_text_access.h"
#include "settings_display_submenus.h"
#include "settings_power_sleep_submenus.h"
#include "app_globals.h"
#include "power_ui.h"
#include "ui_render.h"
#include "menu_input.h"

static void formatVoltageString(char* buffer, size_t bufferSize, uint16_t millivolts) {
  snprintf(buffer, bufferSize, "%2u.%02uV", millivolts / 1000U, (millivolts % 1000U) / 10U);
}

static void showVinCalibrationScreen() {
  uint16_t lang = g_storedVar.language;

  const char* lblRead[9]   = {"LES", "READ", "READ", "READ", "LEE", "IST", "LETT", "LEES", "LER"};
  const char* lblSet[9]    = {"SETT", "SET", "SET", "SET", "SET", "SET", "SET", "SET", "SET"};
  const char* lblSave[9]   = {"TRYKK=LAGR", "CLICK=SAVE", "CLICK=SAVE", "CLICK=SAVE", "CLICK=SAVE", "CLICK=SAVE", "CLICK=SAVE", "KLIK=OPSL", "CLIQUE=GRAV"};
  const char* lblCancel[9] = {"BREMS=TILB", "BRAKE=BACK", "BRAKE=BACK", "BRAKE=BACK", "FRENO=ATR", "BREMSE=ZRK", "FRENO=BACK", "REM=TERUG", "TRAV=VOLT"};
  const char* lblNoVin[9]  = {"INGEN VIN", "NO VIN", "NO VIN", "NO VIN", "SIN VIN", "KEIN VIN", "NO VIN", "GEEN VIN", "SEM VIN"};

  const uint8_t menuFont = FONT_8x8;
  const uint8_t lineH = HEIGHT8x8;
  const long minTick = VIN_CAL_TARGET_MIN_MVOLTS / VIN_CAL_TARGET_STEP_MVOLTS;
  const long maxTick = VIN_CAL_TARGET_MAX_MVOLTS / VIN_CAL_TARGET_STEP_MVOLTS;

  uint16_t initialTarget_mV = constrain(
    (g_escVar.Vin_mV >= VIN_CAL_TARGET_MIN_MVOLTS && g_escVar.Vin_mV <= VIN_CAL_TARGET_MAX_MVOLTS)
      ? g_escVar.Vin_mV
      : 12000U,
    VIN_CAL_TARGET_MIN_MVOLTS,
    VIN_CAL_TARGET_MAX_MVOLTS);
  uint16_t target_mV = (initialTarget_mV / VIN_CAL_TARGET_STEP_MVOLTS) * VIN_CAL_TARGET_STEP_MVOLTS;
  bool showNoVinWarning = false;
  uint32_t noVinWarningUntil = 0;
  bool needRedraw = true;

  g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
  setUiEncoderBoundaries(minTick, maxTick, false);
  resetUiEncoder(target_mV / VIN_CAL_TARGET_STEP_MVOLTS);

  uint32_t lastInteraction = millis();
  bool screensaverActive = false;
  uint16_t screensaverEncoderPos = (uint16_t)readUiEncoder();

  while (true) {
    bool wakeUp = refreshIdleInteractionFromControls(&lastInteraction, &screensaverActive, &screensaverEncoderPos);
    if (wakeUp) {
      setLastEncoderInteraction(lastInteraction);
      obdFill(&g_obd, OBD_WHITE, 1);
      needRedraw = true;
    }

    if (consumeScreensaverWakeInput(wakeUp)) { continue; }

    if (!wakeUp && !screensaverActive && g_storedVar.screensaverTimeout > 0 &&
        millis() - lastInteraction > (g_storedVar.screensaverTimeout * 1000UL)) {
      if (g_escVar.trigger_norm == 0) {
        screensaverActive = true;
        screensaverEncoderPos = readUiEncoder();
        showScreensaver();
        continue;
      }
    }

    if (!wakeUp && screensaverActive) {
      if (serviceIdlePowerTransitions(&lastInteraction, &screensaverActive)) {
        obdFill(&g_obd, OBD_WHITE, 1);
        needRedraw = true;
      }
      delay(10);
      continue;
    }

    if (showNoVinWarning && millis() > noVinWarningUntil) {
      showNoVinWarning = false;
      needRedraw = true;
    }

    if (g_rotaryEncoder.encoderChanged()) {
      lastInteraction = millis();
      setLastEncoderInteraction(lastInteraction);
      target_mV = (uint16_t)readUiEncoder() * VIN_CAL_TARGET_STEP_MVOLTS;
      needRedraw = true;
    }

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      setLastEncoderInteraction(lastInteraction);

      uint16_t measuredVin_mV = g_escVar.Vin_mV;
      if (measuredVin_mV < 1000U) {
        showNoVinWarning = true;
        noVinWarningUntil = millis() + 1200UL;
        needRedraw = true;
      } else {
        uint32_t scaledRange = ((uint32_t)g_adcVoltageRange_mV * target_mV + (measuredVin_mV / 2U)) / measuredVin_mV;
        applyAdcVoltageRangeMilliVolts((uint16_t)scaledRange);
        saveEEPROM(g_storedVar);
        break;
      }
    }

    static BrakeButtonEdge brakeBtnInVinCal;
    if (brakeBtnInVinCal.pressed()) {
      waitForBrakeButtonRelease();
      break;
    }

    if (needRedraw) {
      char voltageStr[12];
      char lineBuf[17];
      const char* title = getPowerMenuName(lang, 4);

      obdFill(&g_obd, OBD_WHITE, 1);

      uint8_t titleX = (OLED_WIDTH > (strlen(title) * WIDTH8x8))
        ? (uint8_t)((OLED_WIDTH - (strlen(title) * WIDTH8x8)) / 2U)
        : 0;
      obdWriteString(&g_obd, 0, titleX, 0, (char*)title, menuFont, OBD_BLACK, 1);

      formatVoltageString(voltageStr, sizeof(voltageStr), g_escVar.Vin_mV);
      snprintf(lineBuf, sizeof(lineBuf), "%-4s%s", lblRead[lang], voltageStr);
      obdWriteString(&g_obd, 0, 0, 2 * lineH, lineBuf, menuFont, OBD_BLACK, 1);

      formatVoltageString(voltageStr, sizeof(voltageStr), target_mV);
      snprintf(lineBuf, sizeof(lineBuf), "%-4s%s", lblSet[lang], voltageStr);
      obdWriteString(&g_obd, 0, 0, 4 * lineH, lineBuf, menuFont, OBD_WHITE, 1);

      obdWriteString(&g_obd, 0, 0, 6 * lineH,
                     (char*)(showNoVinWarning ? lblNoVin[lang] : lblSave[lang]),
                     menuFont, OBD_BLACK, 1);
      obdWriteString(&g_obd, 0, 0, 7 * lineH, (char*)lblCancel[lang], menuFont, OBD_BLACK, 1);

      needRedraw = false;
    }

    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }

    vTaskDelay(10);
  }
}

/**
 * Power settings submenu: SCRSV, SLEEP, DEEP SLEEP, STARTUP DELAY, VIN CAL, BACK.
 * SCRSV/SLEEP/DEEP SLEEP open submenus; STARTUP DELAY is edited inline; VIN CAL opens a dedicated calibration screen.
 */
void showPowerSettings() {
  uint16_t lang = g_storedVar.language;
  const uint8_t NUM = POWER_ITEMS_COUNT;  /* 6: SCRSV, SLEEP, DEEP SLEEP, STARTUP, VIN CAL, BACK */
  const uint8_t menuFont = FONT_8x8;
  const uint8_t lineH    = HEIGHT8x8;

  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, NUM - 1, false);
  resetUiEncoder(0);

  uint8_t sel = 0;
  bool editing = false;
  uint16_t tmpDelay = g_storedVar.startupDelay;
  uint16_t origDelay = g_storedVar.startupDelay;
  bool needRedraw = true;
  uint32_t lastInteraction = millis();
  bool ssActive = false;
  uint16_t ssEncoderPos = (uint16_t)readUiEncoder();

  auto resumeAfterChildMenu = [&](uint8_t selectedIndex) {
    lastInteraction = millis();
    setLastEncoderInteraction(lastInteraction);
    ssActive = false;
    ssEncoderPos = readUiEncoder();
    g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
    setUiEncoderBoundaries(0, NUM - 1, false);
    resetUiEncoder(selectedIndex);
    obdFill(&g_obd, OBD_WHITE, 1);
    needRedraw = true;
  };

  while (true) {
    bool wakeUp = refreshIdleInteractionFromControls(&lastInteraction, &ssActive, &ssEncoderPos);
    if (wakeUp) {
      obdFill(&g_obd, OBD_WHITE, 1);
      needRedraw = true;
    }

    if (consumeScreensaverWakeInput(wakeUp)) { continue; }

    /* Screensaver timeout */
    if (!wakeUp && !ssActive && g_storedVar.screensaverTimeout > 0 &&
        millis() - lastInteraction > (g_storedVar.screensaverTimeout * 1000UL)) {
      if (g_escVar.trigger_norm == 0) {
        if (!ssActive) {
          ssActive = true;
          ssEncoderPos = readUiEncoder();
          showScreensaver();
        }
        if (serviceIdlePowerTransitions(&lastInteraction, &ssActive)) {
          obdFill(&g_obd, OBD_WHITE, 1);
          needRedraw = true;
        }
        delay(10);
        continue;
      }
    }

    if (!wakeUp && ssActive) {
      if (serviceIdlePowerTransitions(&lastInteraction, &ssActive)) {
        obdFill(&g_obd, OBD_WHITE, 1);
        needRedraw = true;
      }
      delay(10);
      continue;
    }

    /* Encoder movement */
    if (g_rotaryEncoder.encoderChanged()) {
      lastInteraction = millis();
      if (editing) {
        tmpDelay = (uint16_t)readUiEncoder();
      } else {
        sel = (uint8_t)readUiEncoder();
      }
      needRedraw = true;
    }

    /* Encoder button click */
    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      if (editing) {
        /* Confirm startup delay */
        g_storedVar.startupDelay = tmpDelay;
        saveEEPROM(g_storedVar);
        editing = false;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, NUM - 1, false);
        resetUiEncoder(3);
        needRedraw = true;
      } else if (sel == 0) {  /* SCRSV */
        showScreensaverSettings();
        if (isEscapeToMainRequested()) break;
        resumeAfterChildMenu(0);
      } else if (sel == 1) {  /* SLEEP */
        showSleepSettings();
        if (isEscapeToMainRequested()) break;
        resumeAfterChildMenu(1);
      } else if (sel == 2) {  /* DEEP SLEEP */
        showDeepSleepSettings();
        if (isEscapeToMainRequested()) break;
        resumeAfterChildMenu(2);
      } else if (sel == 3) {  /* STARTUP DELAY - enter edit */
        editing = true;
        origDelay = g_storedVar.startupDelay;
        tmpDelay  = g_storedVar.startupDelay;
        g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
        setUiEncoderBoundaries(STARTUP_DELAY_MIN, STARTUP_DELAY_MAX, false);
        resetUiEncoder(tmpDelay);
        needRedraw = true;
      } else if (sel == 4) {  /* VIN CAL */
        showVinCalibrationScreen();
        if (isEscapeToMainRequested()) break;
        resumeAfterChildMenu(4);
      } else {  /* BACK */
        break;
      }
    }

    /* Brake button = cancel edit or back */
    static BrakeButtonEdge brakeBtnInPower;
    if (brakeBtnInPower.pressed()) {
      if (editing) {
        g_storedVar.startupDelay = origDelay;
        tmpDelay = origDelay;
        editing = false;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, NUM - 1, false);
        resetUiEncoder(3);
        needRedraw = true;
      } else {
        waitForBrakeButtonRelease();
        break;
      }
    }

    /* Draw menu */
    if (needRedraw) {
      obdFill(&g_obd, OBD_WHITE, 1);
      for (uint8_t i = 0; i < NUM; i++) {
        bool isHighlighted = editing ? (i == 3) : (sel == i);
        obdWriteString(&g_obd, 0, 0, i * lineH, (char*)getPowerMenuName(lang, i),
                       menuFont, isHighlighted ? OBD_WHITE : OBD_BLACK, 1);
        /* Show startup delay value on the right for item 3 */
        if (i == 3) {
          char valStr[7];
          uint16_t disp = editing ? tmpDelay : g_storedVar.startupDelay;
          if (disp == 0) snprintf(valStr, sizeof(valStr), "   %s", getOnOffLabel(lang, 0));
          else           snprintf(valStr, sizeof(valStr), "%3dms", disp * 10);
          uint8_t vx = OLED_WIDTH - (uint8_t)(strlen(valStr) * WIDTH8x8);
          obdWriteString(&g_obd, 0, vx, i * lineH, valStr, menuFont,
                         isHighlighted ? OBD_WHITE : OBD_BLACK, 1);
        }
      }
      needRedraw = false;
    }

    /* Long press = escape to main for race mode toggle */
    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }

    vTaskDelay(10);
  }
}
