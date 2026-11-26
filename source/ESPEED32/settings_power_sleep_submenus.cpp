#include "settings_power_sleep_submenus.h"
#include <Arduino.h>
#include "slot_ESC.h"
#include "ui_text_access.h"
#include "app_globals.h"
#include "power_ui.h"
#include "ui_render.h"
#include "menu_input.h"

static void formatConfiguredMenuLabel(const char* source, char* buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) return;
  buffer[0] = '\0';
  if (source == nullptr) return;

  if (g_storedVar.textCase != TEXT_CASE_PASCAL) {
    snprintf(buffer, bufferSize, "%s", source);
    return;
  }

  size_t out = 0;
  bool newWord = true;
  for (size_t i = 0; source[i] != '\0' && out + 1 < bufferSize; i++) {
    char c = source[i];
    if (c >= 'A' && c <= 'Z') {
      buffer[out++] = newWord ? c : (char)(c - 'A' + 'a');
      newWord = false;
    } else if (c >= 'a' && c <= 'z') {
      buffer[out++] = newWord ? (char)(c - 'a' + 'A') : c;
      newWord = false;
    } else {
      buffer[out++] = c;
      newWord = (c == ' ' || c == '/' || c == '=' || c == '-');
    }
  }
  buffer[out] = '\0';
}

/**
 * Sleep settings submenu: NOW (manual sleep) and INTERVAL (auto-sleep timeout).
 */
void showSleepSettings() {
  uint16_t lang = g_storedVar.language;

  /* Labels [NOR, ENG, ESP, DEU, ITA, NLD, POR, SWE] */
  const char* lblInterval[LANG_COUNT] = {"INTERV.", "INTERVAL", "INTERVALO", "INTERVAL", "INTERVAL", "INTERVAL", "INTERVAL", "INTERV."};
  const char* lblNow[LANG_COUNT]      = {"SOV NA",  "SLEEP NOW", "REPOSO YA", "JETZT RUH", "DORMI ORA", "SLAAP NU", "DORMIR JA", "SOV NU"};
  const uint8_t ITEM_NOW = 0;
  const uint8_t ITEM_INTERVAL = 1;
  const uint8_t ITEM_BACK = 2;
  const uint8_t NUM_ITEMS = 3;  /* 0=NOW, 1=INTERVAL, 2=BACK */
  const uint8_t menuFont  = FONT_8x8;
  const uint8_t charWidth = WIDTH8x8;
  const uint8_t lineH     = HEIGHT8x8;

  int8_t sel = 0;
  bool editing = false;
  uint16_t tmpTimeout = g_storedVar.powerSaveTimeout;
  bool needRedraw = true;

  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
  resetUiEncoder(0);

  uint32_t lastInteraction = millis();
  bool screensaverActive = false;
  uint16_t screensaverEncoderPos = (uint16_t)readUiEncoder();

  while (true) {
    bool wakeUp = refreshIdleInteractionFromControls(&lastInteraction, &screensaverActive, &screensaverEncoderPos);
    if (wakeUp) {
      obdFill(&g_obd, OBD_WHITE, 1);
      needRedraw = true;
    }

    if (consumeScreensaverWakeInput(wakeUp)) { continue; }

    if (!wakeUp && g_storedVar.screensaverTimeout > 0 &&
        millis() - lastInteraction > (g_storedVar.screensaverTimeout * 1000UL)) {
      if (g_escVar.trigger_norm == 0) {
        if (!screensaverActive) {
          screensaverActive = true;
          screensaverEncoderPos = readUiEncoder();
          showScreensaver();
        }
        if (serviceIdlePowerTransitions(&lastInteraction, &screensaverActive)) {
          obdFill(&g_obd, OBD_WHITE, 1);
          needRedraw = true;
        }
        delay(10);
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

    /* Encoder movement */
    if (g_rotaryEncoder.encoderChanged()) {
      lastInteraction = millis();
      if (editing) {
        tmpTimeout = (uint16_t)readUiEncoder();
      } else {
        sel = (int8_t)readUiEncoder();
      }
      needRedraw = true;
    }

    /* Encoder button */
    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      if (editing) {
        /* Confirm new interval */
        g_storedVar.powerSaveTimeout = tmpTimeout;
        saveEEPROM(g_storedVar);
        editing = false;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
        resetUiEncoder(ITEM_INTERVAL);
      } else if (sel == ITEM_INTERVAL) {
        /* Enter interval editing */
        editing = true;
        tmpTimeout = g_storedVar.powerSaveTimeout;
        g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
        setUiEncoderBoundaries(0, POWER_SAVE_TIMEOUT_MAX, false);
        resetUiEncoder(tmpTimeout);
      } else if (sel == ITEM_NOW) {
        /* Sleep NOW */
        showPowerSave();
        lastInteraction = millis();
        screensaverActive = false;
        needRedraw = true;
      } else if (sel == ITEM_BACK) {
        /* BACK */
        break;
      }
      needRedraw = true;
    }

    /* Brake button = cancel/back */
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      lastInteraction = millis();
      if (editing) {
        editing = false;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
        resetUiEncoder(ITEM_INTERVAL);
        needRedraw = true;
        delay(200);
      } else {
        waitForBrakeButtonRelease();
        break;
      }
    }

    /* Draw */
    if (needRedraw) {
      obdFill(&g_obd, OBD_WHITE, 1);

      /* Row 0: NOW */
      bool s0 = (!editing && sel == ITEM_NOW);
      char rowLabel[16];
      formatConfiguredMenuLabel(lblNow[lang], rowLabel, sizeof(rowLabel));
      obdWriteString(&g_obd, 0, 0, 0, rowLabel, menuFont, s0 ? OBD_WHITE : OBD_BLACK, 1);

      /* Row 1: INTERVAL + value */
      bool s1 = (!editing && sel == ITEM_INTERVAL);
      formatConfiguredMenuLabel(lblInterval[lang], rowLabel, sizeof(rowLabel));
      obdWriteString(&g_obd, 0, 0, lineH, rowLabel, menuFont, s1 ? OBD_WHITE : OBD_BLACK, 1);
      char valStr[8];
      uint16_t disp = editing ? tmpTimeout : g_storedVar.powerSaveTimeout;
      if (disp == 0) snprintf(valStr, sizeof(valStr), "  %s", getOnOffLabel(lang, 0));
      else           snprintf(valStr, sizeof(valStr), "%2dmin", disp);
      uint8_t vx = OLED_WIDTH - (uint8_t)(strlen(valStr) * charWidth);
      obdWriteString(&g_obd, 0, vx, lineH, valStr, menuFont, (s1 || editing) ? OBD_WHITE : OBD_BLACK, 1);

      /* Row 2: BACK */
      bool s2 = (!editing && sel == ITEM_BACK);
      obdWriteString(&g_obd, 0, 0, 2 * lineH, (char*)getBackLabel(lang), menuFont, s2 ? OBD_WHITE : OBD_BLACK, 1);

      needRedraw = false;
    }

    /* Long press = escape to main for race mode toggle */
    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }

    vTaskDelay(10);
  }
}

