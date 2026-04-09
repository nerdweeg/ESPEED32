#include "menu_car.h"
#include <Arduino.h>
#include "slot_ESC.h"
#include "ui_strings.h"
#include "ui_text_access.h"
#include "settings_reset_menu.h"

extern StoredVar_type g_storedVar;
extern ESC_type g_escVar;
extern OBDISP g_obd;
extern uint16_t g_carSel;
extern Menu_type g_carMenu;
extern AiEsp32RotaryEncoder g_rotaryEncoder;
extern char msgStr[50];
extern bool g_isEditingCarSelection;

extern bool consumeScreensaverWakeInput(bool wakeTriggered);
extern bool refreshIdleInteractionFromControls(uint32_t* lastInteraction, bool* screensaverActive, uint16_t* lastEncoderPos);
extern bool serviceIdlePowerTransitions(uint32_t* lastInteraction, bool* screensaverActive);
extern bool checkRaceModeEscape();
extern void requestEscapeToMain();
extern bool isEscapeToMainRequested();
extern uint8_t getMainMenuSelector();
extern uint8_t getMainMenuItemsCount();

extern void showScreensaver();
extern void initMenuItems();
extern void saveEEPROM(StoredVar_type toSave);

void showCarSelection() {
  /* "Frame" indicates which items are currently displayed.
     It consist of a lower and upper bound: only the items within this boundaries are displayed.
     The difference between upper and lower bound is fixed to be g_carMenu.lines
     It's important that the encoder boundaries matches the menu items (e.g., 10 items (cars), encoder boundaries must be [0,9]) */
  static uint16_t frameUpper = 1;
  static uint16_t frameLower = g_carMenu.lines;

  /* Clear screen */
  obdFill(&g_obd, OBD_WHITE, 1);

  /* Save original value for cancel */
  uint16_t originalCarNumber = g_storedVar.selectedCarNumber;

  /* Set encoder to car selection parameter */
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, CAR_MAX_COUNT - 1, false);
  resetUiEncoder(g_storedVar.selectedCarNumber);

  /* Screensaver support */
  uint32_t lastInteraction = millis();
  bool screensaverActive = false;
  uint16_t screensaverEncoderPos = (uint16_t)readUiEncoder();

  /* Exit car selection when encoder is clicked */
  while (true)
  {
    bool wakeUpTriggered = refreshIdleInteractionFromControls(&lastInteraction, &screensaverActive, &screensaverEncoderPos);
    if (wakeUpTriggered) {
      obdFill(&g_obd, OBD_WHITE, 1);
    }

    /* Check for screensaver timeout */
    if (consumeScreensaverWakeInput(wakeUpTriggered)) { continue; }

    if (!wakeUpTriggered && g_storedVar.screensaverTimeout > 0 && millis() - lastInteraction > (g_storedVar.screensaverTimeout * 1000UL)) {
      if (g_escVar.trigger_norm == 0) {
        if (!screensaverActive) {
          screensaverActive = true;
          screensaverEncoderPos = readUiEncoder();
          showScreensaver();
        }
        if (serviceIdlePowerTransitions(&lastInteraction, &screensaverActive)) {
          obdFill(&g_obd, OBD_WHITE, 1);
        }
        delay(10);  /* Small delay to prevent watchdog timeout */
        continue;  /* Don't draw menu while screensaver is active */
      }
    }

    /* Check for brake button press - cancel and exit */
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      lastInteraction = millis();
      delay(BUTTON_SHORT_PRESS_DEBOUNCE_MS);
      g_storedVar.selectedCarNumber = originalCarNumber;  /* Restore original */
      obdFill(&g_obd, OBD_WHITE, 1);
      return;
    }

    /* Get encoder value if changed */
    if (g_rotaryEncoder.encoderChanged()) {
      lastInteraction = millis();
      g_storedVar.selectedCarNumber = readUiEncoder();
    }

    /* If encoder move out of frame, adjust frame */
    if (g_storedVar.selectedCarNumber > frameLower)
    {
      frameLower = g_storedVar.selectedCarNumber;
      frameUpper = frameLower - g_carMenu.lines + 1;
      obdFill(&g_obd, OBD_WHITE, 1);
    }
    else if (g_storedVar.selectedCarNumber < frameUpper)
    {
      frameUpper = g_storedVar.selectedCarNumber;
      frameLower = frameUpper + g_carMenu.lines - 1;
      obdFill(&g_obd, OBD_WHITE, 1);
    }

    /* Print car menu */
    for (uint8_t i = 0; i < g_carMenu.lines; i++)
    {
      /* Print the item (car) name */
      obdWriteString(&g_obd, 0, 0, i * HEIGHT12x16, g_carMenu.item[frameUpper + i].name, FONT_12x16, (g_storedVar.selectedCarNumber - frameUpper == i) ? OBD_WHITE : OBD_BLACK, 1);
      if (g_carMenu.item[frameUpper + i].value != ITEM_NO_VALUE)
      {
        /* value is a generic pointer to void, so first cast to uint16_t pointer, then take the pointed value */
        sprintf(msgStr, "%2d", *(uint16_t *)(g_carMenu.item[frameUpper + i].value));
        /* Print the item value (car number) */
        obdWriteString(&g_obd, 0, OLED_WIDTH - 24, i * HEIGHT12x16, msgStr, FONT_12x16, OBD_BLACK, 1);
      }
    }

    /* Print car selection label on the bottom of the screen */
    const char* selLabel = STR_SELECT_CAR[g_storedVar.language];
    int selLabelWidth = strlen(selLabel) * 6;
    obdWriteString(&g_obd, 0, (OLED_WIDTH - selLabelWidth) / 2, OLED_HEIGHT - HEIGHT8x8, (char *)selLabel, FONT_6x8, OBD_WHITE, 1);

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      break;
    }

    /* Long press = escape to main for race mode toggle */
    if (checkRaceModeEscape()) { requestEscapeToMain(); return; }
  }

  /* Reset encoder and clear is done in caller routine */
  return;
}


/**
 * Show the Copy Car Settings screen. Allows copying all parameters from one car to another.
 */
