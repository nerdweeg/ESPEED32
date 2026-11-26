#include "settings_sound_wifi_submenus.h"
#include <Arduino.h>
#include "slot_ESC.h"
#include "ui_text_access.h"
#include "connectivity_portal.h"
#include "telemetry_logging.h"
#include "HAL.h"
#include "app_globals.h"
#include "power_ui.h"
#include "ui_render.h"
#include "menu_input.h"

static void showTelemetryLoggingStartFailed() {
  obdFill(&g_obd, OBD_WHITE, 1);
  const char* title = "Log failed";
  obdWriteString(&g_obd, 0, centerX8x8(title), 8, (char*)title, FONT_8x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 8, 28, (char*)"Could not start", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 8, 36, (char*)"logging", FONT_6x8, OBD_BLACK, 1);
  delay(1100);
  obdFill(&g_obd, OBD_WHITE, 1);
}

static void showLoggingTransportHint() {
  obdFill(&g_obd, OBD_WHITE, 1);
  const char* title = "Logging ON";
  obdWriteString(&g_obd, 0, centerX8x8(title), 4, (char*)title, FONT_8x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 4, 20, (char*)"USB works now.", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 4, 30, (char*)"Start WiFi later", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 4, 40, (char*)"for live web view.", FONT_6x8, OBD_BLACK, 1);
  delay(1300);
  obdFill(&g_obd, OBD_WHITE, 1);
}

static bool startTelemetryLoggingNow() {
  if (!telemetryStartLogging(&g_storedVar,
                             g_antiSpinStepMs,
                             g_encoderInvertEnabled,
                             g_adcVoltageRange_mV,
                             (uint8_t)g_carSel)) {
    showTelemetryLoggingStartFailed();
    return false;
  }

  return true;
}

/**
 * Sound settings submenu: BOOT (on/off), RACE (on/off), BACK.
 * BOOT controls startup/calibration/on/off sounds.
 * RACE controls the race mode toggle beep.
 */
void showSoundSettings() {
  const uint8_t SND_ITEMS = SOUND_ITEMS_COUNT;  /* 3: BOOT, RACE, BACK */
  const uint8_t menuFont  = FONT_8x8;
  const uint8_t lineH     = HEIGHT8x8;

  obdFill(&g_obd, OBD_WHITE, 1);

  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(1, SND_ITEMS, false);
  resetUiEncoder(1);

  uint8_t sel = 1;
  uint8_t prevSel = 0xFF;
  bool needRedraw = true;

  uint32_t lastInteraction = millis();
  bool ssActive = false;
  uint16_t ssEncoderPos = (uint16_t)readUiEncoder();

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
      sel = (uint8_t)readUiEncoder();
      needRedraw = true;
    }

    /* Encoder button click */
    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      if (sel == SND_ITEMS) {  /* BACK */
        break;
      } else if (sel == 1) {  /* BOOT toggle */
        g_storedVar.soundBoot = g_storedVar.soundBoot ? 0 : 1;
        saveEEPROM(g_storedVar);
      } else if (sel == 2) {  /* RACE toggle */
        g_storedVar.soundRace = g_storedVar.soundRace ? 0 : 1;
        saveEEPROM(g_storedVar);
      }
      needRedraw = true;
      delay(200);
    }

    /* Brake button = back */
    static BrakeButtonEdge brakeBtnInSound;
    if (brakeBtnInSound.pressed()) {
      waitForBrakeButtonRelease();
      break;
    }

    /* Long press = escape to main for race mode toggle */
    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }

    /* Draw */
    if (needRedraw || sel != prevSel) {
      needRedraw = false;
      prevSel = sel;
      uint8_t lang = g_storedVar.language;
      for (uint8_t i = 0; i < SND_ITEMS; i++) {
        bool isSelected = (sel == i + 1);
        obdWriteString(&g_obd, 0, 0, i * lineH, (char*)getSoundMenuName(lang, i),
                       menuFont, isSelected ? OBD_WHITE : OBD_BLACK, 1);
        /* Show ON/OFF value on right for BOOT (i=0) and RACE (i=1) */
        if (i < 2) {
          uint16_t state = (i == 0) ? g_storedVar.soundBoot : g_storedVar.soundRace;
          sprintf(msgStr, "%3s", getOnOffLabel(lang, state));
          uint8_t vx = OLED_WIDTH - (uint8_t)(strlen(msgStr) * WIDTH8x8);
          obdWriteString(&g_obd, 0, vx, i * lineH, msgStr,
                         menuFont, isSelected ? OBD_WHITE : OBD_BLACK, 1);
        }
      }
    }

    vTaskDelay(10);
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}

