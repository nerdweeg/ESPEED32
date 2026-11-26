#include "settings_hardware_menu.h"
#include <Arduino.h>
#include "slot_ESC.h"
#include "ui_text_access.h"
#include "HAL.h"
#include "settings_ext_pot_menu.h"
#include "diagnostics_self_test.h"
#include "app_globals.h"
#include "app_init.h"
#include "power_ui.h"
#include "ui_render.h"
#include "menu_input.h"

static const char* HARDWARE_MENU_LABELS[LANG_COUNT][HARDWARE_ITEMS_COUNT] = {
  {"ENC.INVERT", "EKST.POT.", "TRIGGER", "PWM_F MAX", "TEST", "TILBAKE"},
  {"ENC.INVERT", "EXT.POT.", "TRIGGER", "PWM_F MAX", "TEST", "BACK"},
  {"ENC.INVERT", "POT.EXT.", "TRIGGER", "PWM_F MAX", "TEST", "ATRAS"},
  {"ENC.INVERT", "EXT.POT.", "TRIGGER", "PWM_F MAX", "TEST", "ZURUCK"},
  {"ENC.INVERT", "POT.EST.", "TRIGGER", "PWM_F MAX", "TEST", "INDIETRO"},
  {"ENC.INVERT", "EXT.POT.", "TRIGGER", "PWM_F MAX", "TEST", "TERUG"},
  {"ENC.INVERT", "POT.EXT.", "TRIGGER", "PWM_F MAX", "TESTE", "VOLTAR"},
  {"ENC.INVERT", "EXT.POT.", "TRIGGER", "PWM_F MAX", "TEST", "TILLBAKA"}
};

static const char* HARDWARE_MENU_LABELS_PASCAL[LANG_COUNT][HARDWARE_ITEMS_COUNT] = {
  {"Enc.Invert", "Ekst.Pot.", "Trigger", "Pwm_F Max", "Test", "Tilbake"},
  {"Enc.Invert", "Ext.Pot.", "Trigger", "Pwm_F Max", "Test", "Back"},
  {"Enc.Invert", "Pot.Ext.", "Trigger", "Pwm_F Max", "Test", "Atras"},
  {"Enc.Invert", "Ext.Pot.", "Trigger", "Pwm_F Max", "Test", "Zuruck"},
  {"Enc.Invert", "Pot.Est.", "Trigger", "Pwm_F Max", "Test", "Indietro"},
  {"Enc.Invert", "Ext.Pot.", "Trigger", "Pwm_F Max", "Test", "Terug"},
  {"Enc.Invert", "Pot.Ext.", "Trigger", "Pwm_F Max", "Teste", "Voltar"},
  {"Enc.Invert", "Ext.Pot.", "Trigger", "Pwm_F Max", "Test", "Tillbaka"}
};

static const char* SENSOR_MENU_LABELS[LANG_COUNT][4] = {
  {"FAMILIE", "AKTIV", "TYPE", "TILBAKE"},
  {"FAMILY", "ACTIVE", "TYPE", "BACK"},
  {"FAMILIA", "ACTIVO", "TYPE", "ATRAS"},
  {"FAMILIE", "AKTIV", "TYPE", "ZURUCK"},
  {"FAMIGLIA", "ATTIVO", "TYPE", "INDIETRO"},
  {"FAMILIE", "ACTIEF", "TYPE", "TERUG"},
  {"FAMILIA", "ATIVO", "TYPE", "VOLTAR"},
  {"FAMILJ", "AKTIV", "TYP", "TILLBAKA"}
};

static const char* SENSOR_MENU_LABELS_PASCAL[LANG_COUNT][4] = {
  {"Familie", "Aktiv", "Type", "Tilbake"},
  {"Family", "Active", "Type", "Back"},
  {"Familia", "Activo", "Type", "Atras"},
  {"Familie", "Aktiv", "Type", "Zuruck"},
  {"Famiglia", "Attivo", "Type", "Indietro"},
  {"Familie", "Actief", "Type", "Terug"},
  {"Familia", "Ativo", "Type", "Voltar"},
  {"Familj", "Aktiv", "Typ", "Tillbaka"}
};

