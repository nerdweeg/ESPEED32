#include "settings_lock_menu.h"
#include <Arduino.h>
#include "slot_ESC.h"
#include "ui_text_access.h"
#include "app_globals.h"
#include "power_ui.h"
#include "ui_render.h"
#include "menu_input.h"

/**
 * Lock settings submenu.
 * Items: MENU ITEM (on/off), SHORTCUT (OFF / 1–10 s), CONFIRM (on/off), BACK.
 * MENU ITEM: show LOCK entry in main menu.
 * SHORTCUT:  brake-hold duration to toggle lock (0=disabled, index = seconds).
 * CONFIRM:   show fullscreen LOCKED/UNLOCKED flash after toggling.
 */
void showLockSettings() {
  const uint8_t ITEM_MENU    = 0;
  const uint8_t ITEM_SHORT   = 1;
  const uint8_t ITEM_CONFIRM = 2;
  const uint8_t ITEM_BACK    = 3;
  const uint8_t NUM_ITEMS    = LOCK_ITEMS_COUNT;  /* 4 */
  const uint8_t menuFont     = FONT_8x8;
  const uint8_t lineH        = HEIGHT8x8;

  uint8_t lang = g_storedVar.language;
  int8_t sel   = 0;
  bool editingShortcut = false;
  uint8_t tmpShortcutIdx = (uint8_t)constrain(g_storedVar.lockShortcutIdx, 0, LOCK_SHORTCUT_COUNT);
  bool needRedraw = true;

  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
  resetUiEncoder(0);

  uint32_t lastInteraction = millis();
  bool screensaverActive   = false;
  uint16_t ssEncoderPos    = (uint16_t)readUiEncoder();

  while (true) {
    serviceTimedWiFiPortal();

    bool wakeUp = refreshIdleInteractionFromControls(&lastInteraction, &screensaverActive, &ssEncoderPos);
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
          ssEncoderPos = readUiEncoder();
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
      if (editingShortcut) {
        tmpShortcutIdx = (uint8_t)readUiEncoder();
      } else {
        sel = (int8_t)readUiEncoder();
      }
      needRedraw = true;
    }

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      if (editingShortcut) {
        g_storedVar.lockShortcutIdx = tmpShortcutIdx;
        saveEEPROM(g_storedVar);
        editingShortcut = false;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
        resetUiEncoder(ITEM_SHORT);
        sel = ITEM_SHORT;
      } else if (sel == ITEM_MENU) {
        g_storedVar.lockMenuEnabled = g_storedVar.lockMenuEnabled ? 0 : 1;
        saveEEPROM(g_storedVar);
      } else if (sel == ITEM_SHORT) {
        editingShortcut = true;
        tmpShortcutIdx = (uint8_t)constrain(g_storedVar.lockShortcutIdx, 0, LOCK_SHORTCUT_COUNT);
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, LOCK_SHORTCUT_COUNT, false);
        resetUiEncoder(tmpShortcutIdx);
      } else if (sel == ITEM_CONFIRM) {
        g_storedVar.lockConfirmEnabled = g_storedVar.lockConfirmEnabled ? 0 : 1;
        saveEEPROM(g_storedVar);
      } else if (sel == ITEM_BACK) {
        break;
      }
      needRedraw = true;
      delay(120);
    }

    /* Brake button = back (or cancel shortcut editing) */
    static BrakeButtonEdge brakeBtnInLock;
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      lastInteraction = millis();
    }
    if (brakeBtnInLock.pressed()) {
      if (editingShortcut) {
        editingShortcut = false;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
        resetUiEncoder(ITEM_SHORT);
        sel = ITEM_SHORT;
        needRedraw = true;
      } else {
        waitForBrakeButtonRelease();
        break;
      }
    }

    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }

    if (needRedraw) {
      obdFill(&g_obd, OBD_WHITE, 1);
      needRedraw = false;
      lang = g_storedVar.language;

      /* Row 0: MENU ITEM — on/off */
      bool s0 = (!editingShortcut && sel == ITEM_MENU);
      obdWriteString(&g_obd, 0, 0, 0 * lineH, (char*)getLockMenuName(lang, ITEM_MENU),
                     menuFont, s0 ? OBD_WHITE : OBD_BLACK, 1);
      sprintf(msgStr, "%3s", getOnOffLabel(lang, g_storedVar.lockMenuEnabled ? 1 : 0));
      uint8_t vx = OLED_WIDTH - (uint8_t)(strlen(msgStr) * WIDTH8x8);
      obdWriteString(&g_obd, 0, vx, 0 * lineH, msgStr,
                     menuFont, s0 ? OBD_WHITE : OBD_BLACK, 1);

      /* Row 1: SHORTCUT — OFF / 1s–10s (index = seconds) */
      bool s1 = (!editingShortcut && sel == ITEM_SHORT);
      obdWriteString(&g_obd, 0, 0, 1 * lineH, (char*)getLockMenuName(lang, ITEM_SHORT),
                     menuFont, (s1 || editingShortcut) ? OBD_WHITE : OBD_BLACK, 1);
      uint8_t shownIdx = editingShortcut ? tmpShortcutIdx : (uint8_t)g_storedVar.lockShortcutIdx;
      sprintf(msgStr, "%4s", getLockShortcutLabel(lang, shownIdx));
      uint8_t sx = OLED_WIDTH - (uint8_t)(strlen(msgStr) * WIDTH8x8);
      obdWriteString(&g_obd, 0, sx, 1 * lineH, msgStr,
                     menuFont, (s1 || editingShortcut) ? OBD_WHITE : OBD_BLACK, 1);

      /* Row 2: CONFIRM — on/off */
      bool s2 = (!editingShortcut && sel == ITEM_CONFIRM);
      obdWriteString(&g_obd, 0, 0, 2 * lineH, (char*)getLockMenuName(lang, ITEM_CONFIRM),
                     menuFont, s2 ? OBD_WHITE : OBD_BLACK, 1);
      sprintf(msgStr, "%3s", getOnOffLabel(lang, g_storedVar.lockConfirmEnabled ? 1 : 0));
      uint8_t cx = OLED_WIDTH - (uint8_t)(strlen(msgStr) * WIDTH8x8);
      obdWriteString(&g_obd, 0, cx, 2 * lineH, msgStr,
                     menuFont, s2 ? OBD_WHITE : OBD_BLACK, 1);

      /* Row 3: BACK */
      bool s3 = (!editingShortcut && sel == ITEM_BACK);
      obdWriteString(&g_obd, 0, 0, 3 * lineH, (char*)getBackLabel(lang),
                     menuFont, s3 ? OBD_WHITE : OBD_BLACK, 1);
    }

    vTaskDelay(10);
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}
