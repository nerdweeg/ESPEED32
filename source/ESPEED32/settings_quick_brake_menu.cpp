#include "settings_quick_brake_menu.h"
#include <Arduino.h>
#include "slot_ESC.h"
#include "ui_text_access.h"
#include "app_globals.h"
#include "power_ui.h"
#include "ui_render.h"
#include "menu_input.h"

enum AdvancedBrakeRowType : uint8_t {
  AB_ROW_BUTTON = 0,
  AB_ROW_MODE = 1,
  AB_ROW_ZONE = 2,
  AB_ROW_LEVEL = 3,
  AB_ROW_BACK = 4
};

static uint8_t getAdvancedBrakeMenuItemCount(uint16_t mode) {
  if (mode == RELEASE_BRAKE_OFF) {
    return 3;
  }
  if (mode == RELEASE_BRAKE_DRAG) {
    return 4;
  }
  return 5;
}

static AdvancedBrakeRowType getAdvancedBrakeRowType(uint16_t mode, uint8_t visibleIndex) {
  if (visibleIndex == 1) {
    return AB_ROW_BUTTON;
  }
  if (visibleIndex == 2) {
    return AB_ROW_MODE;
  }
  if (mode == RELEASE_BRAKE_OFF) {
    return AB_ROW_BACK;
  }
  if (mode == RELEASE_BRAKE_DRAG) {
    return (visibleIndex == 3) ? AB_ROW_LEVEL : AB_ROW_BACK;
  }
  if (visibleIndex == 3) {
    return AB_ROW_ZONE;
  }
  if (visibleIndex == 4) {
    return AB_ROW_LEVEL;
  }
  return AB_ROW_BACK;
}

static const char* getAdvancedBrakeRowLabel(uint8_t lang, uint16_t mode, AdvancedBrakeRowType rowType) {
  static const char* BUTTON_LABELS[LANG_COUNT] = {"Alt.brems", "Alt.Brake", "Alt.Brake", "Alt.Brake", "Alt.Freno", "Alt.Brems", "Alt.Freno", "Alt.Rem", "Alt.Trav", "Alt.broms"};
  static const char* MODE_LABELS[LANG_COUNT]   = {"Rel.brems", "Rel.Brake", "Rel.Brake", "Rel.Brake", "Rel.Freno", "Rel.Brems", "Rel.Freno", "Rel.Rem", "Rel.Trav", "Rel.broms"};
  static const char* ZONE_LABELS[LANG_COUNT]   = {"Sone", "Zone", "Zone", "Zone", "Zona", "Zone", "Zona", "Zone", "Zona", "Zon"};
  static const char* QUICK_LABELS[LANG_COUNT]  = {"Quick", "Quick", "Quick", "Quick", "Quick", "Quick", "Quick", "Quick", "Quick", "Quick"};
  static const char* DRAG_LABELS[LANG_COUNT]   = {"Drag", "Drag", "Drag", "Drag", "Drag", "Drag", "Drag", "Drag", "Drag", "Drag"};

  switch (rowType) {
    case AB_ROW_BUTTON:
      return BUTTON_LABELS[lang];
    case AB_ROW_MODE:
      return MODE_LABELS[lang];
    case AB_ROW_ZONE:
      return ZONE_LABELS[lang];
    case AB_ROW_LEVEL:
      return (mode == RELEASE_BRAKE_DRAG) ? DRAG_LABELS[lang] : QUICK_LABELS[lang];
    case AB_ROW_BACK:
    default:
      return getBackLabel(lang);
  }
}

static const char* getReleaseBrakeMenuModeValue(uint8_t lang, uint16_t mode) {
  static const char* MODE_VALUES[LANG_COUNT][3] = {
    {"AV", "QUICK", "DRAG"},
    {"OFF", "QUICK", "DRAG"},
    {"OFF", "QUICK", "DRAG"},
    {"OFF", "QUICK", "DRAG"},
    {"OFF", "QUICK", "DRAG"},
    {"AUS", "QUICK", "DRAG"},
    {"OFF", "QUICK", "DRAG"},
    {"UIT", "QUICK", "DRAG"},
    {"OFF", "QUICK", "DRAG"},
    {"AV", "QUICK", "DRAG"}
  };

  if (mode > RELEASE_BRAKE_DRAG) {
    mode = RELEASE_BRAKE_OFF;
  }
  return MODE_VALUES[lang][mode];
}

