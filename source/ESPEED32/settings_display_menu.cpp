#include "settings_display_menu.h"
#include <Arduino.h>
#include "slot_ESC.h"
#include "ui_strings.h"
#include "ui_text_access.h"
#include "settings_display_submenus.h"

extern StoredVar_type g_storedVar;
extern ESC_type g_escVar;
extern OBDISP g_obd;
extern AiEsp32RotaryEncoder g_rotaryEncoder;
extern Menu_type g_settingsMenu;
extern char msgStr[50];

extern bool consumeScreensaverWakeInput(bool wakeTriggered);
extern bool refreshIdleInteractionFromControls(uint32_t* lastInteraction, bool* screensaverActive, uint16_t* lastEncoderPos);
extern bool serviceIdlePowerTransitions(uint32_t* lastInteraction, bool* screensaverActive);
extern bool checkRaceModeEscape();
extern void requestEscapeToMain();
extern bool isEscapeToMainRequested();

extern void showScreensaver();
extern void saveEEPROM(StoredVar_type toSave);
extern void initMenuItems();
extern void initSettingsMenuItems();
extern void initDisplayMenuItems();

/**
 * Display settings submenu: VIEW, LANG, CASE, FSIZE, STEPS, STATUS, BACK.
 * Handles value editing for display-related parameters.
 * STEPS opens a dedicated submenu for ANTISPIN, BRAKE STEP and SENSI STEP.
 */