void showCopyCarSettings() {
  static uint16_t frameUpper = 1;
  static uint16_t frameLower = g_carMenu.lines;
  uint16_t sourceCar = 0;
  uint16_t destCar = 0;

  /* ========== SELECT SOURCE CAR (FROM) ========== */
  /* Clear screen */
  obdFill(&g_obd, OBD_WHITE, 1);

  /* Screensaver support */
  uint32_t lastInteraction = millis();
  bool screensaverActive = false;
  uint16_t screensaverEncoderPos = (uint16_t)readUiEncoder();

  /* Set encoder to car selection parameter */
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, CAR_MAX_COUNT - 1, false);
  resetUiEncoder(g_storedVar.selectedCarNumber);
  sourceCar = g_storedVar.selectedCarNumber;

  /* Select source car */
  while (true)
  {
    bool wakeUpTriggered = refreshIdleInteractionFromControls(&lastInteraction, &screensaverActive, &screensaverEncoderPos);
    if (wakeUpTriggered) {
      obdFill(&g_obd, OBD_WHITE, 1);
    }

    /* Check for screensaver timeout */
    if (consumeScreensaverWakeInput(wakeUpTriggered)) { continue; }

    if (!wakeUpTriggered && g_storedVar.screensaverTimeout > 0 && millis() - lastInteraction > (g_storedVar.screensaverTimeout * 1000UL)) {
      if (g_escVar.trigger_norm == 0) {
        if (!screensaverActive) {
          screensaverActive = true;
          screensaverEncoderPos = readUiEncoder();
          showScreensaver();
        }
        if (serviceIdlePowerTransitions(&lastInteraction, &screensaverActive)) {
          obdFill(&g_obd, OBD_WHITE, 1);
        }
        delay(10);  /* Small delay to prevent watchdog timeout */
        continue;  /* Don't draw menu while screensaver is active */
      }
    }

    /* Check for brake button press - cancel and exit */
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      lastInteraction = millis();
      delay(BUTTON_SHORT_PRESS_DEBOUNCE_MS);
      obdFill(&g_obd, OBD_WHITE, 1);
      return;
    }

    /* Get encoder value if changed */
    if (g_rotaryEncoder.encoderChanged()) {
      lastInteraction = millis();
      sourceCar = readUiEncoder();
    }

    /* If encoder move out of frame, adjust frame */
    if (sourceCar > frameLower)
    {
      frameLower = sourceCar;
      frameUpper = frameLower - g_carMenu.lines + 1;
      obdFill(&g_obd, OBD_WHITE, 1);
    }
    else if (sourceCar < frameUpper)
    {
      frameUpper = sourceCar;
      frameLower = frameUpper + g_carMenu.lines - 1;
      obdFill(&g_obd, OBD_WHITE, 1);
    }

    /* Print car menu */
    for (uint8_t i = 0; i < g_carMenu.lines; i++)
    {
      /* Print the item (car) name */
      obdWriteString(&g_obd, 0, 0, i * HEIGHT12x16, g_carMenu.item[frameUpper + i].name, FONT_12x16, (sourceCar - frameUpper == i) ? OBD_WHITE : OBD_BLACK, 1);
      if (g_carMenu.item[frameUpper + i].value != ITEM_NO_VALUE)
      {
        /* Print the item value (car number) */
        sprintf(msgStr, "%2d", *(uint16_t *)(g_carMenu.item[frameUpper + i].value));
        obdWriteString(&g_obd, 0, OLED_WIDTH - 24, i * HEIGHT12x16, msgStr, FONT_12x16, OBD_BLACK, 1);
      }
    }

    /* Print "COPY FROM:" on the bottom of the screen */
    const char* fromLabel = STR_COPY_FROM[g_storedVar.language];
    int fromLabelWidth = strlen(fromLabel) * 6;
    obdWriteString(&g_obd, 0, (OLED_WIDTH - fromLabelWidth) / 2, OLED_HEIGHT - HEIGHT8x8, (char *)fromLabel, FONT_6x8, OBD_WHITE, 1);

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      break;
    }

    /* Long press = escape to main for race mode toggle */
    if (checkRaceModeEscape()) { requestEscapeToMain(); return; }
  }

  /* Small delay to prevent double-click */
  delay(200);
  if (isEscapeToMainRequested()) return;

  /* ========== SELECT DESTINATION CAR (TO) ========== */
  /* Clear screen */
  obdFill(&g_obd, OBD_WHITE, 1);

  /* Reset screensaver tracking */
  lastInteraction = millis();
  screensaverActive = false;

  /* Reset encoder for destination car selection (CAR_MAX_COUNT = ALL option) */
  setUiEncoderBoundaries(0, CAR_MAX_COUNT, false);  /* 0-19 = cars, 20 = ALL */
  resetUiEncoder(g_storedVar.selectedCarNumber);
  destCar = g_storedVar.selectedCarNumber;

  /* Reset frame if needed */
  frameUpper = 1;
  frameLower = g_carMenu.lines;

  uint8_t visibleLines = g_carMenu.lines;
  const uint16_t ALL_CARS_OPTION = CAR_MAX_COUNT;  /* Index for "ALL" option */

  /* Select destination car */
  while (true)
  {
    bool wakeUpTriggered = refreshIdleInteractionFromControls(&lastInteraction, &screensaverActive, &screensaverEncoderPos);
    if (wakeUpTriggered) {
      obdFill(&g_obd, OBD_WHITE, 1);
    }

    /* Check for screensaver timeout */
    if (consumeScreensaverWakeInput(wakeUpTriggered)) { continue; }

    if (!wakeUpTriggered && g_storedVar.screensaverTimeout > 0 && millis() - lastInteraction > (g_storedVar.screensaverTimeout * 1000UL)) {
      if (g_escVar.trigger_norm == 0) {
        if (!screensaverActive) {
          screensaverActive = true;
          screensaverEncoderPos = readUiEncoder();
          showScreensaver();
        }
        if (serviceIdlePowerTransitions(&lastInteraction, &screensaverActive)) {
          obdFill(&g_obd, OBD_WHITE, 1);
        }
        delay(10);  /* Small delay to prevent watchdog timeout */
        continue;  /* Don't draw menu while screensaver is active */
      }
    }

    /* Check for brake button press - cancel and exit */
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      lastInteraction = millis();
      delay(BUTTON_SHORT_PRESS_DEBOUNCE_MS);
      obdFill(&g_obd, OBD_WHITE, 1);
      return;
    }

    /* Get encoder value if changed */
    if (g_rotaryEncoder.encoderChanged()) {
      lastInteraction = millis();
      destCar = readUiEncoder();
    }

    /* If encoder move out of frame, adjust frame */
    if (destCar > frameLower)
    {
      frameLower = destCar;
      frameUpper = frameLower - visibleLines + 1;
      obdFill(&g_obd, OBD_WHITE, 1);
    }
    else if (destCar < frameUpper)
    {
      frameUpper = destCar;
      frameLower = frameUpper + visibleLines - 1;
      obdFill(&g_obd, OBD_WHITE, 1);
    }

    /* Print car menu */
    for (uint8_t i = 0; i < visibleLines; i++)
    {
      uint16_t itemIndex = frameUpper + i;
      bool isSelected = (destCar == itemIndex);

      if (itemIndex == ALL_CARS_OPTION) {
        /* Print "ALL" option */
        obdWriteString(&g_obd, 0, 0, i * HEIGHT12x16, (char *)STR_ALL[g_storedVar.language], FONT_12x16, isSelected ? OBD_WHITE : OBD_BLACK, 1);
      } else if (itemIndex < CAR_MAX_COUNT) {
        /* Print the item (car) name */
        obdWriteString(&g_obd, 0, 0, i * HEIGHT12x16, g_carMenu.item[itemIndex].name, FONT_12x16, isSelected ? OBD_WHITE : OBD_BLACK, 1);
        if (g_carMenu.item[itemIndex].value != ITEM_NO_VALUE)
        {
          /* Print the item value (car number) */
          sprintf(msgStr, "%2d", *(uint16_t *)(g_carMenu.item[itemIndex].value));
          obdWriteString(&g_obd, 0, OLED_WIDTH - 24, i * HEIGHT12x16, msgStr, FONT_12x16, OBD_BLACK, 1);
        }
      }
    }

    /* Print "COPY TO:" on the bottom of the screen */
    const char* toLabel = STR_COPY_TO[g_storedVar.language];
    int toLabelWidth = strlen(toLabel) * 6;
    obdWriteString(&g_obd, 0, (OLED_WIDTH - toLabelWidth) / 2, OLED_HEIGHT - HEIGHT8x8, (char *)toLabel, FONT_6x8, OBD_WHITE, 1);

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      break;
    }

    /* Long press = escape to main for race mode toggle */
    if (checkRaceModeEscape()) { requestEscapeToMain(); return; }
  }

  /* Copy car parameters */
  if (destCar == ALL_CARS_OPTION) {
    /* Copy to ALL cars (except source car) */
    for (uint16_t i = 0; i < CAR_MAX_COUNT; i++) {
      if (i != sourceCar) {
        g_storedVar.carParam[i].minSpeed = g_storedVar.carParam[sourceCar].minSpeed;
        g_storedVar.carParam[i].brake = g_storedVar.carParam[sourceCar].brake;
        g_storedVar.carParam[i].maxSpeed = g_storedVar.carParam[sourceCar].maxSpeed;
        g_storedVar.carParam[i].throttleCurveVertex.inputThrottle = g_storedVar.carParam[sourceCar].throttleCurveVertex.inputThrottle;
        g_storedVar.carParam[i].throttleCurveVertex.curveSpeedDiff = g_storedVar.carParam[sourceCar].throttleCurveVertex.curveSpeedDiff;
        g_storedVar.carParam[i].fade = g_storedVar.carParam[sourceCar].fade;
        g_storedVar.carParam[i].antiSpin = g_storedVar.carParam[sourceCar].antiSpin;
        g_storedVar.carParam[i].freqPWM = g_storedVar.carParam[sourceCar].freqPWM;
        g_storedVar.carParam[i].brakeButtonReduction = g_storedVar.carParam[sourceCar].brakeButtonReduction;
        g_storedVar.carParam[i].quickBrakeEnabled = g_storedVar.carParam[sourceCar].quickBrakeEnabled;
        g_storedVar.carParam[i].quickBrakeThreshold = g_storedVar.carParam[sourceCar].quickBrakeThreshold;
        g_storedVar.carParam[i].quickBrakeStrength = g_storedVar.carParam[sourceCar].quickBrakeStrength;
      }
    }
    /* Show confirmation message */
    obdFill(&g_obd, OBD_WHITE, 1);
    obdWriteString(&g_obd, 0, 0, 24, (char *)STR_COPIED_ALL[g_storedVar.language], FONT_12x16, OBD_BLACK, 1);
    delay(1000);
  } else if (sourceCar != destCar) {
    /* Copy to single car */
    g_storedVar.carParam[destCar].minSpeed = g_storedVar.carParam[sourceCar].minSpeed;
    g_storedVar.carParam[destCar].brake = g_storedVar.carParam[sourceCar].brake;
    g_storedVar.carParam[destCar].maxSpeed = g_storedVar.carParam[sourceCar].maxSpeed;
    g_storedVar.carParam[destCar].throttleCurveVertex.inputThrottle = g_storedVar.carParam[sourceCar].throttleCurveVertex.inputThrottle;
    g_storedVar.carParam[destCar].throttleCurveVertex.curveSpeedDiff = g_storedVar.carParam[sourceCar].throttleCurveVertex.curveSpeedDiff;
    g_storedVar.carParam[destCar].fade = g_storedVar.carParam[sourceCar].fade;
    g_storedVar.carParam[destCar].antiSpin = g_storedVar.carParam[sourceCar].antiSpin;
    g_storedVar.carParam[destCar].freqPWM = g_storedVar.carParam[sourceCar].freqPWM;
    g_storedVar.carParam[destCar].brakeButtonReduction = g_storedVar.carParam[sourceCar].brakeButtonReduction;
    g_storedVar.carParam[destCar].quickBrakeEnabled = g_storedVar.carParam[sourceCar].quickBrakeEnabled;
    g_storedVar.carParam[destCar].quickBrakeThreshold = g_storedVar.carParam[sourceCar].quickBrakeThreshold;
    g_storedVar.carParam[destCar].quickBrakeStrength = g_storedVar.carParam[sourceCar].quickBrakeStrength;

    /* Show confirmation message */
    obdFill(&g_obd, OBD_WHITE, 1);
    obdWriteString(&g_obd, 0, 34, 24, (char *)STR_COPIED[g_storedVar.language], FONT_12x16, OBD_BLACK, 1);
    delay(1000);
  }

  /* Reset frame for next time */
  frameUpper = 1;
  frameLower = g_carMenu.lines;

  return;
}


