#include "settings_ext_pot_menu.h"
#include <Arduino.h>
#include "slot_ESC.h"
#include "ui_text_access.h"
#include "connectivity_portal.h"
#include "ext_pot.h"

extern StoredVar_type g_storedVar;
extern ESC_type g_escVar;
extern OBDISP g_obd;
extern AiEsp32RotaryEncoder g_rotaryEncoder;
extern char msgStr[50];

extern bool consumeScreensaverWakeInput(bool wakeTriggered);
extern bool refreshIdleInteractionFromControls(uint32_t* lastInteraction, bool* screensaverActive, uint16_t* lastEncoderPos);
extern bool serviceIdlePowerTransitions(uint32_t* lastInteraction, bool* screensaverActive);
extern bool checkRaceModeEscape();
extern void requestEscapeToMain();
extern void showScreensaver();
extern void saveEEPROM(StoredVar_type toSave);

static const char* getExtPotTargetLabel(uint8_t lang, uint16_t target) {
  switch (target) {
    case EXT_POT_TARGET_BRAKE:
      return getRaceLabel(lang, 0);
    case EXT_POT_TARGET_SENSI:
      return getRaceLabel(lang, 1);
    default:
      return getOnOffLabel(lang, 0);
  }
}

static const char* EXT_POT_ITEM_LABELS[9][2] = {
  {"POT.METER 1", "POT.METER 2"},
  {"POT.METER 1", "POT.METER 2"},
  {"POT.METER 1", "POT.METER 2"},
  {"POT.METER 1", "POT.METER 2"},
  {"POT.METER 1", "POT.METER 2"},
  {"POT.METER 1", "POT.METER 2"},
  {"POT.METER 1", "POT.METER 2"},
  {"POT.METER 1", "POT.METER 2"},
  {"POT.METER 1", "POT.METER 2"}
};

static const char* EXT_POT_ITEM_LABELS_PASCAL[9][2] = {
  {"Pot.meter 1", "Pot.meter 2"},
  {"Pot.meter 1", "Pot.meter 2"},
  {"Pot.meter 1", "Pot.meter 2"},
  {"Pot.meter 1", "Pot.meter 2"},
  {"Pot.meter 1", "Pot.meter 2"},
  {"Pot.meter 1", "Pot.meter 2"},
  {"Pot.meter 1", "Pot.meter 2"},
  {"Pot.meter 1", "Pot.meter 2"},
  {"Pot.meter 1", "Pot.meter 2"}
};

static const char* getExtPotItemLabel(uint8_t lang, uint8_t item) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL)
    ? EXT_POT_ITEM_LABELS_PASCAL[lang][item]
    : EXT_POT_ITEM_LABELS[lang][item];
}

static void clearValueField(uint8_t x, uint8_t y, uint8_t widthPx, uint8_t heightPx) {
  if (widthPx == 0 || heightPx == 0) return;

  uint8_t xEnd = x + widthPx - 1;
  for (uint8_t row = 0; row < heightPx; row++) {
    obdDrawLine(&g_obd, x, y + row, xEnd, y + row, OBD_WHITE, 1);
  }
}

static void drawRightAlignedValue(uint8_t y, const char* value, bool selected, uint8_t fieldChars) {
  if (value == nullptr || value[0] == '\0') return;

  uint8_t fieldWidthPx = fieldChars * WIDTH8x8;
  uint8_t fieldX = OLED_WIDTH - fieldWidthPx;
  clearValueField(fieldX, y, fieldWidthPx, HEIGHT8x8);

  uint8_t valueWidthPx = (uint8_t)(strlen(value) * WIDTH8x8);
  uint8_t valueX = OLED_WIDTH - valueWidthPx;
  obdWriteString(&g_obd, 0, valueX, y, (char*)value,
                 FONT_8x8, selected ? OBD_WHITE : OBD_BLACK, 1);
}

void showExtPotSettings() {
  const uint8_t NUM_ITEMS = 3;  /* POT 1, POT 2, BACK */
  const uint8_t ITEM_POT1 = 0;
  const uint8_t ITEM_POT2 = 1;
  const uint8_t ITEM_BACK = 2;
  const uint8_t menuFont = FONT_8x8;
  const uint8_t lineH = HEIGHT8x8;

  obdFill(&g_obd, OBD_WHITE, 1);

  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
  resetUiEncoder(0);

  uint8_t sel = 0;
  bool needRedraw = true;
  MenuState_enum epMenuState = ITEM_SELECTION;
  uint8_t editingPotIdx = 0;
  uint16_t tempPotTarget = 0;

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
      if (epMenuState == ITEM_SELECTION) {
        sel = (uint8_t)readUiEncoder();
      } else {
        tempPotTarget = (uint16_t)readUiEncoder();
      }
      needRedraw = true;
    }

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      if (epMenuState == VALUE_SELECTION) {
        /* Confirm */
        setExtPotTarget(editingPotIdx, tempPotTarget);
        saveEEPROM(g_storedVar);
        epMenuState = ITEM_SELECTION;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
        resetUiEncoder(sel);
        needRedraw = true;
      } else {
        if (sel == ITEM_BACK) {
          break;
        } else if (sel == ITEM_POT1 || sel == ITEM_POT2) {
          editingPotIdx = (sel == ITEM_POT1) ? 0 : 1;
          tempPotTarget = getExtPotTarget(editingPotIdx);
          epMenuState = VALUE_SELECTION;
          g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
          setUiEncoderBoundaries(EXT_POT_TARGET_MIN, EXT_POT_TARGET_MAX, false);
          resetUiEncoder(tempPotTarget);
          needRedraw = true;
        }
      }
      delay(200);
    }

    static bool brakeBtnInMenu = false;
    static uint32_t lastBrakeBtnTime = 0;
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      if (!brakeBtnInMenu && millis() - lastBrakeBtnTime > BUTTON_SHORT_PRESS_DEBOUNCE_MS) {
        brakeBtnInMenu = true;
        lastBrakeBtnTime = millis();
        if (epMenuState == VALUE_SELECTION) {
          /* Cancel */
          epMenuState = ITEM_SELECTION;
          g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
          setUiEncoderBoundaries(0, NUM_ITEMS - 1, false);
          resetUiEncoder(sel);
          obdFill(&g_obd, OBD_WHITE, 1);
          needRedraw = true;
        } else {
          while (digitalRead(BUTT_PIN) == BUTTON_PRESSED) { vTaskDelay(5); }
          break;
        }
      }
    } else {
      brakeBtnInMenu = false;
    }

    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }

    if (needRedraw) {
      needRedraw = false;
      uint8_t lang = g_storedVar.language;
      for (uint8_t i = 0; i < NUM_ITEMS; i++) {
        bool isNameSel  = (sel == i && epMenuState == ITEM_SELECTION);
        bool isValueSel = (sel == i && epMenuState == VALUE_SELECTION);
        const char* label = (i == ITEM_BACK) ? getBackLabel(lang) : getExtPotItemLabel(lang, i);
        obdWriteString(&g_obd, 0, 0, i * lineH, (char*)label,
                       menuFont, isNameSel ? OBD_WHITE : OBD_BLACK, 1);

        if (i == ITEM_POT1 || i == ITEM_POT2) {
          uint8_t potIdx = (i == ITEM_POT1) ? 0 : 1;
          uint16_t displayTarget = (isValueSel) ? tempPotTarget : getExtPotTarget(potIdx);
          drawRightAlignedValue(i * lineH, getExtPotTargetLabel(lang, displayTarget),
                                isNameSel || isValueSel, 5);
        }
      }
    }

    vTaskDelay(10);
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}
