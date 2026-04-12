#include "settings_menu_root.h"
#include <Arduino.h>
#include "slot_ESC.h"
#include "ui_text_access.h"
#include "connectivity_portal.h"
#include "settings_power_menu.h"
#include "settings_display_menu.h"
#include "settings_hardware_menu.h"
#include "settings_sound_wifi_submenus.h"
#include "settings_lock_menu.h"
#include "settings_reset_menu.h"
#include "settings_about_screen.h"
#include "ui_render.h"

extern StoredVar_type g_storedVar;
extern uint16_t g_statsEnabled;
extern uint16_t g_antiSpinStepMs;
extern ESC_type g_escVar;
extern OBDISP g_obd;
extern AiEsp32RotaryEncoder g_rotaryEncoder;
extern Menu_type g_settingsMenu;
extern char msgStr[50];
extern uint32_t g_lastEncoderInteraction;

extern void initSettingsMenuItems();
extern void initMenuItems();
extern void saveEEPROM(StoredVar_type toSave);
extern bool consumeScreensaverWakeInput(bool wakeTriggered);
extern bool refreshIdleInteractionFromControls(uint32_t* lastInteraction, bool* screensaverActive, uint16_t* lastEncoderPos);
extern bool serviceIdlePowerTransitions(uint32_t* lastInteraction, bool* screensaverActive);
extern bool checkRaceModeEscape();
extern void setInSettingsMenu(bool active);
extern void requestEscapeToMain();
extern bool isEscapeToMainRequested();
extern uint8_t getMainMenuSelector();
extern uint8_t getMainMenuItemsCount();