void showDisplaySettings() {
  initDisplayMenuItems();
  obdFill(&g_obd, OBD_WHITE, 1);

  uint16_t lang = g_storedVar.language;
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(1, DISPLAY_ITEMS_COUNT, false);
  resetUiEncoder(1);

  uint16_t sel = 1;
  uint16_t prevSel = 0;
  MenuState_enum menuState = ITEM_SELECTION;
  uint16_t *valuePtr = NULL;
  uint16_t originalValue = 0;

  uint16_t prevLanguage = g_storedVar.language;
  uint16_t tempLanguage = g_storedVar.language;
  bool isEditingLanguage = false;
  uint16_t prevTextCase = g_storedVar.textCase;
  uint16_t tempTextCase = g_storedVar.textCase;
  bool isEditingTextCase = false;
  uint16_t prevFontSize = g_storedVar.listFontSize;
  uint16_t tempFontSize = g_storedVar.listFontSize;
  bool isEditingFontSize = false;

  uint8_t visibleLines = (DISPLAY_ITEMS_COUNT > 5) ? 5 : DISPLAY_ITEMS_COUNT;  /* Compact scrollable list. */
  uint16_t frameUpper = 1;
  uint16_t frameLower = visibleLines;

  uint32_t lastInteraction = millis();
  bool ssActive = false;
  uint16_t ssEncoderPos = (uint16_t)readUiEncoder();

  while (true) {
    bool wakeUp = refreshIdleInteractionFromControls(&lastInteraction, &ssActive, &ssEncoderPos);
    if (wakeUp) {
      obdFill(&g_obd, OBD_WHITE, 1);
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
        }
        delay(10);
        continue;
      }
    }

    if (!wakeUp && ssActive) {
      if (serviceIdlePowerTransitions(&lastInteraction, &ssActive)) {
        obdFill(&g_obd, OBD_WHITE, 1);
      }
      delay(10);
      continue;
    }

    /* Encoder button click */
    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      if (menuState == ITEM_SELECTION) {
        if (sel == DISPLAY_ITEMS_COUNT) {  /* BACK */
          break;
        }
        /* STEPS submenu (ANTISPIN, BRAKE STEP, SENSI STEP) */
        if (sel == DISPLAY_ITEMS_COUNT - 2) {
          showStepsSettings();
          if (isEscapeToMainRequested()) break;
          initDisplayMenuItems();
          g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
          setUiEncoderBoundaries(1, DISPLAY_ITEMS_COUNT, false);
          resetUiEncoder(sel);
          obdFill(&g_obd, OBD_WHITE, 1);
          prevSel = 0;
          continue;
        }
        /* STATUS submenu lives right before BACK. */
        if (sel == DISPLAY_ITEMS_COUNT - 1) {
          showStatusSettings();
          if (isEscapeToMainRequested()) break;
          initDisplayMenuItems();
          g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
          setUiEncoderBoundaries(1, DISPLAY_ITEMS_COUNT, false);
          resetUiEncoder(sel);
          obdFill(&g_obd, OBD_WHITE, 1);
          prevSel = 0;
          continue;
        }
        /* Value items */
        if (g_settingsMenu.item[sel - 1].value != ITEM_NO_VALUE) {
          if (sel == 2) {  /* LANG */
            isEditingLanguage = true;
            tempLanguage = g_storedVar.language;
            originalValue = g_storedVar.language;
          } else if (sel == 3) {  /* CASE */
            isEditingTextCase = true;
            tempTextCase = g_storedVar.textCase;
            originalValue = g_storedVar.textCase;
          } else if (sel == 4) {  /* FSIZE */
            isEditingFontSize = true;
            tempFontSize = g_storedVar.listFontSize;
            originalValue = g_storedVar.listFontSize;
          } else {
            valuePtr = (uint16_t *)g_settingsMenu.item[sel - 1].value;
            originalValue = *valuePtr;
          }
          menuState = VALUE_SELECTION;
          g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
          if (!isEditingLanguage && !isEditingTextCase && !isEditingFontSize) {
            valuePtr = (uint16_t *)g_settingsMenu.item[sel - 1].value;
          }
          setUiEncoderBoundaries(g_settingsMenu.item[sel - 1].minValue,
                                        g_settingsMenu.item[sel - 1].maxValue, false);
          uint16_t resetVal = isEditingLanguage ? tempLanguage :
                              (isEditingTextCase ? tempTextCase :
                              (isEditingFontSize ? tempFontSize : *valuePtr));
          resetUiEncoder(resetVal);
        }
      } else {
        /* Confirm edit */
        menuState = ITEM_SELECTION;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(1, DISPLAY_ITEMS_COUNT, false);
        resetUiEncoder(sel);
        if (isEditingLanguage) { g_storedVar.language = tempLanguage; lang = tempLanguage; isEditingLanguage = false; }
        if (isEditingTextCase) { g_storedVar.textCase = tempTextCase; isEditingTextCase = false; }
        if (isEditingFontSize) { g_storedVar.listFontSize = tempFontSize; isEditingFontSize = false; }
        saveEEPROM(g_storedVar);
        if (g_storedVar.language != prevLanguage || g_storedVar.textCase != prevTextCase || g_storedVar.listFontSize != prevFontSize) {
          initMenuItems();
          initSettingsMenuItems();
          initDisplayMenuItems();  /* Must be last: shared g_settingsMenu is overwritten by initSettingsMenuItems */
          prevLanguage = g_storedVar.language;
          prevTextCase = g_storedVar.textCase;
          prevFontSize = g_storedVar.listFontSize;
          /* visibleLines stays fixed - submenu always uses FONT_8x8 */
          frameUpper = 1;
          frameLower = visibleLines;
          obdFill(&g_obd, OBD_WHITE, 1);
          lang = g_storedVar.language;
        }
      }
      delay(200);
    }

    /* Encoder movement */
    if (g_rotaryEncoder.encoderChanged()) {
      lastInteraction = millis();
      if (menuState == ITEM_SELECTION) {
        sel = readUiEncoder();
      } else {
        if (isEditingLanguage) {
          tempLanguage = readUiEncoder();
        } else if (isEditingTextCase) {
          uint16_t newTC = readUiEncoder();
          if (newTC != tempTextCase) {
            tempTextCase = newTC;
            obdFill(&g_obd, OBD_WHITE, 1);
          }
        } else if (isEditingFontSize) {
          tempFontSize = readUiEncoder();
        } else {
          *valuePtr = readUiEncoder();
        }
      }
    }

    /* Brake button = cancel edit or back */
    static bool brakeBtnInDisplay = false;
    static uint32_t lastBrakeBtnDisplayTime = 0;
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      if (!brakeBtnInDisplay && millis() - lastBrakeBtnDisplayTime > BUTTON_SHORT_PRESS_DEBOUNCE_MS) {
        brakeBtnInDisplay = true;
        lastBrakeBtnDisplayTime = millis();
        lastInteraction = millis();
        if (menuState == VALUE_SELECTION) {
          if (isEditingLanguage) { tempLanguage = originalValue; g_storedVar.language = originalValue; isEditingLanguage = false; }
          else if (isEditingTextCase) { tempTextCase = originalValue; g_storedVar.textCase = originalValue; isEditingTextCase = false; }
          else if (isEditingFontSize) { tempFontSize = originalValue; g_storedVar.listFontSize = originalValue; isEditingFontSize = false; }
          else if (valuePtr != NULL) { *valuePtr = originalValue; }
          menuState = ITEM_SELECTION;
          g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
          setUiEncoderBoundaries(1, DISPLAY_ITEMS_COUNT, false);
          resetUiEncoder(sel);
          obdFill(&g_obd, OBD_WHITE, 1);
        } else {
          while (digitalRead(BUTT_PIN) == BUTTON_PRESSED) { vTaskDelay(5); }
          break;
        }
      }
    } else {
      brakeBtnInDisplay = false;
    }

    /* Frame scrolling */
    if (menuState == ITEM_SELECTION) {
      if (sel > frameLower) {
        frameLower = sel;
        frameUpper = frameLower - visibleLines + 1;
        obdFill(&g_obd, OBD_WHITE, 1);
      } else if (sel < frameUpper) {
        frameUpper = sel;
        frameLower = frameUpper + visibleLines - 1;
        obdFill(&g_obd, OBD_WHITE, 1);
      }
    }

    /* Display - submenu always uses FONT_8x8 regardless of global listFontSize */
    const uint8_t menuFont  = FONT_8x8;
    const uint8_t charWidth = WIDTH8x8;
    const uint8_t lineHeight = HEIGHT8x8;

    for (uint8_t i = 0; i < visibleLines; i++) {
      uint16_t itemIdx = frameUpper - 1 + i;
      if (itemIdx >= DISPLAY_ITEMS_COUNT) break;

      bool isSelected = (sel - frameUpper == i && menuState == ITEM_SELECTION);
      obdWriteString(&g_obd, 0, 0, i * lineHeight, (char*)getDisplayMenuName(lang, itemIdx),
                     menuFont, isSelected ? OBD_WHITE : OBD_BLACK, 1);

      if (g_settingsMenu.item[itemIdx].value != ITEM_NO_VALUE) {
        bool isValueSel = (sel - frameUpper == i && menuState == VALUE_SELECTION);
        uint16_t value = *(uint16_t *)(g_settingsMenu.item[itemIdx].value);

        if (itemIdx == 0) {  /* VIEW */
          sprintf(msgStr, "%6s", getViewModeLabel(g_storedVar.language, value));
        } else if (itemIdx == 1) {  /* LANG */
          uint16_t dispLang = (isEditingLanguage && isValueSel) ? tempLanguage : value;
          sprintf(msgStr, "%3s", LANG_LABELS[dispLang]);
        } else if (itemIdx == 2) {  /* CASE */
          uint16_t dispTC = (isEditingTextCase && isValueSel) ? tempTextCase : value;
          sprintf(msgStr, "%6s", TEXT_CASE_LABELS[g_storedVar.language][dispTC]);
        } else if (itemIdx == 3) {  /* FSIZE */
          uint16_t dispFS = (isEditingFontSize && isValueSel) ? tempFontSize : value;
          sprintf(msgStr, "%5s", FONT_SIZE_LABELS[g_storedVar.language][dispFS]);
        } else if (g_settingsMenu.item[itemIdx].unit[0] != '\0') {
          snprintf(msgStr, sizeof(msgStr), "%2d %s", value, g_settingsMenu.item[itemIdx].unit);
        } else {
          sprintf(msgStr, "%3d", value);
        }

        int textWidth = strlen(msgStr) * charWidth;
        obdWriteString(&g_obd, 0, OLED_WIDTH - textWidth, i * lineHeight, msgStr,
                       menuFont, isValueSel ? OBD_WHITE : OBD_BLACK, 1);
      }
    }

    /* Long press = escape to main for race mode toggle */
    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }

    vTaskDelay(10);
  }
}