/**
 * Show the options to Select or Rename the Car, called upon selecting the CAR item in the main menu
 */
void showSelectRenameCar() {

  /* Trigger reading stops, so stop the motor */
  /* Set trigRaw to max throttle if throttle is reversed, set to min throttle otherwise */
  /* g_escVar.trigger_raw = THROTTLE_REV ? g_storedVar.maxTrigger_raw : g_storedVar.minTrigger_raw; */ /*TODO: decide what to do now that trigger reading is done on task 2 */

  uint16_t selectedOption = 0;
  /* Clear screen */
  obdFill(&g_obd, OBD_WHITE, 1);

  /* Set encoder to selection parameter */
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, 5, false); /* Boundaries are [0, 5] because there are six options */
  resetUiEncoder(selectedOption);

  /* Setup scrolling frame - number of visible items depends on font size */
  const uint8_t totalOptions = 6;
  uint8_t visibleLines = getMenuLines();
  uint8_t frameUpper = 0;
  uint8_t frameLower = visibleLines - 1;

  /* Track editing state for RACESWP option */
  bool isEditingRaceswp = false;
  uint16_t tempRaceswpValue = g_storedVar.gridCarSelectEnabled;

  /* Screensaver support */
  uint32_t lastInteraction = millis();
  bool screensaverActive = false;
  uint16_t screensaverEncoderPos = (uint16_t)readUiEncoder();

  /* Exit car selection when encoder is clicked */
  while (true)
  {
    bool wakeUpTriggered = refreshIdleInteractionFromControls(&lastInteraction, &screensaverActive, &screensaverEncoderPos);
    if (wakeUpTriggered) {
      obdFill(&g_obd, OBD_WHITE, 1);
    }

    /* Check for screensaver timeout */
    if (consumeScreensaverWakeInput(wakeUpTriggered)) { continue; }

    if (!wakeUpTriggered && g_storedVar.screensaverTimeout > 0 && millis() - lastInteraction > (g_storedVar.screensaverTimeout * 1000UL)) {
      if (g_escVar.trigger_norm == 0) {
        if (!screensaverActive) {
          screensaverActive = true;
          screensaverEncoderPos = readUiEncoder();
          showScreensaver();
        }
        if (serviceIdlePowerTransitions(&lastInteraction, &screensaverActive)) {
          obdFill(&g_obd, OBD_WHITE, 1);
        }
        delay(10);  /* Small delay to prevent watchdog timeout */
        continue;  /* Don't draw menu while screensaver is active */
      }
    }

    /* Check for brake button press - acts as "back" in CAR menu */
    static bool brakeBtnInCarMenu = false;
    static uint32_t lastBrakeBtnCarMenuTime = 0;
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      if (!brakeBtnInCarMenu && millis() - lastBrakeBtnCarMenuTime > BUTTON_SHORT_PRESS_DEBOUNCE_MS) {
        brakeBtnInCarMenu = true;
        lastBrakeBtnCarMenuTime = millis();
        lastInteraction = millis();

        if (isEditingRaceswp) {
          /* Cancel RACESWP editing */
          isEditingRaceswp = false;
          g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
          setUiEncoderBoundaries(0, 5, false);
          resetUiEncoder(selectedOption);
        } else {
          /* Exit CAR menu completely */
          goto exitCarMenu;
        }
      }
    } else {
      brakeBtnInCarMenu = false;
    }

    /* Get encoder value if changed */
    if (g_rotaryEncoder.encoderChanged()) {
      lastInteraction = millis();
      if (!isEditingRaceswp) {
        selectedOption = readUiEncoder();
      } else {
        /* When editing RACESWP, encoder changes the value */
        tempRaceswpValue = readUiEncoder();
      }
    }

    /* Adjust frame if selection moves outside visible area (only when not editing) */
    if (!isEditingRaceswp) {
      if (selectedOption > frameLower)
      {
        frameLower = selectedOption;
        frameUpper = frameLower - visibleLines + 1;
        obdFill(&g_obd, OBD_WHITE, 1);
      }
      else if (selectedOption < frameUpper)
      {
        frameUpper = selectedOption;
        frameLower = frameUpper + visibleLines - 1;
        obdFill(&g_obd, OBD_WHITE, 1);
      }
    }

    /* Determine font size and dimensions based on setting */
    uint8_t menuFont = (g_storedVar.listFontSize == FONT_SIZE_SMALL) ? FONT_8x8 : FONT_12x16;
    uint8_t charWidth = (g_storedVar.listFontSize == FONT_SIZE_SMALL) ? WIDTH8x8 : WIDTH12x16;
    uint8_t lineHeight = (g_storedVar.listFontSize == FONT_SIZE_SMALL) ? 8 : HEIGHT12x16;

    /* Display only the visible items within the frame */
    for (uint8_t i = 0; i < visibleLines; i++)
    {
      uint8_t optionIndex = frameUpper + i;
      if (optionIndex >= totalOptions) break;

      uint8_t yPos = i * lineHeight;
      bool isSelected = (optionIndex == selectedOption);

      uint16_t lang = g_storedVar.language;

      switch (optionIndex)
      {
        case CAR_OPTION_SELECT:
          obdWriteString(&g_obd, 0, 0, yPos, (char *)getCarMenuOption(lang, 0), menuFont, isSelected ? OBD_WHITE : OBD_BLACK, 1);
          break;

        case CAR_OPTION_RENAME:
          obdWriteString(&g_obd, 0, 0, yPos, (char *)getCarMenuOption(lang, 1), menuFont, isSelected ? OBD_WHITE : OBD_BLACK, 1);
          break;

        case CAR_OPTION_GRID_SEL:
        {
          /* Grid select option - show label and right-aligned ON/OFF value */
          obdWriteString(&g_obd, 0, 0, yPos, (char *)getCarMenuOption(lang, 2), menuFont, isSelected ? OBD_WHITE : OBD_BLACK, 1);
          /* Show temp value when editing, stored value otherwise */
          uint16_t displayValue = isEditingRaceswp ? tempRaceswpValue : g_storedVar.gridCarSelectEnabled;
          sprintf(msgStr, "%3s", getOnOffLabel(lang, displayValue ? 1 : 0));
          /* Value is white when editing, follows selection otherwise */
          uint8_t valueColor = isEditingRaceswp ? OBD_WHITE : (isSelected ? OBD_WHITE : OBD_BLACK);
          obdWriteString(&g_obd, 0, OLED_WIDTH - 3 * charWidth, yPos, msgStr, menuFont, valueColor, 1);
          break;
        }

        case CAR_OPTION_COPY:
          obdWriteString(&g_obd, 0, 0, yPos, (char *)getCarMenuOption(lang, 3), menuFont, isSelected ? OBD_WHITE : OBD_BLACK, 1);
          break;

        case CAR_OPTION_RESET:
          obdWriteString(&g_obd, 0, 0, yPos, (char *)getCarMenuOption(lang, 4), menuFont, isSelected ? OBD_WHITE : OBD_BLACK, 1);
          break;

        case CAR_OPTION_BACK:
        {
          /* Display BACK option using helper for text case support */
          obdWriteString(&g_obd, 0, 0, yPos, (char *)getBackLabel(lang), menuFont, isSelected ? OBD_WHITE : OBD_BLACK, 1);
          break;
        }
      }
    }

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      break;
    }

    /* Long press = escape to main for race mode toggle */
    if (checkRaceModeEscape()) { requestEscapeToMain(); goto exitCarMenu; }
  }

  /* Handle encoder click based on current state */
  if (selectedOption == CAR_OPTION_GRID_SEL && !isEditingRaceswp) {
    /* First click on RACESWP: enter edit mode */
    isEditingRaceswp = true;
    g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
    setUiEncoderBoundaries(0, 1, false);  /* 0=OFF, 1=ON */
    resetUiEncoder(tempRaceswpValue);
    /* Wait for second click to confirm */
    while (!g_rotaryEncoder.isEncoderButtonClicked()) {
      /* Check for brake button to cancel editing */
      if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
        delay(BUTTON_SHORT_PRESS_DEBOUNCE_MS);
        /* Cancel editing - restore encoder for option selection */
        isEditingRaceswp = false;
        g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
        setUiEncoderBoundaries(0, 5, false);
        resetUiEncoder(selectedOption);
        obdFill(&g_obd, OBD_WHITE, 1);
        break;
      }

      tempRaceswpValue = g_rotaryEncoder.encoderChanged() ? readUiEncoder() : tempRaceswpValue;

      /* Redraw the RACESWP option with updated value */
      uint8_t optionScreenPos = CAR_OPTION_GRID_SEL - frameUpper;
      if (optionScreenPos < visibleLines) {
        /* Determine font size and dimensions based on setting */
        uint8_t menuFont = (g_storedVar.listFontSize == FONT_SIZE_SMALL) ? FONT_8x8 : FONT_12x16;
        uint8_t charWidth = (g_storedVar.listFontSize == FONT_SIZE_SMALL) ? WIDTH8x8 : WIDTH12x16;
        uint8_t lineHeight = (g_storedVar.listFontSize == FONT_SIZE_SMALL) ? 8 : HEIGHT12x16;
        uint8_t yPos = optionScreenPos * lineHeight;
        uint16_t lang = g_storedVar.language;

        obdWriteString(&g_obd, 0, 0, yPos, (char *)getCarMenuOption(lang, 2), menuFont, OBD_WHITE, 1);
        sprintf(msgStr, "%3s", getOnOffLabel(lang, tempRaceswpValue ? 1 : 0));
        obdWriteString(&g_obd, 0, OLED_WIDTH - 3 * charWidth, yPos, msgStr, menuFont, OBD_WHITE, 1);
      }
    }
    /* Save the confirmed value (only if we didn't cancel) */
    if (isEditingRaceswp) {
      g_storedVar.gridCarSelectEnabled = tempRaceswpValue;
      selectedOption = CAR_OPTION_GRID_SEL;  /* Mark as handled */
    }
  }

  /* If RENAME option was selected, go to renameCar routine */
  if (selectedOption == CAR_OPTION_RENAME)
  {
    showRenameCar();
    if (!isEscapeToMainRequested()) saveEEPROM(g_storedVar);
  }
  /* If SELECT option was selected, go to showCarSelection routine */
  else if (selectedOption == CAR_OPTION_SELECT)
  {
    showCarSelection();
    if (!isEscapeToMainRequested()) saveEEPROM(g_storedVar);
  }
  /* If RACESWP option was selected, value was already saved in the edit loop above */
  else if (selectedOption == CAR_OPTION_GRID_SEL)
  {
    saveEEPROM(g_storedVar);
  }
  /* If COPY option was selected, go to showCopyCarSettings routine */
  else if (selectedOption == CAR_OPTION_COPY)
  {
    showCopyCarSettings();
    if (!isEscapeToMainRequested()) saveEEPROM(g_storedVar);
  }
  /* If RESET option was selected, reset all car parameters with double confirmation */
  else if (selectedOption == CAR_OPTION_RESET)
  {
    uint16_t lang = g_storedVar.language;
    const char* allCarsLabel[] = {
      "ALLE BILER", "ALL CARS", "ALL CARS", "ALL CARS",
      "TODOS AUTOS", "ALLE AUTOS", "TUTTE AUTO"
    };
    const char* doneText[] = {
      "NULLSTILT!", "RESET DONE!", "RESET DONE!", "RESET DONE!",
      "RESET OK!", "RESET OK!", "RESET OK!"
    };
    const char* carLabel = allCarsLabel[lang];
    bool confirmed = showResetConfirmDialog(carLabel);
    if (confirmed) {
      resetAllCarsToFactoryDefaults();
      saveEEPROM(g_storedVar);
      initMenuItems();
      obdFill(&g_obd, OBD_WHITE, 1);
      int tw = strlen(doneText[lang]) * WIDTH12x16;
      obdWriteString(&g_obd, 0, (OLED_WIDTH - tw) / 2, 24, (char *)doneText[lang], FONT_12x16, OBD_BLACK, 1);
      delay(1500);
      obdFill(&g_obd, OBD_WHITE, 1);
    }
  }
  /* If BACK option was selected, simply exit without changes */
  /* Note: No action needed for BACK, we just fall through to the reset code below */