static const char* getHardwareMenuLabel(uint8_t lang, uint8_t item) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL)
    ? HARDWARE_MENU_LABELS_PASCAL[lang][item]
    : HARDWARE_MENU_LABELS[lang][item];
}

static const char* getSensorMenuLabel(uint8_t lang, uint8_t item) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL)
    ? SENSOR_MENU_LABELS_PASCAL[lang][item]
    : SENSOR_MENU_LABELS[lang][item];
}

static void clearValueField(uint8_t x, uint8_t y, uint8_t widthPx, uint8_t heightPx) {
  if (widthPx == 0 || heightPx == 0) return;

  uint8_t xEnd = x + widthPx - 1;
  for (uint8_t row = 0; row < heightPx; row++) {
    obdDrawLine(&g_obd, x, y + row, xEnd, y + row, OBD_WHITE, 1);
  }
}

static void copyConfiguredCaseValue(const char* value, char* buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) return;
  buffer[0] = '\0';
  if (value == nullptr) return;

  snprintf(buffer, bufferSize, "%s", value);

  if (g_storedVar.textCase != TEXT_CASE_PASCAL) {
    return;
  }

  bool hasLetters = false;
  bool canConvertToPascal = true;
  for (size_t i = 0; buffer[i] != '\0'; i++) {
    char c = buffer[i];
    if (c >= 'A' && c <= 'Z') {
      hasLetters = true;
      continue;
    }
    if (c >= 'a' && c <= 'z') {
      hasLetters = true;
      canConvertToPascal = false;
      break;
    }
    if ((c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-') {
      canConvertToPascal = false;
      break;
    }
  }

  if (!hasLetters || !canConvertToPascal) {
    return;
  }

  for (size_t i = 1; buffer[i] != '\0'; i++) {
    if (buffer[i] >= 'A' && buffer[i] <= 'Z') {
      buffer[i] = (char)(buffer[i] - 'A' + 'a');
    }
  }
}

static void drawRightAlignedValue(uint8_t y, const char* value, bool selected, uint8_t fieldChars = 0) {
  if (value == nullptr || value[0] == '\0') return;

  copyConfiguredCaseValue(value, msgStr, sizeof(msgStr));

  uint8_t fieldLen = (uint8_t)strlen(msgStr);
  if (fieldChars > fieldLen) {
    fieldLen = fieldChars;
  }

  uint8_t fieldWidthPx = fieldLen * WIDTH8x8;
  uint8_t fieldX = OLED_WIDTH - fieldWidthPx;
  clearValueField(fieldX, y, fieldWidthPx, HEIGHT8x8);

  uint8_t vx = OLED_WIDTH - (uint8_t)(strlen(msgStr) * WIDTH8x8);
  obdWriteString(&g_obd, 0, vx, y, msgStr,
                 FONT_8x8, selected ? OBD_WHITE : OBD_BLACK, 1);
}