void showSettingsMenu() {
  initSettingsMenuItems();
  obdFill(&g_obd, OBD_WHITE, 1);
  setInSettingsMenu(true);

  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(1, SETTINGS_ITEMS_COUNT, false);
  resetUiEncoder(1);

  uint16_t settingsSelector = 1;
  uint16_t prevSettingsSelector = 0;
  MenuState_enum settingsMenuState = ITEM_SELECTION;
  uint16_t *settingsValuePtr = NULL;
  uint16_t originalSettingsValue = 0;

  uint16_t frameUpper = 1;
  uint8_t visibleLines = getMenuLines();
  uint16_t frameLower = visibleLines;

  uint32_t lastSettingsInteraction = millis();
  bool settingsScreensaverActive = false;
  uint16_t settingsScreensaverEncoderPos = (uint16_t)readUiEncoder();

  auto resumeAfterSettingsChild = [&]() {
    lastSettingsInteraction = millis();
    g_lastEncoderInteraction = lastSettingsInteraction;
    settingsScreensaverActive = false;
    settingsScreensaverEncoderPos = readUiEncoder();
    initSettingsMenuItems();
    g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
    setUiEncoderBoundaries(1, SETTINGS_ITEMS_COUNT, false);
    resetUiEncoder(settingsSelector);
    obdFill(&g_obd, OBD_WHITE, 1);
    prevSettingsSelector = 0;
  };

  while (true) {
    bool wakeUpTriggered = refreshIdleInteractionFromControls(&lastSettingsInteraction, &settingsScreensaverActive, &settingsScreensaverEncoderPos);
    if (wakeUpTriggered) {
      obdFill(&g_obd, OBD_WHITE, 1);
    }

    /* Screensaver timeout */
    if (consumeScreensaverWakeInput(wakeUpTriggered)) { continue; }

    if (!wakeUpTriggered && g_storedVar.screensaverTimeout > 0 &&
        millis() - lastSettingsInteraction > (g_storedVar.screensaverTimeout * 1000UL)) {
      if (g_escVar.trigger_norm == 0) {
        if (!settingsScreensaverActive) {
          settingsScreensaverActive = true;
          settingsScreensaverEncoderPos = readUiEncoder();
          showScreensaver();
        }
        if (serviceIdlePowerTransitions(&lastSettingsInteraction, &settingsScreensaverActive)) {
          obdFill(&g_obd, OBD_WHITE, 1);
        }
        delay(10);
        continue;
      }
    }

    /* Encoder button click */
    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastSettingsInteraction = millis();
      if (settingsMenuState == ITEM_SELECTION) {
        /* BACK */
        if (settingsSelector == SETTINGS_ITEMS_COUNT) { break; }
        /* POWER submenu */
        if (settingsSelector == 1) {
          showPowerSettings();
          if (isEscapeToMainRequested()) break;
          resumeAfterSettingsChild();
          continue;
        }
        /* DISPLAY submenu */
        if (settingsSelector == 2) {
          showDisplaySettings();
          if (isEscapeToMainRequested()) break;
          resumeAfterSettingsChild();
          continue;
        }
        /* SOUND submenu */
        if (settingsSelector == 3) {
          showSoundSettings();
          if (isEscapeToMainRequested()) break;
          resumeAfterSettingsChild();
          continue;
        }
        /* HARDWARE submenu */
        if (settingsSelector == 4) {
          showHardwareSettings();
          if (isEscapeToMainRequested()) break;
          resumeAfterSettingsChild();
          continue;
        }
        /* WIFI submenu */
        if (settingsSelector == SETTINGS_ITEMS_COUNT - 6) {
          showWiFiSettings();
          if (isEscapeToMainRequested()) break;
          resumeAfterSettingsChild();
          continue;
        }
        /* LOGGING submenu */
        if (settingsSelector == SETTINGS_ITEMS_COUNT - 5) {
          showLoggingSettings();
          if (isEscapeToMainRequested()) break;
          resumeAfterSettingsChild();
          continue;
        }
        /* LOCK submenu */
        if (settingsSelector == SETTINGS_ITEMS_COUNT - 4) {
          showLockSettings();
          if (isEscapeToMainRequested()) break;
          resumeAfterSettingsChild();
          continue;
        }
        /* USB */
        if (settingsSelector == SETTINGS_ITEMS_COUNT - 3) {
          showUSBPortalScreen();
          if (isEscapeToMainRequested()) break;
          resumeAfterSettingsChild();
          continue;
        }
        /* RESET */
        if (settingsSelector == SETTINGS_ITEMS_COUNT - 2) {
          showResetSubmenu();
          if (isEscapeToMainRequested()) break;
          resumeAfterSettingsChild();
          continue;
        }
        /* ABOUT */
        if (settingsSelector == SETTINGS_ITEMS_COUNT - 1) {
          showAboutScreen();
          resumeAfterSettingsChild();
          continue;
        }
        /* STATS - instant toggle */
        if (g_settingsMenu.item[settingsSelector - 1].value == (void*)&g_statsEnabled) {
          g_statsEnabled = g_statsEnabled ? 0 : 1;
          saveEEPROM(g_storedVar);
          initMenuItems();
          prevSettingsSelector = 0;
        /* Value items */
        } else if (g_settingsMenu.item[settingsSelector - 1].value != ITEM_NO_VALUE) {
          settingsValuePtr = (uint16_t *)g_settingsMenu.item[settingsSelector - 1].value;
          originalSettingsValue = *settingsValuePtr;
          settingsMenuState = VALUE_SELECTION;
          g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
          setUiEncoderBoundaries(g_settingsMenu.item[settingsSelector - 1].minValue,
                                 g_settingsMenu.item[settingsSelector - 1].maxValue, false);
          resetUiEncoder(*settingsValuePtr);
        }
      } else {
        /* Confirm edit */
        settingsMenuState = ITEM_SELECTION;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(1, SETTINGS_ITEMS_COUNT, false);
        resetUiEncoder(settingsSelector);
        saveEEPROM(g_storedVar);
        initMenuItems();
      }
      delay(200);
    }

    /* Encoder movement */
    if (g_rotaryEncoder.encoderChanged()) {
      lastSettingsInteraction = millis();
      if (settingsMenuState == ITEM_SELECTION) {
        settingsSelector = readUiEncoder();
      } else {
        uint16_t newValue = (uint16_t)readUiEncoder();
        *settingsValuePtr = newValue;
      }
    }

    /* Brake button = cancel edit or exit */
    static bool brakeBtnInSettings = false;
    static uint32_t lastBrakeBtnSettingsTime = 0;
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      if (!brakeBtnInSettings && millis() - lastBrakeBtnSettingsTime > BUTTON_SHORT_PRESS_DEBOUNCE_MS) {
        brakeBtnInSettings = true;
        lastBrakeBtnSettingsTime = millis();
        lastSettingsInteraction = millis();
        if (settingsMenuState == VALUE_SELECTION) {
          if (settingsValuePtr != NULL) {
            *settingsValuePtr = originalSettingsValue;
          }
          settingsMenuState = ITEM_SELECTION;
          g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
          setUiEncoderBoundaries(1, SETTINGS_ITEMS_COUNT, false);
          resetUiEncoder(settingsSelector);
          obdFill(&g_obd, OBD_WHITE, 1);
        } else {
          break;
        }
      }
    } else {
      brakeBtnInSettings = false;
    }

    /* Long press encoder button = escape to main + race mode toggle */
    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }

    /* Frame scrolling */
    if (settingsMenuState == ITEM_SELECTION) {
      if (settingsSelector > frameLower) {
        frameLower = settingsSelector;
        frameUpper = frameLower - visibleLines + 1;
        obdFill(&g_obd, OBD_WHITE, 1);
      } else if (settingsSelector < frameUpper) {
        frameUpper = settingsSelector;
        frameLower = frameUpper + visibleLines - 1;
        obdFill(&g_obd, OBD_WHITE, 1);
      }
    }

    uint8_t menuFont = (g_storedVar.listFontSize == FONT_SIZE_SMALL) ? FONT_8x8 : FONT_12x16;
    uint8_t charWidth = (g_storedVar.listFontSize == FONT_SIZE_SMALL) ? WIDTH8x8 : WIDTH12x16;
    uint8_t lineHeight = (g_storedVar.listFontSize == FONT_SIZE_SMALL) ? 8 : HEIGHT12x16;

    for (uint8_t i = 0; i < visibleLines; i++) {
      uint16_t itemIndex = frameUpper - 1 + i;
      if (itemIndex >= SETTINGS_ITEMS_COUNT) break;

      bool isSelected = (settingsSelector - frameUpper == i && settingsMenuState == ITEM_SELECTION);
      obdWriteString(&g_obd, 0, 0, i * lineHeight, g_settingsMenu.item[itemIndex].name,
                     menuFont, isSelected ? OBD_WHITE : OBD_BLACK, 1);

      if (g_settingsMenu.item[itemIndex].value != ITEM_NO_VALUE) {
        bool isValueSelected = (settingsSelector - frameUpper == i && settingsMenuState == VALUE_SELECTION);
        uint16_t value = *(uint16_t *)(g_settingsMenu.item[itemIndex].value);
        if (g_settingsMenu.item[itemIndex].type == VALUE_TYPE_STRING) {
          snprintf(msgStr, sizeof(msgStr), "%3s", getOnOffLabel(g_storedVar.language, value ? 1 : 0));
        } else if (g_settingsMenu.item[itemIndex].unit[0] != '\0') {
          snprintf(msgStr, sizeof(msgStr), "%2d%s", value, g_settingsMenu.item[itemIndex].unit);
        } else {
          sprintf(msgStr, "%3d", value);
        }
        int textWidth = strlen(msgStr) * charWidth;
        obdWriteString(&g_obd, 0, OLED_WIDTH - textWidth, i * lineHeight, msgStr,
                       menuFont, isValueSelected ? OBD_WHITE : OBD_BLACK, 1);
      }
    }

    vTaskDelay(10);
  }

  setInSettingsMenu(false);
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(1, getMainMenuItemsCount(), false);
  resetUiEncoder(getMainMenuSelector());
  g_escVar.encoderPos = getMainMenuSelector();
  saveEEPROM(g_storedVar);
  obdFill(&g_obd, OBD_WHITE, 1);
}