exitCarMenu:
  /* Reset encoder */
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(1, getMainMenuItemsCount(), false);
  resetUiEncoder(getMainMenuSelector());
  g_escVar.encoderPos = getMainMenuSelector();
  /* Clear screen */
  obdFill(&g_obd, OBD_WHITE, 1);

  return;
}


/**
 * Show the Rename car screen. Called by selecting RENAME option on the showSelectRenameCar screen.
 */
void showRenameCar() {

  uint16_t selectedChar = RENAME_CAR_MIN_ASCII; /* the selected char: starts from 33 (ASCII for !) to 122 (ASCII for z)*/
  uint16_t selectedOption = 0;                  /* The selected option, could be one of the char of the name (0 : CAR_NAME_MAX_SIZE - 2) or the confirm option (CAR_NAME_MAX_SIZE - 1)*/
                                                /* Remember that CAR_NAME_MAX_SIZE includes the last terminator char */
  char tmpName[CAR_NAME_MAX_SIZE];  /* Array of chars containing the temporary name */
  uint16_t mode = RENAME_CAR_SELECT_OPTION_MODE;  /* Initial mode. There are two mode:
                                                     - RENAME_CAR_SELECT_OPTION_MODE, when scrolling the encoder changes the selectedOption (pick which char to change, or the OK option)
                                                     - RENAME_CAR_SELECT_CHAR_MODE, when scrolling the encoder changes the selectedChar, that is the value of the selectedOption */
  sprintf(tmpName, "%s", g_storedVar.carParam[g_storedVar.selectedCarNumber].carName); /* Store the current carName in the temporary name */

  /* Clear screen */
  obdFill(&g_obd, OBD_WHITE, 1);

  /* Set encoder to selection parameter */
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, CAR_NAME_MAX_SIZE - 1, false);
  resetUiEncoder(selectedOption);

  /* Print "-RENAME THE CAR-"  and "-CLICK OK TO CONFIRM" */
  obdWriteString(&g_obd, 0, 16, 0, (char *)STR_RENAME_CAR[g_storedVar.language], FONT_6x8, OBD_WHITE, 1);
  obdWriteString(&g_obd, 0, 1, OLED_HEIGHT - HEIGHT8x8, (char *)STR_CONFIRM[g_storedVar.language], FONT_6x8, OBD_WHITE, 1);

  /* Draw the right arrow */
  for (uint8_t j = 0; j < 8; j++)
  {
    obdDrawLine(&g_obd, 80 + j, 16 + j, 80 + j, 30 - j, OBD_BLACK, 1);
  }
  obdDrawLine(&g_obd, 72, 22, 80, 22, OBD_BLACK, 1);
  obdDrawLine(&g_obd, 72, 23, 80, 23, OBD_BLACK, 1);
  obdDrawLine(&g_obd, 72, 24, 80, 24, OBD_BLACK, 1);

  /* Screensaver support */
  uint32_t lastInteraction = millis();
  bool screensaverActive = false;
  uint16_t screensaverEncoderPos = (uint16_t)readUiEncoder();

  /* Exit car renaming when encoder is clicked AND CONFIRM is selected */
  while (1)
  {
    bool wakeUpTriggered = refreshIdleInteractionFromControls(&lastInteraction, &screensaverActive, &screensaverEncoderPos);
    if (wakeUpTriggered) {
      obdFill(&g_obd, OBD_WHITE, 1);
      /* Redraw static elements */
      obdWriteString(&g_obd, 0, 16, 0, (char *)STR_RENAME_CAR[g_storedVar.language], FONT_6x8, OBD_WHITE, 1);
      obdWriteString(&g_obd, 0, 1, OLED_HEIGHT - HEIGHT8x8, (char *)STR_CONFIRM[g_storedVar.language], FONT_6x8, OBD_WHITE, 1);
      for (uint8_t j = 0; j < 8; j++) {
        obdDrawLine(&g_obd, 80 + j, 16 + j, 80 + j, 30 - j, OBD_BLACK, 1);
      }
      obdDrawLine(&g_obd, 72, 22, 80, 22, OBD_BLACK, 1);
      obdDrawLine(&g_obd, 72, 23, 80, 23, OBD_BLACK, 1);
      obdDrawLine(&g_obd, 72, 24, 80, 24, OBD_BLACK, 1);
    }

    /* Check for screensaver timeout */
    if (consumeScreensaverWakeInput(wakeUpTriggered)) { continue; }

    if (!wakeUpTriggered && g_storedVar.screensaverTimeout > 0 && millis() - lastInteraction > (g_storedVar.screensaverTimeout * 1000UL)) {
      if (g_escVar.trigger_norm == 0) {
        if (!screensaverActive) {
          screensaverActive = true;
          screensaverEncoderPos = readUiEncoder();
          showScreensaver();
        }
        if (serviceIdlePowerTransitions(&lastInteraction, &screensaverActive)) {
          obdFill(&g_obd, OBD_WHITE, 1);
        }
        delay(10);  /* Small delay to prevent watchdog timeout */
        continue;  /* Don't draw menu while screensaver is active */
      }
    }

    /* Check for brake button press - cancel and exit */
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      lastInteraction = millis();
      delay(BUTTON_SHORT_PRESS_DEBOUNCE_MS);
      /* Don't save changes - just exit */
      obdFill(&g_obd, OBD_WHITE, 1);
      return;
    }

    /* Get encoder value if changed */
    if (g_rotaryEncoder.encoderChanged()) {
      lastInteraction = millis();
      /* Change selectedOption if in RENAME_CAR_SELECT_OPTION_MODE */
      if (mode == RENAME_CAR_SELECT_OPTION_MODE) {
        selectedOption = readUiEncoder();
      }
      /* Change selectedChar if in RENAME_CAR_SELECT_CHAR_MODE */
      if (mode == RENAME_CAR_SELECT_CHAR_MODE) {
        selectedChar = readUiEncoder();
        tmpName[selectedOption] = (char)selectedChar; /* Change the value of the selected char in the temp name */

        /* Draw the upward and downward arrows on the selected char to indicate that it can be changed */
        for (uint8_t j = 0; j < 6; j++) {
          obdDrawLine(&g_obd, 1 + j + (selectedOption * 12), 14 - j, 11 - j + (selectedOption * 12), 14 - j, OBD_BLACK, 1);
          obdDrawLine(&g_obd, 1 + j + (selectedOption * 12), 33 + j, 11 - j + (selectedOption * 12), 33 + j, OBD_BLACK, 1);
        }
      }
    }

    /* Print each Char of the name, only the selected one is highlighted (WHITE color) */
    for (uint8_t i = 0; i < CAR_NAME_MAX_SIZE - 1; i++) 
    {
      sprintf(msgStr, "%c", tmpName[i]);
      obdWriteString(&g_obd, 0, 0 + (i * 12), 22, msgStr, FONT_12x16, (selectedOption == i) ? OBD_WHITE : OBD_BLACK, 1);
    }

    /* Print the confirm button */
    obdWriteString(&g_obd, 0, OLED_WIDTH - 24, 22, (char *)"OK", FONT_12x16, (selectedOption == CAR_NAME_MAX_SIZE - 1) ? OBD_WHITE : OBD_BLACK, 1);

    /* If encoder button is clicked */
    if (g_rotaryEncoder.isEncoderButtonClicked())
    {
      lastInteraction = millis();
      if (screensaverActive) {
        screensaverActive = false;
        obdFill(&g_obd, OBD_WHITE, 1);
        /* Redraw static elements */
        obdWriteString(&g_obd, 0, 16, 0, (char *)STR_RENAME_CAR[g_storedVar.language], FONT_6x8, OBD_WHITE, 1);
        obdWriteString(&g_obd, 0, 1, OLED_HEIGHT - HEIGHT8x8, (char *)STR_CONFIRM[g_storedVar.language], FONT_6x8, OBD_WHITE, 1);
        for (uint8_t j = 0; j < 8; j++) {
          obdDrawLine(&g_obd, 80 + j, 16 + j, 80 + j, 30 - j, OBD_BLACK, 1);
        }
        obdDrawLine(&g_obd, 72, 22, 80, 22, OBD_BLACK, 1);
        obdDrawLine(&g_obd, 72, 23, 80, 23, OBD_BLACK, 1);
        obdDrawLine(&g_obd, 72, 24, 80, 24, OBD_BLACK, 1);
        continue;
      }
      /* Exit renameCar routing when CONFIRM is selected */
      if (selectedOption == CAR_NAME_MAX_SIZE - 1)
      {
        /* Change the name of the Car */
        sprintf(g_storedVar.carParam[g_storedVar.selectedCarNumber].carName, "%s", tmpName);
        /* Menu variables are initialized in main loop */
        return;
      }

      /* If in RENAME_CAR_SELECT_OPTION_MODE */
      if (mode == RENAME_CAR_SELECT_OPTION_MODE) {
        /* switch mode */
        mode = RENAME_CAR_SELECT_CHAR_MODE;
        /* Reset encode */
        setUiEncoderBoundaries(RENAME_CAR_MIN_ASCII, RENAME_CAR_MAX_ASCII, false);
        resetUiEncoder((uint16_t)tmpName[selectedOption]);
        selectedChar = (uint16_t)tmpName[selectedOption];
      }
      /* If in RENAME_CAR_SELECT_CHAR_MODE */
      else {
        /* switch mode */
        mode = RENAME_CAR_SELECT_OPTION_MODE;
        /* Reset encode */
        setUiEncoderBoundaries(0, CAR_NAME_MAX_SIZE - 1, false);
        resetUiEncoder(selectedOption);
        /* Cancel the upward and downward arrows (draw them black) */
        for (uint8_t j = 0; j < 6; j++) {
          obdDrawLine(&g_obd, 1 + j + (selectedOption * 12), 14 - j, 11 - j + (selectedOption * 12), 14 - j, OBD_WHITE, 1);
          obdDrawLine(&g_obd, 1 + j + (selectedOption * 12), 33 + j, 11 - j + (selectedOption * 12), 33 + j, OBD_WHITE, 1);
        }
      }
    }

    /* Long press = escape to main for race mode toggle */
    if (checkRaceModeEscape()) { requestEscapeToMain(); return; }
  }
}