static void showTriggerSensorSettings() {
  const uint8_t menuFont = FONT_8x8;
  const uint8_t lineH = HEIGHT8x8;
  const bool supportsTypeOverride = HAL_TriggerSensorSupportsTypeOverride();
  const uint8_t numItems = supportsTypeOverride ? 4 : 3;
  const uint8_t itemFamily = 0;
  const uint8_t itemActive = 1;
  const uint8_t itemType = 2;
  const uint8_t itemBack = numItems - 1;

  obdFill(&g_obd, OBD_WHITE, 1);
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, numItems - 1, false);
  resetUiEncoder(0);

  uint8_t sel = 0;
  bool needRedraw = true;
  MenuState_enum trigMenuState = ITEM_SELECTION;
  uint16_t tempTrigType = 0;
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
      if (trigMenuState == ITEM_SELECTION) {
        sel = (uint8_t)readUiEncoder();
      } else {
        tempTrigType = (uint16_t)readUiEncoder();
      }
      needRedraw = true;
    }

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      if (trigMenuState == VALUE_SELECTION) {
        /* Confirm */
        (void)HAL_SetTriggerSensorTypeOverride(tempTrigType);
        trigMenuState = ITEM_SELECTION;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, numItems - 1, false);
        resetUiEncoder(sel);
        needRedraw = true;
      } else {
        if (sel == itemBack) {
          break;
        } else if (supportsTypeOverride && sel == itemType) {
          tempTrigType = HAL_GetTriggerSensorTypeOverride();
          trigMenuState = VALUE_SELECTION;
          g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
          setUiEncoderBoundaries(TRIGGER_SENSOR_TYPE_AUTO, TRIGGER_SENSOR_TYPE_MAX, false);
          resetUiEncoder(tempTrigType);
          needRedraw = true;
        }
      }
      delay(200);
    }

    static BrakeButtonEdge brakeBtnInMenu;
    if (brakeBtnInMenu.pressed()) {
      if (trigMenuState == VALUE_SELECTION) {
        /* Cancel */
        trigMenuState = ITEM_SELECTION;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, numItems - 1, false);
        resetUiEncoder(sel);
        obdFill(&g_obd, OBD_WHITE, 1);
        needRedraw = true;
      } else {
        waitForBrakeButtonRelease();
        break;
      }
    }

    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }

    if (needRedraw) {
      needRedraw = false;
      uint8_t lang = g_storedVar.language;
      char familyBuf[16];
      char activeBuf[16];
      char typeBuf[16];
      HAL_GetTriggerSensorFamilyLabel(familyBuf, sizeof(familyBuf));
      HAL_GetTriggerSensorActiveTypeLabel(activeBuf, sizeof(activeBuf));
      uint16_t displayType = (trigMenuState == VALUE_SELECTION) ? tempTrigType : HAL_GetTriggerSensorTypeOverride();
      HAL_GetTriggerSensorTypeOptionLabel(displayType, typeBuf, sizeof(typeBuf));

      for (uint8_t i = 0; i < numItems; i++) {
        bool isNameSel  = (sel == i && trigMenuState == ITEM_SELECTION);
        bool isValueSel = (sel == i && trigMenuState == VALUE_SELECTION);
        const char* label = (i == itemBack)
          ? getBackLabel(lang)
          : getSensorMenuLabel(lang, i);
        obdWriteString(&g_obd, 0, 0, i * lineH, (char*)label,
                       menuFont, isNameSel ? OBD_WHITE : OBD_BLACK, 1);

        const char* value = nullptr;
        if (i == itemFamily) value = familyBuf;
        else if (i == itemActive) value = activeBuf;
        else if (supportsTypeOverride && i == itemType) value = typeBuf;

        if (value != nullptr && value[0] != '\0') {
          drawRightAlignedValue(i * lineH, value, isNameSel || isValueSel, 8);
        }
      }
    }

    vTaskDelay(10);
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}

