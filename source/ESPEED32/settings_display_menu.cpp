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
extern uint16_t g_advancedMenuEnabled;
extern char msgStr[50];

extern bool consumeScreensaverWakeInput(bool wakeTriggered);
extern bool refreshIdleInteractionFromControls(uint32_t* lastInteraction, bool* screensaverActive, uint16_t* lastEncoderPos);
extern bool serviceIdlePowerTransitions(uint32_t* lastInteraction, bool* screensaverActive);
extern bool checkRaceModeEscape();
extern void requestEscapeToMain();
extern bool isEscapeToMainRequested();

extern void showScreensaver();
extern void saveEEPROM(StoredVar_type toSave);
extern void applyAdvancedMenuSetting(uint16_t enabled);
extern void initMenuItems();
extern void initSettingsMenuItems();
extern void initDisplayMenuItems();

/**
 * Display settings submenu: VIEW, LANG, CASE, FSIZE, ADVANCED, STEPS, STATUS, BACK.
 * 2-choice params (CASE, FSIZE, ADVANCED) toggle instantly on click.
 * Multi-choice params (VIEW=3, LANG=9) use press-enter / scroll / confirm.
 * STEPS and STATUS open dedicated submenus.
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

  uint8_t visibleLines = (DISPLAY_ITEMS_COUNT > 5) ? 5 : DISPLAY_ITEMS_COUNT;
  uint16_t frameUpper = 1;
  uint16_t frameLower = visibleLines;

  uint32_t lastInteraction = millis();
  bool ssActive = false;
  uint16_t ssEncoderPos = (uint16_t)readUiEncoder();

  /* Inline reinit helper used by instant-toggle items */
  auto reinitDisplayMenu = [&]() {
    initMenuItems();
    initSettingsMenuItems();
    initDisplayMenuItems();  /* Must be last: overwrites shared g_settingsMenu */
    if (sel < frameUpper) {
      frameUpper = sel;
      frameLower = frameUpper + visibleLines - 1;
    } else if (sel > frameLower) {
      frameLower = sel;
      frameUpper = frameLower - visibleLines + 1;
    }
    if (frameLower > DISPLAY_ITEMS_COUNT) {
      frameLower = DISPLAY_ITEMS_COUNT;
      frameUpper = frameLower - visibleLines + 1;
    }
    obdFill(&g_obd, OBD_WHITE, 1);
    prevSel = 0;
  };

  while (true) {
    bool wakeUp = refreshIdleInteractionFromControls(&lastInteraction, &ssActive, &ssEncoderPos);
    if (wakeUp) {
      obdFill(&g_obd, OBD_WHITE, 1);
    }

    if (consumeScreensaverWakeInput(wakeUp)) { continue; }

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
        if (sel == DISPLAY_ITEM_BACK_IDX + 1) { break; }

        /* Submenus */
        if (sel == DISPLAY_ITEM_STEPS_IDX + 1) {
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
        if (sel == DISPLAY_ITEM_STATUS_IDX + 1) {
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

        /* 2-choice instant toggles */
        if (sel == DISPLAY_ITEM_ADVANCED_IDX + 1) {
          applyAdvancedMenuSetting(g_advancedMenuEnabled ? 0 : 1);
          saveEEPROM(g_storedVar);
          reinitDisplayMenu();
          continue;
        }
        if (sel == DISPLAY_ITEM_CASE_IDX + 1) {
          g_storedVar.textCase = (g_storedVar.textCase == TEXT_CASE_UPPER) ? TEXT_CASE_PASCAL : TEXT_CASE_UPPER;
          saveEEPROM(g_storedVar);
          reinitDisplayMenu();
          continue;
        }
        if (sel == DISPLAY_ITEM_FSIZE_IDX + 1) {
          g_storedVar.listFontSize = (g_storedVar.listFontSize == FONT_SIZE_LARGE) ? FONT_SIZE_SMALL : FONT_SIZE_LARGE;
          saveEEPROM(g_storedVar);
          reinitDisplayMenu();
          continue;
        }

        /* Multi-choice VALUE_SELECTION: VIEW (3 options), LANG (9 options) */
        if (g_settingsMenu.item[sel - 1].value != ITEM_NO_VALUE) {
          if (sel == DISPLAY_ITEM_LANG_IDX + 1) {
            isEditingLanguage = true;
            tempLanguage = g_storedVar.language;
            originalValue = g_storedVar.language;
          } else {
            valuePtr = (uint16_t *)g_settingsMenu.item[sel - 1].value;
            originalValue = *valuePtr;
          }
          menuState = VALUE_SELECTION;
          g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
          if (!isEditingLanguage) {
            valuePtr = (uint16_t *)g_settingsMenu.item[sel - 1].value;
          }
          setUiEncoderBoundaries(g_settingsMenu.item[sel - 1].minValue,
                                        g_settingsMenu.item[sel - 1].maxValue, false);
          resetUiEncoder(isEditingLanguage ? tempLanguage : *valuePtr);
        }
      } else {
        /* Confirm edit */
        menuState = ITEM_SELECTION;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(1, DISPLAY_ITEMS_COUNT, false);
        resetUiEncoder(sel);
        if (isEditingLanguage) { g_storedVar.language = tempLanguage; lang = tempLanguage; isEditingLanguage = false; }
        saveEEPROM(g_storedVar);
        if (g_storedVar.language != prevLanguage) {
          prevLanguage = g_storedVar.language;
          reinitDisplayMenu();
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

        if (itemIdx == DISPLAY_ITEM_VIEW_IDX) {
          sprintf(msgStr, "%6s", getViewModeLabel(g_storedVar.language, value));
        } else if (itemIdx == DISPLAY_ITEM_LANG_IDX) {
          uint16_t dispLang = (isEditingLanguage && isValueSel) ? tempLanguage : value;
          sprintf(msgStr, "%3s", LANG_LABELS[dispLang]);
        } else if (itemIdx == DISPLAY_ITEM_CASE_IDX) {
          sprintf(msgStr, "%6s", TEXT_CASE_LABELS[g_storedVar.language][value]);
        } else if (itemIdx == DISPLAY_ITEM_FSIZE_IDX) {
          sprintf(msgStr, "%5s", FONT_SIZE_LABELS[g_storedVar.language][value]);
        } else if (itemIdx == DISPLAY_ITEM_ADVANCED_IDX) {
          snprintf(msgStr, sizeof(msgStr), "%3s", getOnOffLabel(g_storedVar.language, g_advancedMenuEnabled ? 1 : 0));
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

    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }

    vTaskDelay(10);
  }
}