/**
 * Helpers for drawing the throttle-response graph used by CURVE and FADE.
 */
static uint16_t calcThrottleCurveVertexSpeedRaw(uint16_t minSpeedRaw, uint16_t maxSpeedRaw, uint16_t curveDiffPct) {
  return (uint16_t)(minSpeedRaw + (((uint32_t)maxSpeedRaw - (uint32_t)minSpeedRaw) * (uint32_t)curveDiffPct) / 100U);
}

static uint8_t graphYFromPercentX10(uint16_t pctX10) {
  return (uint8_t)map(pctX10, 0, 1000, 50, 0);
}

static uint8_t graphYFromSpeedRaw(uint16_t speedRaw) {
  return graphYFromPercentX10(sensiToPctX10(speedRaw));
}

static uint8_t graphXFromThrottleNorm(uint16_t throttleNorm) {
  uint16_t pct = (uint16_t)(((uint32_t)throttleNorm * 100U) / THROTTLE_NORMALIZED);
  return (uint8_t)(25U + min((uint16_t)100U, pct));
}

static void drawThrottleResponseGraph(uint16_t curveDiffValue, uint16_t fadePctValue, uint16_t triggerPct, const char* valueText) {
  uint16_t minSpeedRaw = g_storedVar.carParam[g_carSel].minSpeed;
  uint16_t maxSpeedRaw = g_storedVar.carParam[g_carSel].maxSpeed * SENSI_SCALE;
  uint16_t minSpeedY = graphYFromPercentX10(sensiToPctX10(minSpeedRaw));
  uint16_t maxSpeedY = graphYFromPercentX10((uint16_t)g_storedVar.carParam[g_carSel].maxSpeed * 10U);
  uint16_t fadeThrottleNorm = fadePctToThrottleNorm(min((uint16_t)FADE_MAX_VALUE, fadePctValue));
  uint16_t curveVertexInputNorm = curveVertexInputWithFade(fadeThrottleNorm, g_storedVar.carParam[g_carSel].throttleCurveVertex.inputThrottle);
  uint16_t curveVertexSpeedRaw = calcThrottleCurveVertexSpeedRaw(minSpeedRaw, maxSpeedRaw, curveDiffValue);
  uint8_t fadeX = graphXFromThrottleNorm(fadeThrottleNorm);
  uint8_t curveX = graphXFromThrottleNorm(curveVertexInputNorm);
  uint8_t curveY = graphYFromSpeedRaw(curveVertexSpeedRaw);

  obdFill(&g_obd, OBD_WHITE, 1);
  obdDrawLine(&g_obd, 25, 0, 25, 50, OBD_BLACK, 1);
  obdDrawLine(&g_obd, 25, 50, 125, 50, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 0, (char *)"100%", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 58, (char *)"  0%", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 104, 58, (char *)"100%", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, (minSpeedY > 42 ? 42 : minSpeedY), (char *)"MIN", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 28, maxSpeedY, (char *)"MAX", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 64, 58, (char *)"50%", FONT_6x8, OBD_BLACK, 1);

  obdSetPixel(&g_obd, 24, minSpeedY, OBD_BLACK, 1);
  obdSetPixel(&g_obd, 23, minSpeedY, OBD_BLACK, 1);
  obdSetPixel(&g_obd, 75, 51, OBD_BLACK, 1);
  obdSetPixel(&g_obd, 75, 52, OBD_BLACK, 1);
  obdSetPixel(&g_obd, 26, maxSpeedY, OBD_BLACK, 1);
  obdSetPixel(&g_obd, 27, maxSpeedY, OBD_BLACK, 1);
  obdSetPixel(&g_obd, curveX, 51, OBD_BLACK, 1);
  obdSetPixel(&g_obd, curveX, 52, OBD_BLACK, 1);

  if (fadePctValue > 0) {
    obdSetPixel(&g_obd, fadeX, 51, OBD_BLACK, 1);
    obdSetPixel(&g_obd, fadeX, 52, OBD_BLACK, 1);
    if (fadePctValue >= 5 && fadeX > 28 && fadeX < 120) {
      obdWriteString(&g_obd, 0, fadeX - 3, 58, (char *)"F", FONT_6x8, OBD_BLACK, 1);
    }
  }

  if (fadePctValue > 0) {
    obdDrawLine(&g_obd, 25, 50, fadeX, minSpeedY, OBD_BLACK, 1);
    if (curveX > fadeX) {
      obdDrawLine(&g_obd, fadeX, minSpeedY, curveX, curveY, OBD_BLACK, 1);
    }
  } else {
    obdDrawLine(&g_obd, 25, minSpeedY, curveX, curveY, OBD_BLACK, 1);
  }
  obdDrawLine(&g_obd, curveX, curveY, 125, maxSpeedY, OBD_BLACK, 1);

  obdWriteString(&g_obd, 0, OLED_WIDTH - 48, 34, (char *)valueText, FONT_12x16, OBD_BLACK, 1);
  snprintf(msgStr, sizeof(msgStr), "%3d%%", triggerPct);
  obdWriteString(&g_obd, 0, OLED_WIDTH - 32, 26, msgStr, FONT_8x8, OBD_BLACK, 1);
}