void showHardwareSettings() {
  const uint8_t menuFont = FONT_8x8;
  const uint8_t lineH = HEIGHT8x8;
  const uint8_t itemEncInv = 0;
  const uint8_t itemExtPot = 1;
  const uint8_t itemTrigger = 2;
  const uint8_t itemPwmMax = 3;
  const uint8_t itemTest = 4;
  const uint8_t itemBack = 5;

  obdFill(&g_obd, OBD_WHITE, 1);
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, HARDWARE_ITEMS_COUNT - 1, false);
  resetUiEncoder(0);

  uint8_t sel = 0;
  bool needRedraw = true;
  MenuState_enum hwMenuState = ITEM_SELECTION;
  uint16_t tempPwmProfile = 0;
  uint32_t lastInteraction = millis();
  bool screensaverActive = false;
  uint16_t screensaverEncoderPos = (uint16_t)readUiEncoder();

  auto resumeAfterChild = [&]() {
    lastInteraction = millis();
    screensaverActive = false;
    screensaverEncoderPos = (uint16_t)readUiEncoder();
    g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
    setUiEncoderBoundaries(0, HARDWARE_ITEMS_COUNT - 1, false);
    resetUiEncoder(sel);
    obdFill(&g_obd, OBD_WHITE, 1);
    needRedraw = true;
  };

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
      if (hwMenuState == ITEM_SELECTION) {
        sel = (uint8_t)readUiEncoder();
      } else {
        tempPwmProfile = (uint16_t)readUiEncoder();
      }
      needRedraw = true;
    }

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      if (hwMenuState == VALUE_SELECTION) {
        /* Confirm PWM_F MAX edit */
        if (applyConfiguredPwmFreqMaxProfile(tempPwmProfile)) {
          initMenuItems();
          saveEEPROM(g_storedVar);
        }
        hwMenuState = ITEM_SELECTION;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, HARDWARE_ITEMS_COUNT - 1, false);
        resetUiEncoder(sel);
        needRedraw = true;
      } else {
        if (sel == itemBack) {
          break;
        } else if (sel == itemEncInv) {
          applyEncoderInvertSetting(g_encoderInvertEnabled ? 0 : 1);
          saveEEPROM(g_storedVar);
          needRedraw = true;
        } else if (sel == itemExtPot) {
          showExtPotSettings();
          if (isEscapeToMainRequested()) break;
          resumeAfterChild();
          continue;
        } else if (sel == itemTrigger) {
          showTriggerSensorSettings();
          if (isEscapeToMainRequested()) break;
          resumeAfterChild();
          continue;
        } else if (sel == itemPwmMax) {
          /* Enter VALUE_SELECTION */
          hwMenuState = VALUE_SELECTION;
          tempPwmProfile = getConfiguredPwmFreqMaxProfile();
          g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
          setUiEncoderBoundaries(PWM_FREQ_MAX_PROFILE_5K, PWM_FREQ_MAX_PROFILE_25K, false);
          resetUiEncoder(tempPwmProfile);
          needRedraw = true;
        } else if (sel == itemTest) {
          showSelfTest();
          if (isEscapeToMainRequested()) break;
          resumeAfterChild();
          continue;
        }
      }
      delay(200);
    }

    static BrakeButtonEdge brakeBtnInMenu;
    if (brakeBtnInMenu.pressed()) {
      if (hwMenuState == VALUE_SELECTION) {
        /* Cancel - discard edit */
        hwMenuState = ITEM_SELECTION;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, HARDWARE_ITEMS_COUNT - 1, false);
        resetUiEncoder(sel);
        obdFill(&g_obd, OBD_WHITE, 1);
        needRedraw = true;
      } else {
        waitForBrakeButtonRelease();
        break;
      }
    }

    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }

    if (needRedraw) {
      needRedraw = false;
      uint8_t lang = g_storedVar.language;
      char familyBuf[16];
      char pwmMaxBuf[8];
      HAL_GetTriggerSensorFamilyLabel(familyBuf, sizeof(familyBuf));
      uint16_t displayProfile = (hwMenuState == VALUE_SELECTION) ? tempPwmProfile : getConfiguredPwmFreqMaxProfile();
      snprintf(pwmMaxBuf, sizeof(pwmMaxBuf), "%uk", (unsigned int)pwmFreqMaxProfileToWholeKHz(displayProfile));

      for (uint8_t i = 0; i < HARDWARE_ITEMS_COUNT; i++) {
        bool isNameSel  = (sel == i && hwMenuState == ITEM_SELECTION);
        bool isValueSel = (sel == i && hwMenuState == VALUE_SELECTION);
        const char* label = (i == itemBack)
          ? getBackLabel(lang)
          : getHardwareMenuLabel(lang, i);
        obdWriteString(&g_obd, 0, 0, i * lineH, (char*)label,
                       menuFont, isNameSel ? OBD_WHITE : OBD_BLACK, 1);

        const char* value = nullptr;
        if (i == itemEncInv) {
          value = getOnOffLabel(lang, g_encoderInvertEnabled ? 1 : 0);
        } else if (i == itemTrigger) {
          value = familyBuf;
        } else if (i == itemPwmMax) {
          value = pwmMaxBuf;
        }

        if (value != nullptr && value[0] != '\0') {
          uint8_t fieldChars = 8;
          if (i == itemEncInv) {
            fieldChars = 3;
          } else if (i == itemPwmMax) {
            fieldChars = 3;
          }
          drawRightAlignedValue(i * lineH, value, isNameSel || isValueSel, fieldChars);
        }
      }
    }

    vTaskDelay(10);
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}