/**
 * Deep sleep settings submenu: NOW (manual deep sleep) and INTERVAL (auto deep-sleep timeout).
 * Uses hardcoded FONT_8x8 regardless of listFontSize.
 */
void showDeepSleepSettings() {
  uint16_t lang = g_storedVar.language;

  const char* lblInterval[LANG_COUNT] = {"INTERV.",   "INTERVAL",  "INTERVALO", "INTERVAL", "INTERVAL", "INTERVAL", "INTERVAL", "INTERV."};
  const char* lblNow[LANG_COUNT]      = {"SLUKK NA",  "SLEEP NOW", "APAGA YA",  "AUS JETZT", "SPEGNI ORA", "UIT NU", "DESLIG JA", "SLACK NU"};
  const uint8_t ITEM_NOW = 0;
  const uint8_t ITEM_INTERVAL = 1;
  const uint8_t ITEM_BACK = 2;
  const uint8_t NUM_ITEMS = 3;  /* 0=NOW, 1=INTERVAL, 2=BACK */

  int8_t sel = 0;
  bool editing = false;
  uint16_t tmpTimeout = g_storedVar.deepSleepTimeout;
  bool needRedraw = true;

  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
  resetUiEncoder(0);

  uint32_t lastInteraction = millis();
  bool screensaverActive = false;
  uint16_t screensaverEncoderPos = (uint16_t)readUiEncoder();

  while (true) {
    bool wakeUp = refreshIdleInteractionFromControls(&lastInteraction, &screensaverActive, &screensaverEncoderPos);
    if (wakeUp) {
      obdFill(&g_obd, OBD_WHITE, 1);
      needRedraw = true;
    }

    if (consumeScreensaverWakeInput(wakeUp)) { continue; }

    if (!wakeUp && g_storedVar.screensaverTimeout > 0 &&
        millis() - lastInteraction > (g_storedVar.screensaverTimeout * 1000UL)) {
      if (g_escVar.trigger_norm == 0) {
        if (!screensaverActive) {
          screensaverActive = true;
          screensaverEncoderPos = readUiEncoder();
          showScreensaver();
        }
        if (serviceIdlePowerTransitions(&lastInteraction, &screensaverActive)) {
          obdFill(&g_obd, OBD_WHITE, 1);
          needRedraw = true;
        }
        delay(10);
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

    if (g_rotaryEncoder.encoderChanged()) {
      lastInteraction = millis();
      if (editing) {
        tmpTimeout = (uint16_t)readUiEncoder();
      } else {
        sel = (int8_t)readUiEncoder();
      }
      needRedraw = true;
    }

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      if (editing) {
        g_storedVar.deepSleepTimeout = tmpTimeout;
        saveEEPROM(g_storedVar);
        editing = false;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
        resetUiEncoder(ITEM_INTERVAL);
      } else if (sel == ITEM_INTERVAL) {
        editing = true;
        tmpTimeout = g_storedVar.deepSleepTimeout;
        g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
        /* 0=OFF, DEEP_SLEEP_TIMEOUT_MIN-DEEP_SLEEP_TIMEOUT_MAX; encode 0 as OFF */
        setUiEncoderBoundaries(0, DEEP_SLEEP_TIMEOUT_MAX, false);
        resetUiEncoder(tmpTimeout);
      } else if (sel == ITEM_NOW) {
        /* Deep sleep NOW - never returns */
        showDeepSleep();
        needRedraw = true;
      } else if (sel == ITEM_BACK) {
        break;
      }
      needRedraw = true;
    }

    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      lastInteraction = millis();
      if (editing) {
        editing = false;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
        resetUiEncoder(ITEM_INTERVAL);
        needRedraw = true;
        delay(200);
      } else {
        waitForBrakeButtonRelease();
        break;
      }
    }

    if (needRedraw) {
      obdFill(&g_obd, OBD_WHITE, 1);

      /* Row 0: NOW */
      bool s0 = (!editing && sel == ITEM_NOW);
      char rowLabel[16];
      formatConfiguredMenuLabel(lblNow[lang], rowLabel, sizeof(rowLabel));
      obdWriteString(&g_obd, 0, 0, 0, rowLabel, FONT_8x8, s0 ? OBD_WHITE : OBD_BLACK, 1);

      /* Row 1: INTERVAL + value */
      bool s1 = (!editing && sel == ITEM_INTERVAL);
      formatConfiguredMenuLabel(lblInterval[lang], rowLabel, sizeof(rowLabel));
      obdWriteString(&g_obd, 0, 0, HEIGHT8x8, rowLabel, FONT_8x8, s1 ? OBD_WHITE : OBD_BLACK, 1);
      char valStr[8];
      uint16_t disp = editing ? tmpTimeout : g_storedVar.deepSleepTimeout;
      if (disp == 0) snprintf(valStr, sizeof(valStr), "  %s", getOnOffLabel(lang, 0));
      else           snprintf(valStr, sizeof(valStr), "%2dmin", disp);
      uint8_t vx = OLED_WIDTH - (uint8_t)(strlen(valStr) * WIDTH8x8);
      obdWriteString(&g_obd, 0, vx, HEIGHT8x8, valStr, FONT_8x8, (s1 || editing) ? OBD_WHITE : OBD_BLACK, 1);

      /* Row 2: BACK */
      bool s2 = (!editing && sel == ITEM_BACK);
      obdWriteString(&g_obd, 0, 0, 2 * HEIGHT8x8, (char*)getBackLabel(lang), FONT_8x8, s2 ? OBD_WHITE : OBD_BLACK, 1);

      needRedraw = false;
    }

    /* Long press = escape to main for race mode toggle */
    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }

    vTaskDelay(10);
  }
}