/**
 * Show the Throttle Curve selection screen. Shwon when the CURVE item is selected.
 * Draws the throttle curve graph as the actual live response, including FADE when enabled.
 * The CURVE parameter and the current trigger value are also displayed.
 */
void showCurveSelection()
{
  uint16_t prevTrigger = g_escVar.outputSpeed_pct;
  uint16_t originalCurveValue = g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff;
  bool curveCanceled = false;

  g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
  setUiEncoderBoundaries(THROTTLE_CURVE_SPEED_DIFF_MIN_VALUE, THROTTLE_CURVE_SPEED_DIFF_MAX_VALUE, false);
  resetUiEncoder(g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff);
  snprintf(msgStr, sizeof(msgStr), "%3d%%", g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff);
  drawThrottleResponseGraph(g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff,
                            g_storedVar.carParam[g_carSel].fade,
                            prevTrigger,
                            msgStr);

  /* Exit curve function when encoder is clicked */
  while (!g_rotaryEncoder.isEncoderButtonClicked() && !curveCanceled)
  {
    /* Check for brake button press - acts as "cancel" in curve screen */
    static bool brakeBtnInCurve = false;
    static uint32_t lastBrakeBtnCurveTime = 0;
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      if (!brakeBtnInCurve && millis() - lastBrakeBtnCurveTime > BUTTON_SHORT_PRESS_DEBOUNCE_MS) {
        brakeBtnInCurve = true;
        lastBrakeBtnCurveTime = millis();
        /* Cancel changes - restore original value */
        g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff = originalCurveValue;
        curveCanceled = true;
      }
    } else {
      brakeBtnInCurve = false;
    }

    /* Write the trigger value only if it changed */
    if (g_escVar.outputSpeed_pct != prevTrigger)
    {
      prevTrigger = g_escVar.outputSpeed_pct;
      snprintf(msgStr, sizeof(msgStr), "%3d%%", g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff);
      drawThrottleResponseGraph(g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff,
                                g_storedVar.carParam[g_carSel].fade,
                                prevTrigger,
                                msgStr);
    }

    /* Get encoder position if it was changed and redraw the graph */
    if (g_rotaryEncoder.encoderChanged())
    {
      g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff = readUiEncoder();
      snprintf(msgStr, sizeof(msgStr), "%3d%%", g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff);
      drawThrottleResponseGraph(g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff,
                                g_storedVar.carParam[g_carSel].fade,
                                prevTrigger,
                                msgStr);
    }
    else
    {
      /* Service the watchdog, to prevent CPU reset */
      vTaskDelay(10);
    }

    /* Long press = escape to main for race mode toggle */
    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }
  }

  /* Reset encoder */
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(1, getMainMenuItemsCount(), false);
  resetUiEncoder(getMainMenuSelector());
  g_escVar.encoderPos = getMainMenuSelector();

  /* Only save if not canceled */
  if (!curveCanceled && !isEscapeToMainRequested()) {
    saveEEPROM(g_storedVar);          /* Save modified values to EEPROM */
  }

  obdFill(&g_obd, OBD_WHITE, 1);    /* Clear screen */

  return;
}