/**
 * Advanced Brake submenu.
 * Opened by clicking BRAKE+ in the main menu.
 * Items: Alt.Brake, Rel.Brake mode, optional QUICK zone + level, DRAG level, BACK.
 * QUICK cuts output to brake inside the configured zone. DRAG keeps output active
 * and blends in drag while the trigger is moving toward release, with no zone.
 */
void showAdvancedBrakeMenu() {
  uint8_t lang = g_storedVar.language;

  obdFill(&g_obd, OBD_WHITE, 1);
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(1, getAdvancedBrakeMenuItemCount(g_storedVar.carParam[g_carSel].quickBrakeEnabled), false);
  resetUiEncoder(1);

  uint8_t sel          = 1;
  uint8_t prevSel      = 0xFF;
  MenuState_enum state = ITEM_SELECTION;
  uint16_t origValue   = 0;
  AdvancedBrakeRowType editRowType = AB_ROW_BUTTON;
  bool forceRedraw     = true;

  uint32_t lastInteraction   = millis();
  bool screensaverActive     = false;
  uint16_t screensaverEncPos = (uint16_t)readUiEncoder();

  static bool brakeInQB = false;
  static uint32_t lastBrakeQB = 0;

  auto currentMode = [&]() -> uint16_t {
    return g_storedVar.carParam[g_carSel].quickBrakeEnabled;
  };

  auto refreshSelectionBounds = [&]() {
    uint8_t visibleItems = getAdvancedBrakeMenuItemCount(currentMode());
    if (sel > visibleItems) {
      sel = visibleItems;
    }
    setUiEncoderBoundaries(1, visibleItems, false);
    resetUiEncoder(sel);
  };

  while (true) {
    bool wakeUp = refreshIdleInteractionFromControls(&lastInteraction, &screensaverActive, &screensaverEncPos);
    if (wakeUp) {
      obdFill(&g_obd, OBD_WHITE, 1);
      prevSel     = 0xFF;
      forceRedraw = true;
    }
    if (consumeScreensaverWakeInput(wakeUp)) { continue; }

    if (!wakeUp && g_storedVar.screensaverTimeout > 0 &&
        millis() - lastInteraction > (g_storedVar.screensaverTimeout * 1000UL)) {
      if (g_escVar.trigger_norm == 0) {
        if (!screensaverActive) {
          screensaverActive = true;
          screensaverEncPos = readUiEncoder();
          showScreensaver();
        }
        if (serviceIdlePowerTransitions(&lastInteraction, &screensaverActive)) {
          obdFill(&g_obd, OBD_WHITE, 1);
          prevSel     = 0xFF;
          forceRedraw = true;
        }
        delay(10);
        continue;
      }
    }

    /* Brake button: cancel edit or exit submenu */
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      if (!brakeInQB && millis() - lastBrakeQB > BUTTON_SHORT_PRESS_DEBOUNCE_MS) {
        brakeInQB = true;
        lastBrakeQB = millis();
        if (state == VALUE_SELECTION) {
          /* Cancel: restore original value */
          if (editRowType == AB_ROW_BUTTON) g_storedVar.carParam[g_carSel].brakeButtonReduction = origValue;
          else if (editRowType == AB_ROW_MODE) g_storedVar.carParam[g_carSel].quickBrakeEnabled = origValue;
          else if (editRowType == AB_ROW_ZONE) g_storedVar.carParam[g_carSel].quickBrakeThreshold = origValue;
          else if (editRowType == AB_ROW_LEVEL) g_storedVar.carParam[g_carSel].quickBrakeStrength = origValue;
          state = ITEM_SELECTION;
          g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
          refreshSelectionBounds();
          obdFill(&g_obd, OBD_WHITE, 1);
          prevSel     = 0xFF;
          forceRedraw = true;
        } else {
          waitForBrakeButtonRelease();
          break;
        }
      }
    } else {
      brakeInQB = false;
    }

    /* Encoder scroll */
    if (g_rotaryEncoder.encoderChanged()) {
      lastInteraction = millis();
      uint16_t ep = (uint16_t)readUiEncoder();
      if (state == ITEM_SELECTION) {
        sel = (uint8_t)ep;
        forceRedraw = true;
      } else {
        /* Live update while editing */
        if (editRowType == AB_ROW_BUTTON) g_storedVar.carParam[g_carSel].brakeButtonReduction = ep;
        else if (editRowType == AB_ROW_MODE) g_storedVar.carParam[g_carSel].quickBrakeEnabled = ep;
        else if (editRowType == AB_ROW_ZONE) g_storedVar.carParam[g_carSel].quickBrakeThreshold = ep;
        else if (editRowType == AB_ROW_LEVEL) g_storedVar.carParam[g_carSel].quickBrakeStrength = ep;
        forceRedraw = true;
      }
    }

    /* Encoder click */
    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      if (state == ITEM_SELECTION) {
        AdvancedBrakeRowType rowType = getAdvancedBrakeRowType(currentMode(), sel);
        if (rowType == AB_ROW_BACK) {
          /* BACK */
          waitForBrakeButtonRelease();
          break;
        }
        /* Enter value edit */
        uint16_t minV = 0, maxV = 1, curV = 0;
        if (rowType == AB_ROW_BUTTON) { curV = g_storedVar.carParam[g_carSel].brakeButtonReduction; minV = 0; maxV = 100; }
        else if (rowType == AB_ROW_MODE) { curV = g_storedVar.carParam[g_carSel].quickBrakeEnabled; minV = RELEASE_BRAKE_OFF; maxV = RELEASE_BRAKE_DRAG; }
        else if (rowType == AB_ROW_ZONE) { curV = g_storedVar.carParam[g_carSel].quickBrakeThreshold; minV = 0; maxV = QUICK_BRAKE_THRESHOLD_MAX; }
        else if (rowType == AB_ROW_LEVEL) { curV = g_storedVar.carParam[g_carSel].quickBrakeStrength; minV = 0; maxV = QUICK_BRAKE_STRENGTH_MAX; }
        origValue = curV;
        editRowType = rowType;
        g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
        setUiEncoderBoundaries(minV, maxV, false);
        resetUiEncoder(curV);
        state       = VALUE_SELECTION;
        obdFill(&g_obd, OBD_WHITE, 1);
        prevSel     = 0xFF;
        forceRedraw = true;
      } else {
        /* Confirm value */
        saveEEPROM(g_storedVar);
        state = ITEM_SELECTION;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        refreshSelectionBounds();
        obdFill(&g_obd, OBD_WHITE, 1);
        prevSel     = 0xFF;
        forceRedraw = true;
      }
    }

    /* Draw */
    if (forceRedraw || sel != prevSel) {
      forceRedraw = false;
      prevSel = sel;
      uint16_t mode = currentMode();
      uint8_t visibleItems = getAdvancedBrakeMenuItemCount(mode);
      obdFill(&g_obd, OBD_WHITE, 1);

      for (uint8_t idx = 0; idx < visibleItems; idx++) {
        uint8_t yPx     = idx * HEIGHT8x8;
        bool isSelected = (sel == idx + 1);
        bool inEdit     = (state == VALUE_SELECTION && isSelected);
        AdvancedBrakeRowType rowType = getAdvancedBrakeRowType(mode, idx + 1);
        const char* rowName = getAdvancedBrakeRowLabel(lang, mode, rowType);

        obdWriteString(&g_obd, 0, 0, yPx, (char *)rowName, FONT_8x8,
                       (isSelected && state == ITEM_SELECTION) ? OBD_WHITE : OBD_BLACK, 1);

        /* Value right-justified */
        char vbuf[12];
        if (rowType == AB_ROW_BUTTON) {
          snprintf(vbuf, sizeof(vbuf), "%3d%%", g_storedVar.carParam[g_carSel].brakeButtonReduction);
        } else if (rowType == AB_ROW_MODE) {
          snprintf(vbuf, sizeof(vbuf), "%s", getReleaseBrakeMenuModeValue(lang, mode));
        } else if (rowType == AB_ROW_ZONE) {
          snprintf(vbuf, sizeof(vbuf), "%3d%%", g_storedVar.carParam[g_carSel].quickBrakeThreshold);
        } else if (rowType == AB_ROW_LEVEL) {
          snprintf(vbuf, sizeof(vbuf), "%3d%%", g_storedVar.carParam[g_carSel].quickBrakeStrength);
        } else {
          vbuf[0] = '\0';
        }
        if (vbuf[0]) {
          int tw = strlen(vbuf) * WIDTH8x8;
          obdWriteString(&g_obd, 0, OLED_WIDTH - tw, yPx, vbuf, FONT_8x8,
                         inEdit ? OBD_WHITE : OBD_BLACK, 1);
        }
      }
    }

    /* Long press = escape to main for race mode toggle */
    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }

    vTaskDelay(10);
  }

  saveEEPROM(g_storedVar);
  obdFill(&g_obd, OBD_WHITE, 1);
  resetEncoderForMainMenu();
}

void showQuickBrakeMenu() {
  showAdvancedBrakeMenu();
}