/**
 * WiFi submenu.
 * Items: START/STOP WIFI, MODE, INFO PAGE, SHOW QR (AP only), AUTO OFF, BACK.
 */
static void showWiFiClientSetupRequiredScreen() {
  obdFill(&g_obd, OBD_WHITE, 1);
  const char* title = "Client mode";
  obdWriteString(&g_obd, 0, centerX8x8(title), 0, (char*)title, FONT_8x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 2 * HEIGHT8x8, (char*)"Set WiFi SSID", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 3 * HEIGHT8x8, (char*)"and password in", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 4 * HEIGHT8x8, (char*)"the web UI first", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 6 * HEIGHT8x8, (char*)"Press to exit", FONT_6x8, OBD_BLACK, 1);

  while (!g_rotaryEncoder.isEncoderButtonClicked()) {
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      delay(BUTTON_SHORT_PRESS_DEBOUNCE_MS);
      break;
    }
    vTaskDelay(1);
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}

void showWiFiSettings() {
  uint16_t lang = g_storedVar.language;

  const char* lblOpenUpper[LANG_COUNT]  = {"INFO SIDE", "INFO PAGE", "INFO PAGE", "INFO PAGE", "PAG INFO", "INFOSITE", "PAG INFO", "INFO PAG", "PAG INFO", "INFO SIDA"};
  const char* lblOpenPascal[LANG_COUNT] = {"Info side", "Info page", "Info page", "Info page", "Pag info", "Infosite", "Pag info", "Info pag", "Pag info", "Info sida"};
  const char* lblTimerUpper[LANG_COUNT] = {"AUTO AV",   "AUTO OFF",  "AUTO OFF",  "AUTO OFF",  "AUTO OFF", "AUTO AUS", "AUTO OFF", "AUTO UIT", "AUTO OFF", "AUTO AV"};
  const char* lblTimerPascal[LANG_COUNT]= {"Auto av",   "Auto off",  "Auto off",  "Auto off",  "Auto off", "Auto aus", "Auto off", "Auto uit", "Auto off", "Auto av"};
  const char* lblModeUpper[LANG_COUNT]  = {"MODUS",     "MODE",      "MODE",      "MODE",      "MODO",      "MODUS",    "MODO",      "MODE",     "MODO", "LAGE"};
  const char* lblModePascal[LANG_COUNT] = {"Modus",     "Mode",      "Mode",      "Mode",      "Modo",      "Modus",    "Modo",      "Mode",     "Modo", "Lage"};
  const char* lblStartUpper[LANG_COUNT] = {"START WIFI", "START WIFI", "START WIFI", "START WIFI", "INIC WIFI", "START WIFI", "AVVIA WIFI", "START WIFI", "INIC WIFI", "START WIFI"};
  const char* lblStartPascal[LANG_COUNT]= {"Start WiFi", "Start WiFi", "Start WiFi", "Start WiFi", "Inic WiFi", "Start WiFi", "Avvia WiFi", "Start WiFi", "Inic WiFi", "Start WiFi"};
  const char* lblStopUpper[LANG_COUNT]  = {"STOPP WIFI", "STOP WIFI",  "STOP WIFI",  "STOP WIFI",  "STOP WIFI", "STOP WIFI",  "STOP WIFI",  "STOP WIFI",  "STOP WIFI", "STOPP WIFI"};
  const char* lblStopPascal[LANG_COUNT] = {"Stopp WiFi", "Stop WiFi",  "Stop WiFi",  "Stop WiFi",  "Stop WiFi", "Stop WiFi",  "Stop WiFi",  "Stop WiFi",  "Stop WiFi", "Stopp WiFi"};
  const char* lblQrUpper[LANG_COUNT]    = {"VIS QR",     "SHOW QR",    "SHOW QR",    "SHOW QR",    "VER QR",    "ZEIG QR",    "MOSTRA QR",  "TOON QR",    "VER QR", "VISA QR"};
  const char* lblQrPascal[LANG_COUNT]   = {"Vis QR",     "Show QR",    "Show QR",    "Show QR",    "Ver QR",    "Zeig QR",    "Mostra QR",  "Toon QR",    "Ver QR", "Visa QR"};
  const char* const* lblOpen   = (g_storedVar.textCase == TEXT_CASE_PASCAL) ? lblOpenPascal : lblOpenUpper;
  const char* const* lblTimer  = (g_storedVar.textCase == TEXT_CASE_PASCAL) ? lblTimerPascal : lblTimerUpper;
  const char* const* lblMode   = (g_storedVar.textCase == TEXT_CASE_PASCAL) ? lblModePascal : lblModeUpper;
  const char* const* lblStartBg= (g_storedVar.textCase == TEXT_CASE_PASCAL) ? lblStartPascal : lblStartUpper;
  const char* const* lblStopBg = (g_storedVar.textCase == TEXT_CASE_PASCAL) ? lblStopPascal : lblStopUpper;
  const char* const* lblQr     = (g_storedVar.textCase == TEXT_CASE_PASCAL) ? lblQrPascal : lblQrUpper;

  const uint8_t ITEM_ACTIVE = 0;
  const uint8_t ITEM_MODE = 1;
  const uint8_t ITEM_INFO = 2;
  const uint8_t menuFont = FONT_8x8;
  const uint8_t lineH = HEIGHT8x8;

  int8_t sel = 0;
  bool editing = false;
  uint16_t tmpMinutes = constrain(getWiFiTimedMinutes(), 1, 120);
  bool needRedraw = true;
  uint8_t itemQr = 3;
  uint8_t itemTimer = 4;
  uint8_t itemBack = 5;
  uint8_t numItems = 6;

  auto getShownPortalMode = [&]() -> uint16_t {
    if (isWiFiPortalActive()) {
      return getActiveWiFiPortalMode();
    }
    return (getConfiguredWiFiMode() == WIFI_CONFIG_HOME) ? WIFI_PORTAL_HOME : WIFI_PORTAL_AP;
  };

  auto updateMenuLayout = [&]() {
    bool showQr = (getShownPortalMode() == WIFI_PORTAL_AP);
    itemQr = 3;
    itemTimer = showQr ? 4 : 3;
    itemBack = showQr ? 5 : 4;
    numItems = showQr ? 6 : 5;
    if (!editing) {
      if (sel >= (int8_t)numItems) {
        sel = (int8_t)(numItems - 1);
      }
      setUiEncoderBoundaries(0, numItems - 1, false);
      resetUiEncoder(sel);
    }
  };

  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  updateMenuLayout();

  uint32_t lastInteraction = millis();
  bool screensaverActive = false;
  uint16_t screensaverEncoderPos = (uint16_t)readUiEncoder();

  while (true) {
    serviceTimedWiFiPortal();

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
        tmpMinutes = (uint16_t)readUiEncoder();
      } else {
        sel = (int8_t)readUiEncoder();
      }
      needRedraw = true;
    }

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      if (editing) {
        setWiFiTimedMinutes(tmpMinutes);
        editing = false;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        updateMenuLayout();
        resetUiEncoder(itemTimer);
      } else if (sel == ITEM_ACTIVE) {
        if (isWiFiPortalActive()) {
          stopTimedWiFiPortal();
        } else {
          startTimedWiFiPortal(getWiFiTimedMinutes());
        }
        updateMenuLayout();
      } else if (sel == ITEM_MODE) {
        uint16_t nextMode = (getConfiguredWiFiMode() == WIFI_CONFIG_HOME) ? WIFI_CONFIG_AP : WIFI_CONFIG_HOME;
        if (nextMode == WIFI_CONFIG_HOME && !hasConfiguredWiFiClientCredentials()) {
          showWiFiClientSetupRequiredScreen();
          lastInteraction = millis();
          screensaverActive = false;
          needRedraw = true;
          delay(120);
          continue;
        }
        bool restartPortal = isWiFiPortalActive();
        uint16_t restartMinutes = constrain(getWiFiTimedMinutes(), 1, 120);
        if (restartPortal) {
          stopTimedWiFiPortal();
        }
        setConfiguredWiFiMode(nextMode);
        saveEEPROM(g_storedVar);
        if (restartPortal) {
          startTimedWiFiPortal(restartMinutes);
        }
        updateMenuLayout();
      } else if (sel == ITEM_INFO) {
        showWiFiPortalScreen();
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        updateMenuLayout();
        resetUiEncoder(sel);
        lastInteraction = millis();
        screensaverActive = false;
      } else if (getShownPortalMode() == WIFI_PORTAL_AP && sel == itemQr) {
        showWiFiQrScreen();
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        updateMenuLayout();
        resetUiEncoder(sel);
        lastInteraction = millis();
        screensaverActive = false;
      } else if (sel == itemTimer) {
        editing = true;
        tmpMinutes = constrain(getWiFiTimedMinutes(), 1, 120);
        g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
        setUiEncoderBoundaries(1, 120, false);
        resetUiEncoder(tmpMinutes);
      } else if (sel == itemBack) {
        break;
      }
      needRedraw = true;
      delay(120);
    }

    static BrakeButtonEdge brakeBtnInWifi;
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      lastInteraction = millis();
    }
    if (brakeBtnInWifi.pressed()) {
      if (editing) {
        editing = false;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        updateMenuLayout();
        resetUiEncoder(itemTimer);
        needRedraw = true;
      } else {
        waitForBrakeButtonRelease();
        break;
      }
    }

    if (needRedraw) {
      obdFill(&g_obd, OBD_WHITE, 1);

      bool s0 = (!editing && sel == ITEM_ACTIVE);
      const char* actionStr = isWiFiPortalActive() ? lblStopBg[lang] : lblStartBg[lang];
      obdWriteString(&g_obd, 0, 0, 0 * lineH, (char*)actionStr, menuFont, s0 ? OBD_WHITE : OBD_BLACK, 1);

      bool s1 = (!editing && sel == ITEM_INFO);
      bool sMode = (!editing && sel == ITEM_MODE);
      bool showQr = (getShownPortalMode() == WIFI_PORTAL_AP);
      obdWriteString(&g_obd, 0, 0, 1 * lineH, (char*)lblMode[lang], menuFont, sMode ? OBD_WHITE : OBD_BLACK, 1);
      const char* modeValue = (getShownPortalMode() == WIFI_PORTAL_HOME) ? "CLIENT" : "AP";
      uint8_t modeX = OLED_WIDTH - (uint8_t)(strlen(modeValue) * WIDTH8x8);
      obdWriteString(&g_obd, 0, modeX, 1 * lineH, (char*)modeValue, menuFont, sMode ? OBD_WHITE : OBD_BLACK, 1);

      obdWriteString(&g_obd, 0, 0, 2 * lineH, (char*)lblOpen[lang], menuFont, s1 ? OBD_WHITE : OBD_BLACK, 1);

      uint8_t timerRow = showQr ? 4 : 3;
      uint8_t backRow = showQr ? 5 : 4;
      bool sQr = (!editing && showQr && sel == itemQr);
      if (showQr) {
        obdWriteString(&g_obd, 0, 0, 3 * lineH, (char*)lblQr[lang], menuFont, sQr ? OBD_WHITE : OBD_BLACK, 1);
      }

      bool s3 = (!editing && sel == itemTimer);
      obdWriteString(&g_obd, 0, 0, timerRow * lineH, (char*)lblTimer[lang], menuFont, s3 ? OBD_WHITE : OBD_BLACK, 1);
      char minutesStr[10];
      uint16_t shownMinutes = editing ? tmpMinutes : constrain(getWiFiTimedMinutes(), 1, 120);
      snprintf(minutesStr, sizeof(minutesStr), "%3dm", shownMinutes);
      uint8_t mx = OLED_WIDTH - (uint8_t)(strlen(minutesStr) * WIDTH8x8);
      obdWriteString(&g_obd, 0, mx, timerRow * lineH, minutesStr, menuFont, (s3 || editing) ? OBD_WHITE : OBD_BLACK, 1);

      bool s4 = (!editing && sel == itemBack);
      obdWriteString(&g_obd, 0, 0, backRow * lineH, (char*)getBackLabel(lang), menuFont, s4 ? OBD_WHITE : OBD_BLACK, 1);

      needRedraw = false;
    }

    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }
    vTaskDelay(10);
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}