/**
 * Show the FADE selection screen. FADE adds a soft 0->SENSI ramp before the normal curve starts.
 */
void showFadeSelection()
{
  uint16_t prevTrigger = g_escVar.outputSpeed_pct;
  uint16_t originalFadeValue = g_storedVar.carParam[g_carSel].fade;
  bool fadeCanceled = false;

  g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);
  setUiEncoderBoundaries(0, FADE_MAX_VALUE, false);
  resetUiEncoder(g_storedVar.carParam[g_carSel].fade);
  snprintf(msgStr, sizeof(msgStr), "%3d%%", g_storedVar.carParam[g_carSel].fade);
  drawThrottleResponseGraph(g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff,
                            g_storedVar.carParam[g_carSel].fade,
                            prevTrigger,
                            msgStr);

  while (!g_rotaryEncoder.isEncoderButtonClicked() && !fadeCanceled)
  {
    static bool brakeBtnInFade = false;
    static uint32_t lastBrakeBtnFadeTime = 0;
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      if (!brakeBtnInFade && millis() - lastBrakeBtnFadeTime > BUTTON_SHORT_PRESS_DEBOUNCE_MS) {
        brakeBtnInFade = true;
        lastBrakeBtnFadeTime = millis();
        g_storedVar.carParam[g_carSel].fade = originalFadeValue;
        fadeCanceled = true;
      }
    } else {
      brakeBtnInFade = false;
    }

    if (g_escVar.outputSpeed_pct != prevTrigger)
    {
      prevTrigger = g_escVar.outputSpeed_pct;
      snprintf(msgStr, sizeof(msgStr), "%3d%%", g_storedVar.carParam[g_carSel].fade);
      drawThrottleResponseGraph(g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff,
                                g_storedVar.carParam[g_carSel].fade,
                                prevTrigger,
                                msgStr);
    }

    if (g_rotaryEncoder.encoderChanged())
    {
      g_storedVar.carParam[g_carSel].fade = readUiEncoder();
      snprintf(msgStr, sizeof(msgStr), "%3d%%", g_storedVar.carParam[g_carSel].fade);
      drawThrottleResponseGraph(g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff,
                                g_storedVar.carParam[g_carSel].fade,
                                prevTrigger,
                                msgStr);
    }
    else
    {
      vTaskDelay(10);
    }

    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }
  }

  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(1, getMainMenuItemsCount(), false);
  resetUiEncoder(getMainMenuSelector());
  g_escVar.encoderPos = getMainMenuSelector();

  if (!fadeCanceled && !isEscapeToMainRequested()) {
    saveEEPROM(g_storedVar);
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}