void showLoggingSettings() {
  uint16_t lang = g_storedVar.language;

  const char* lblTimerUpper[LANG_COUNT]    = {"AUTO AV",   "AUTO OFF",  "AUTO OFF",  "AUTO OFF",  "AUTO OFF", "AUTO AUS", "AUTO OFF", "AUTO UIT", "AUTO OFF", "AUTO AV"};
  const char* lblTimerPascal[LANG_COUNT]   = {"Auto av",   "Auto off",  "Auto off",  "Auto off",  "Auto off", "Auto aus", "Auto off", "Auto uit", "Auto off", "Auto av"};
  const char* lblStartUpper[LANG_COUNT]    = {"START NA",  "START NOW", "START NOW", "START NOW", "INIC AHOR", "START NOW", "AVVIA ORA", "START NU", "INIC AGORA", "START NU"};
  const char* lblStartPascal[LANG_COUNT]   = {"Start nå",  "Start now", "Start now", "Start now", "Inic ahor", "Start now", "Avvia ora", "Start nu", "Inic agora", "Start nu"};
  const char* lblStopUpper[LANG_COUNT]     = {"STOPP NA",  "STOP NOW",  "STOP NOW",  "STOP NOW",  "PARA AHOR", "STOP NOW",  "STOP ORA",  "STOP NU",  "STOP AGORA", "STOPP NU"};
  const char* lblStopPascal[LANG_COUNT]    = {"Stopp nå",  "Stop now",  "Stop now",  "Stop now",  "Para ahor", "Stop now",  "Stop ora",  "Stop nu",  "Stop agora", "Stopp nu"};
  const char* const* lblTimer     = (g_storedVar.textCase == TEXT_CASE_PASCAL) ? lblTimerPascal : lblTimerUpper;
  const char* const* lblStartNow  = (g_storedVar.textCase == TEXT_CASE_PASCAL) ? lblStartPascal : lblStartUpper;
  const char* const* lblStopNow   = (g_storedVar.textCase == TEXT_CASE_PASCAL) ? lblStopPascal : lblStopUpper;

  const uint8_t ITEM_ACTIVE = 0;
  const uint8_t ITEM_TIMER = 1;
  const uint8_t ITEM_BACK = 2;
  const uint8_t NUM_ITEMS = 3;
  const uint8_t menuFont = FONT_8x8;
  const uint8_t lineH = HEIGHT8x8;

  int8_t sel = 0;
  bool editing = false;
  uint16_t tmpMinutes = constrain(getTelemetryTimedMinutes(), 1, 120);
  bool needRedraw = true;

  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
  resetUiEncoder(0);

  uint32_t lastInteraction = millis();
  bool screensaverActive = false;
  uint16_t screensaverEncoderPos = (uint16_t)readUiEncoder();

  while (true) {
    serviceTimedWiFiPortal();

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
        tmpMinutes = (uint16_t)readUiEncoder();
      } else {
        sel = (int8_t)readUiEncoder();
      }
      needRedraw = true;
    }

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      if (editing) {
        setTelemetryTimedMinutes(tmpMinutes);
        editing = false;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
        resetUiEncoder(ITEM_TIMER);
      } else if (sel == ITEM_ACTIVE) {
        if (telemetryIsLoggingActive()) {
          telemetryStopLogging();
          stopTimedTelemetryLogging();
        } else {
          uint16_t loggingMinutes = constrain(getTelemetryTimedMinutes(), 1, 120);
          bool wifiWasActive = isWiFiPortalActive();
          if (startTelemetryLoggingNow()) {
            startTimedTelemetryLogging(loggingMinutes);
            if (!wifiWasActive) {
              showLoggingTransportHint();
              lastInteraction = millis();
              screensaverActive = false;
            }
          }
        }
      } else if (sel == ITEM_TIMER) {
        editing = true;
        tmpMinutes = constrain(getTelemetryTimedMinutes(), 1, 120);
        g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
        setUiEncoderBoundaries(1, 120, false);
        resetUiEncoder(tmpMinutes);
      } else if (sel == ITEM_BACK) {
        break;
      }
      needRedraw = true;
      delay(120);
    }

    static BrakeButtonEdge brakeBtnInLogging;
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      lastInteraction = millis();
    }
    if (brakeBtnInLogging.pressed()) {
      if (editing) {
        editing = false;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
        resetUiEncoder(ITEM_TIMER);
        needRedraw = true;
      } else {
        waitForBrakeButtonRelease();
        break;
      }
    }

    if (needRedraw) {
      obdFill(&g_obd, OBD_WHITE, 1);

      bool s0 = (!editing && sel == ITEM_ACTIVE);
      const char* actionStr = telemetryIsLoggingActive() ? lblStopNow[lang] : lblStartNow[lang];
      obdWriteString(&g_obd, 0, 0, 0 * lineH, (char*)actionStr, menuFont, s0 ? OBD_WHITE : OBD_BLACK, 1);
      sprintf(msgStr, "%3s", getOnOffLabel(lang, telemetryIsLoggingActive() ? 1 : 0));
      uint8_t ax = OLED_WIDTH - (uint8_t)(strlen(msgStr) * WIDTH8x8);
      obdWriteString(&g_obd, 0, ax, 0 * lineH, msgStr, menuFont, s0 ? OBD_WHITE : OBD_BLACK, 1);

      bool s1 = (!editing && sel == ITEM_TIMER);
      obdWriteString(&g_obd, 0, 0, 1 * lineH, (char*)lblTimer[lang], menuFont, s1 ? OBD_WHITE : OBD_BLACK, 1);
      char minutesStr[10];
      uint16_t shownMinutes = editing ? tmpMinutes : constrain(getTelemetryTimedMinutes(), 1, 120);
      snprintf(minutesStr, sizeof(minutesStr), "%3dm", shownMinutes);
      uint8_t mx = OLED_WIDTH - (uint8_t)(strlen(minutesStr) * WIDTH8x8);
      obdWriteString(&g_obd, 0, mx, 1 * lineH, minutesStr, menuFont, (s1 || editing) ? OBD_WHITE : OBD_BLACK, 1);

      bool s2 = (!editing && sel == ITEM_BACK);
      obdWriteString(&g_obd, 0, 0, 2 * lineH, (char*)getBackLabel(lang), menuFont, s2 ? OBD_WHITE : OBD_BLACK, 1);

      needRedraw = false;
    }

    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }
    vTaskDelay(10);
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}
