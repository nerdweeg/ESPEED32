#include <Arduino.h>
#include "HAL.h"
#include "ext_pot.h"
#include "ui_render.h"
#include "ui_strings.h"
#include "ui_text_access.h"
#include "connectivity_portal.h"
#include "app_globals.h"
#include "app_init.h"
#include "power_ui.h"

static void formatExtPotLabel(char* out, size_t outSize, int8_t potIndex) {
  if (!out || outSize == 0) return;
  if (potIndex >= 0) snprintf(out, outSize, "POT%d", (int)potIndex + 1);
  else snprintf(out, outSize, "POT");
}

static void formatTunedPctX10Value(char* out, size_t outSize, uint16_t pctX10, uint16_t stepRaw) {
  if (!out || outSize == 0) return;
  pctX10 = min(pctX10, (uint16_t)1000U);
  if ((stepRaw % 10U) == 0 && (pctX10 % 10U) == 0) {
    snprintf(out, outSize, "%u%%", (unsigned int)(pctX10 / 10U));
  } else {
    snprintf(out, outSize, "%u.%u%%", (unsigned int)(pctX10 / 10U), (unsigned int)(pctX10 % 10U));
  }
}

static void formatBrakeValue(char* out, size_t outSize, uint16_t brakeRaw) {
  formatTunedPctX10Value(out, outSize, brakeRaw, constrain(g_brakeStep, BRAKE_STEP_MIN, BRAKE_STEP_MAX));
}

static void formatSensiValue(char* out, size_t outSize, uint16_t sensiRaw) {
  formatTunedPctX10Value(out, outSize, sensiRaw, constrain(g_sensiStep, SENSI_STEP_MIN, SENSI_STEP_MAX));
}

static uint8_t getFontCharWidth(uint8_t font) {
  return (font == FONT_12x16) ? WIDTH12x16 : WIDTH8x8;
}

static void writeBlankField(int x, int y, uint8_t font, uint8_t clearChars) {
  if (clearChars == 0U) return;

  char blank[17];
  uint8_t count = min(clearChars, (uint8_t)(sizeof(blank) - 1U));
  for (uint8_t i = 0; i < count; i++) blank[i] = ' ';
  blank[count] = '\0';

  obdWriteString(&g_obd, 0, x, y, blank, font, OBD_BLACK, 1);
}

static void clearCenteredField(int centerX, int y, uint8_t font, uint8_t clearChars) {
  uint8_t fieldWidth = clearChars * getFontCharWidth(font);
  writeBlankField(centerX - (fieldWidth / 2), y, font, clearChars);
}

static void clearRightAlignedField(int rightX, int y, uint8_t font, uint8_t clearChars) {
  uint8_t fieldWidth = clearChars * getFontCharWidth(font);
  writeBlankField(rightX - fieldWidth, y, font, clearChars);
}

static void formatActiveBrakeStatus(char* out, size_t outSize, uint8_t activeBrakeKind, uint8_t activeBrakePct) {
  if (!out || outSize == 0) return;

  activeBrakePct = (uint8_t)constrain((int)activeBrakePct, 0, 100);

  switch (activeBrakeKind) {
    case ACTIVE_BRAKE_BASE:
      snprintf(out, outSize, "B%03u%%", (unsigned int)activeBrakePct);
      break;
    case ACTIVE_BRAKE_ALT:
      snprintf(out, outSize, "A%03u%%", (unsigned int)activeBrakePct);
      break;
    case ACTIVE_BRAKE_QUICK:
      snprintf(out, outSize, "Q%03u%%", (unsigned int)activeBrakePct);
      break;
    case ACTIVE_BRAKE_DRAG:
      snprintf(out, outSize, "D%03u%%", (unsigned int)activeBrakePct);
      break;
    default:
      snprintf(out, outSize, "NONE ");
      break;
  }
}

static void formatAntiSpinValue(char* out, size_t outSize, uint16_t antiSpinMs) {
  if (!out || outSize == 0) return;

  switch (g_antiSpinDisplayMode) {
    case ANTISPIN_UI_MODE_PERCENT:
      snprintf(out, outSize, "%u%%", (unsigned int)antiSpinMsToPercent(antiSpinMs));
      break;
    case ANTISPIN_UI_MODE_TEXT:
      snprintf(out, outSize, "%s", antiSpinTextLevelToLabel(antiSpinMsToTextLevel(antiSpinMs)));
      break;
    case ANTISPIN_UI_MODE_MS:
    default:
      snprintf(out, outSize, "%ums", (unsigned int)antiSpinMs);
      break;
  }
}

/**
 * @brief Display bottom status line with throttle, car name and voltage
 * @details Common function used by both main menu and screensaver
 */
void displayStatusLine() {
  const uint8_t Y = 3 * HEIGHT12x16 + HEIGHT8x8;  /* y = 56 (bottom status row) */

  /* Fixed column x-positions for each slot (4 × 32px = 128px total).
   * Each slot renders 5 chars (30px) left-aligned in its 32px column.
   * No preliminary erase: writing 5 fixed-width chars directly over the previous 5 chars
   * eliminates the blank-flash that caused flicker. Blank slots write 5 spaces to clear. */
  static const uint8_t SLOT_X[STATUS_SLOTS] = {0, 32, 64, 96};

  /* Determine if CAR is selected in grid mode (for car name highlight) */
  bool carSelected = false;
  if (g_storedVar.viewMode == VIEW_MODE_GRID && g_storedVar.gridCarSelectEnabled) {
    if (g_storedVar.raceViewMode == RACE_VIEW_SIMPLE) {
      carSelected = (g_encoderMainSelector == 3);
    } else {
      carSelected = (g_encoderMainSelector == 5);
    }
  }

  char buf[7];  /* 5 chars + null */
  int8_t lockSlot = -1;
  int8_t wifiSlot = -1;

  /* Settings lock indicator: find a blank slot, or steal slot 0 */
  if (isSettingsLocked()) {
    for (uint8_t s = 0; s < STATUS_SLOTS; s++) {
      if (normalizeStatusSlotValue(g_storedVar.statusSlot[s]) == STATUS_BLANK) {
        lockSlot = (int8_t)s;
        break;
      }
    }
    if (lockSlot < 0) lockSlot = 0;
  }

  if (isWiFiPortalActive()) {
    for (uint8_t s = 0; s < STATUS_SLOTS; s++) {
      if ((int8_t)s == lockSlot) continue;  /* Don't double-assign */
      if (normalizeStatusSlotValue(g_storedVar.statusSlot[s]) == STATUS_BLANK) {
        wifiSlot = (int8_t)s;
        break;
      }
    }

    /* If all slots are occupied, temporarily override slot 4 with WIFI status. */
    if (wifiSlot < 0) {
      wifiSlot = (lockSlot == STATUS_SLOTS - 1) ? STATUS_SLOTS - 2 : STATUS_SLOTS - 1;
    }
  }

  for (uint8_t s = 0; s < STATUS_SLOTS; s++) {
    uint16_t slot = normalizeStatusSlotValue(g_storedVar.statusSlot[s]);
    uint8_t color = OBD_BLACK;

    if (lockSlot == (int8_t)s) {
      strcpy(buf, "LOCK ");
      obdWriteString(&g_obd, 0, SLOT_X[s], Y, buf, FONT_6x8, OBD_WHITE, 1);
      continue;
    }

    if (wifiSlot == (int8_t)s) {
      strcpy(buf, "WIFI ");
      obdWriteString(&g_obd, 0, SLOT_X[s], Y, buf, FONT_6x8, color, 1);
      continue;
    }

    switch (slot) {
      case STATUS_OUTPUT:
        if (digitalRead(BUTT_PIN) == BUTTON_PRESSED && g_escVar.trigger_norm == 0) {
          snprintf(buf, sizeof(buf), "B%3d%%", g_storedVar.carParam[g_carSel].brakeButtonReduction);
        } else {
          snprintf(buf, sizeof(buf), "%3d%%O", g_escVar.outputSpeed_pct);
        }
        color = (g_escVar.outputSpeed_pct == 100) ? OBD_WHITE : OBD_BLACK;
        break;
      case STATUS_THROTTLE: {
        uint8_t tpct = (uint8_t)((g_escVar.trigger_norm * 100UL) / THROTTLE_NORMALIZED);
        snprintf(buf, sizeof(buf), "%3d%%T", tpct);
        break;
      }
      case STATUS_CAR:
        snprintf(buf, 6, "%-5s", g_storedVar.carParam[g_carSel].carName);
        color = carSelected ? OBD_WHITE : OBD_BLACK;
        break;
      case STATUS_CURRENT:
      case STATUS_CURRENT_MA: {
        if (HAL_HasMotorCurrentSense()) {
          uint16_t mA = g_escVar.motorCurrent_mA;
          if (mA < 1000U) {
            snprintf(buf, sizeof(buf), "%3umA", (unsigned)mA);
          } else {
            snprintf(buf, sizeof(buf), "%2u.%1uA", (unsigned)(mA / 1000U), (unsigned)((mA % 1000U) / 100U));
          }
        } else {
          strcpy(buf, " N/A ");
        }
        break;
      }
      case STATUS_VOLTAGE: {
        uint16_t v = g_escVar.Vin_mV;
        snprintf(buf, sizeof(buf), "%2d.%01dV", v / 1000, (v % 1000) / 100);
        break;
      }
      case STATUS_BATTERY: {
        uint16_t v = g_escVar.Bat_mV;
        if (v < 1000U) {
          strcpy(buf, " N/A ");
        } else {
          if (v > 9999U) v = 9999U;
          snprintf(buf, sizeof(buf), "%1u.%1uVB", (unsigned)(v / 1000U), (unsigned)((v % 1000U) / 100U));
        }
        break;
      }
      case STATUS_ACTIVE_BRAKE:
        formatActiveBrakeStatus(buf, sizeof(buf), g_escVar.activeBrakeKind, g_escVar.activeBrake_pct);
        break;
      case STATUS_DEVICE_ID: {
        char deviceId[5];
        getDeviceIdSuffix(deviceId, sizeof(deviceId));
        snprintf(buf, 6, "#%-4s", deviceId);
        break;
      }
      default:  /* STATUS_BLANK */
        strcpy(buf, "     ");
        break;
    }

    obdWriteString(&g_obd, 0, SLOT_X[s], Y, buf, FONT_6x8, color, 1);
  }
}

/* Parameter shown in a race view slot (FULL uses slots 0-3, SIMPLE slots 0-1). */
static uint8_t getRaceSlotParam(uint8_t slot) {
  return normalizeRaceSlotParam(g_storedVar.raceSlot[slot], slot);
}

/**
 * Format the value of a race view parameter for the current car.
 * BRAKE and SENSI show the external pot label instead when a pot drives them and the slot is not
 * being edited.
 * @return true when the pot label is shown
 */
static bool formatRaceParamValue(uint8_t param, bool editingThisSlot, char* out, size_t outSize) {
  const CarParam_type& car = g_storedVar.carParam[g_carSel];
  out[0] = '\0';
  switch (param) {
    case RACE_PARAM_BRAKE:
      if (isExtPotBrakeTarget() && !editingThisSlot) {
        formatExtPotLabel(out, outSize, getExtPotIndexForTarget(EXT_POT_TARGET_BRAKE));
        return true;
      }
      formatBrakeValue(out, outSize, getEffectiveBrakeRaw());
      return false;
    case RACE_PARAM_SENSI:
      if (isExtPotSensiTarget() && !editingThisSlot) {
        formatExtPotLabel(out, outSize, getExtPotIndexForTarget(EXT_POT_TARGET_SENSI));
        return true;
      }
      formatSensiValue(out, outSize, getEffectiveSensiRaw());
      return false;
    case RACE_PARAM_ANTIS:
      formatAntiSpinValue(out, outSize, car.antiSpin);
      return false;
    case RACE_PARAM_CURVE:
      snprintf(out, outSize, "%3d%%", car.throttleCurveVertex.curveSpeedDiff);
      return false;
    case RACE_PARAM_FADE:
      snprintf(out, outSize, "%3d%%", car.fade);
      return false;
    case RACE_PARAM_PWM_F:
      snprintf(out, outSize, "%u.%01uk", (unsigned)(car.freqPWM / 10U), (unsigned)(car.freqPWM % 10U));
      return false;
    case RACE_PARAM_LIMIT: {
      uint16_t shown = isLimitShownAsChoke(g_storedVar.terminology) ? limitPctToChokePct(car.maxSpeed) : car.maxSpeed;
      snprintf(out, outSize, "%3d%%", shown);
      return false;
    }
    default:
      return false;
  }
}

/**
 * @brief Display simple race mode - the first two race slots in a larger font
 * @param selectedItem Item selected for editing (0-1 = slots, 2 = CAR if enabled)
 * @param isEditing True if currently editing a value
 */
void displayRaceModeSimple(uint8_t selectedItem, bool isEditing) {
  static char lastText[2][8];
  static uint8_t lastSelectedItem = 255;
  static bool lastIsEditing = false;
  static uint16_t lastCarSel = 999;

  bool force = g_forceRaceRedraw || selectedItem != lastSelectedItem || isEditing != lastIsEditing || g_carSel != lastCarSel;
  if (force) {
    lastSelectedItem = selectedItem;
    lastIsEditing = isEditing;
    lastCarSel = g_carSel;
  }

  for (uint8_t slot = 0; slot < 2; slot++) {
    uint8_t param = getRaceSlotParam(slot);
    char text[8];
    bool usesPot = formatRaceParamValue(param, isEditing && selectedItem == slot, text, sizeof(text));
    if (!force && strcmp(text, lastText[slot]) == 0) continue;

    int center = (slot == 0) ? 32 : 96;  /* centre of the left / right half */
    uint8_t color = (selectedItem == slot) ? OBD_WHITE : OBD_BLACK;

    /* Label in FONT_12x16: 5 chars x 12px = 60px wide */
    const char* label = getRaceLabel(g_storedVar.language, param);
    obdWriteString(&g_obd, 0, center - ((strlen(label) * WIDTH12x16) / 2), 0, (char *)label, FONT_12x16, color, 1);

    clearCenteredField(center, 16, FONT_12x16, 6);
    /* BRAKE values can be 6 chars ("100.0%"), so they always use the small font; others use the
       large font when they fit in the 64px column. */
    bool smallValue = (param == RACE_PARAM_BRAKE && !usesPot) || (strlen(text) * WIDTH12x16 > 64U);
    if (smallValue) {
      obdWriteString(&g_obd, 0, center - ((strlen(text) * WIDTH8x8) / 2), 20, text, FONT_8x8, color, 1);
    } else {
      obdWriteString(&g_obd, 0, center - ((strlen(text) * WIDTH12x16) / 2), 16, text, FONT_12x16, color, 1);
    }
    snprintf(lastText[slot], sizeof(lastText[slot]), "%s", text);
  }

  /* Note: Car name, voltage, and LIMITER warning are displayed by displayStatusLine() */
}

/**
 * @brief Display race mode - compact grid with the four race slots
 * @param selectedItem Item selected for editing (0-3 = slots, 4 = CAR if enabled, 255 = none)
 * @param isEditing True if currently editing a value
 */
void displayRaceMode(uint8_t selectedItem, bool isEditing) {
  static char lastText[RACE_SLOTS][8];
  static uint8_t lastSelectedItem = 255;
  static bool lastIsEditing = false;
  static uint16_t lastCarSel = 999;

  bool force = g_forceRaceRedraw || selectedItem != lastSelectedItem || isEditing != lastIsEditing || g_carSel != lastCarSel;
  if (force) {
    lastSelectedItem = selectedItem;
    lastIsEditing = isEditing;
    lastCarSel = g_carSel;
  }

  /* Layout: slots 0/1 on the top row, 2/3 below; label above value, centred in each half */
  for (uint8_t slot = 0; slot < RACE_SLOTS; slot++) {
    uint8_t param = getRaceSlotParam(slot);
    char text[8];
    formatRaceParamValue(param, isEditing && selectedItem == slot, text, sizeof(text));
    if (!force && strcmp(text, lastText[slot]) == 0) continue;

    bool rightColumn = (slot & 1U) != 0U;
    int center = rightColumn ? 96 : 32;
    int labelY = (slot < 2U) ? 2 : 24;
    uint8_t color = (selectedItem == slot) ? OBD_WHITE : OBD_BLACK;

    /* Right-column labels sit 1px right, as before */
    const char* label = getRaceLabel(g_storedVar.language, param);
    obdWriteString(&g_obd, 0, center - ((strlen(label) * 6) / 2) + (rightColumn ? 1 : 0), labelY, (char *)label, FONT_6x8, color, 1);

    clearCenteredField(center, labelY + 10, FONT_8x8, 6);
    obdWriteString(&g_obd, 0, center - ((strlen(text) * WIDTH8x8) / 2), labelY + 10, text, FONT_8x8, color, 1);
    snprintf(lastText[slot], sizeof(lastText[slot]), "%s", text);
  }

  /* Note: Car name, voltage, and LIMITER warning are displayed by displayStatusLine() */
  /* CAR selection (item 4) uses the existing car name display in status line when enabled */
}

/**
 * @brief Show screensaver with branding
 * @details Displays personalized branding only (full screen, no status line).
 *          Text is configurable on-device and via web UI.
 */
void showScreensaver() {
  /* Clear screen */
  obdFill(&g_obd, OBD_WHITE, 1);

  /* Calculate text width for centering (16 pixels per character for FONT_16x32) */
  int line1_width = strlen(g_storedVar.screensaverLine1) * 16;
  int line1_x = (OLED_WIDTH - line1_width) / 2;

  /* Display main text in extra large font centered */
  obdWriteString(&g_obd, 0, line1_x, 8, g_storedVar.screensaverLine1, FONT_16x32, OBD_BLACK, 1);

  /* Calculate text width for centering (6 pixels per character for FONT_6x8) */
  int line2_width = strlen(g_storedVar.screensaverLine2) * 6;
  int line2_x = (OLED_WIDTH - line2_width) / 2;

  /* Display subtitle in smaller font centered below */
  obdWriteString(&g_obd, 0, line2_x, 34, g_storedVar.screensaverLine2, FONT_6x8, OBD_BLACK, 1);

}

/**
 * Print the main menu.
 * Takes care of printing the menu items and to scroll them according to the encoder position.
 * The current menu state is used to highlight decide whether to highlight only the item or also the value.
 * 
 * @param currMenuState The current menu state (ITEM_SELECTION or VALUE_SELECTION)
 */
void printMainMenu(MenuState_enum currMenuState) 
{
  if (isOtaInProgress()) {
    return;
  }

  static uint16_t tmp = 0;
  static bool screensaverActive = false;
  uint8_t mainMenuItems = getMainMenuItemsCount();

  /* "Frame" indicates which items are currently displayed.
     It consist of a lower and upper bound: only the items within this boundaries are displayed.
     The difference between upper and lower bound is fixed to be visibleLines
     It's important that the encoder boundaries matches the menu items (e.g., 8 items, encoder boundaries must be [1,8]) */
  static uint16_t frameUpper = 1;
  static uint16_t frameLower = 3;
  static uint8_t lastVisibleLines = 3;
  uint8_t visibleLines = min(getMenuLines(), mainMenuItems);

  if (visibleLines < 1) visibleLines = 1;
  if (g_encoderMainSelector < 1) g_encoderMainSelector = 1;
  if (g_encoderMainSelector > mainMenuItems) g_encoderMainSelector = mainMenuItems;

  /* If font size changed, reset frame */
  if (visibleLines != lastVisibleLines) {
    frameUpper = 1;
    frameLower = visibleLines;
    lastVisibleLines = visibleLines;
    obdFill(&g_obd, OBD_WHITE, 1);
  }

  /* In encoder move out of frame, adjust frame */
  if (g_encoderMainSelector > frameLower)
  {
    frameLower = g_encoderMainSelector;
    frameUpper = frameLower - visibleLines + 1;
    obdFill(&g_obd, OBD_WHITE, 1);
    screensaverActive = false;
  }
  else if (g_encoderMainSelector < frameUpper)
  {
    frameUpper = g_encoderMainSelector;
    frameLower = frameUpper + visibleLines - 1;
    obdFill(&g_obd, OBD_WHITE, 1);
    screensaverActive = false;
  }

  if (frameLower > mainMenuItems) {
    frameLower = mainMenuItems;
    frameUpper = (frameLower >= visibleLines) ? (frameLower - visibleLines + 1) : 1;
  }

  /* Check what to display based on view mode */
  static uint16_t lastViewMode = 255;  /* Track view mode changes */

  if (g_storedVar.viewMode == VIEW_MODE_GRID)
  {
    /* GRID mode: always show race mode with editing capability */
    /* Grid items depend on race view mode:
       FULL: 0=BRAKE, 1=SENSI, 2=ANTIS, 3=CURVE, 4=CAR (if enabled)
       SIMPLE: 0=BRAKE, 1=SENSI, 2=CAR (if enabled) */
    static uint8_t gridSelectedItem = 0;  /* Currently selected grid item */
    static MenuState_enum gridMenuState = ITEM_SELECTION;  /* Grid edit state */

    /* Force redraw when entering GRID mode */
    if (lastViewMode != VIEW_MODE_GRID) {
      lastViewMode = VIEW_MODE_GRID;
      g_forceRaceRedraw = true;
      obdFill(&g_obd, OBD_WHITE, 1);
    }

    /* Clear screen if coming from screensaver */
    if (screensaverActive) {
      obdFill(&g_obd, OBD_WHITE, 1);
      screensaverActive = false;
    }

    /* Calculate max grid item based on race view mode */
    uint8_t maxGridItem;
    if (g_storedVar.raceViewMode == RACE_VIEW_SIMPLE) {
      /* SIMPLE: BRAKE, SENSI, CAR (if enabled) */
      maxGridItem = g_storedVar.gridCarSelectEnabled ? 3 : 2;
    }
    else {
      /* FULL: BRAKE, SENSI, ANTIS, CURVE, CAR (if enabled) */
      maxGridItem = g_storedVar.gridCarSelectEnabled ? 5 : 4;
    }

    if (g_encoderMainSelector < 1) g_encoderMainSelector = 1;
    if (g_encoderMainSelector > maxGridItem) g_encoderMainSelector = maxGridItem;
    gridSelectedItem = g_encoderMainSelector - 1;

    /* Determine if we're editing based on menu state */
    bool isEditing = (currMenuState == VALUE_SELECTION);

    /* Call appropriate display function based on race view mode */
    if (g_storedVar.raceViewMode == RACE_VIEW_SIMPLE) {
      displayRaceModeSimple(gridSelectedItem, isEditing);
    }
    else {
      displayRaceMode(gridSelectedItem, isEditing);
    }
    g_forceRaceRedraw = false;  /* Reset force redraw flag after display */

    /* Print LIMITER warning if LIMIT is any value other than 100% */
    if (g_storedVar.carParam[g_carSel].maxSpeed < MAX_SPEED_DEFAULT)
    {
      obdWriteString(&g_obd, 0, WIDTH8x8, 3 * HEIGHT12x16, (char *)getLimiterLabel(g_storedVar.language), FONT_8x8, OBD_WHITE, 1);
    }
    else
    {
      obdWriteString(&g_obd, 0, WIDTH8x8, 3 * HEIGHT12x16, (char *)"             ", FONT_8x8, OBD_BLACK, 1);
    }
  }
  else {
  /* LIST mode: show screensaver or menu */
  lastViewMode = VIEW_MODE_LIST;  /* Track that we're in LIST mode */

  /* Track encoder position for screensaver wake-up */
  static uint16_t screensaverEncoderPos = 0;

  bool wakeUpTriggered = refreshIdleInteractionFromControls(&g_lastEncoderInteraction, &screensaverActive, &screensaverEncoderPos);
  if (wakeUpTriggered) {
    obdFill(&g_obd, OBD_WHITE, 1);
  }

  if (g_storedVar.screensaverTimeout > 0 && millis() - g_lastEncoderInteraction > (g_storedVar.screensaverTimeout * 1000UL))
  {
    /* Timeout reached with no active controls - show screensaver */
    if (!screensaverActive && g_escVar.trigger_norm == 0) {
      screensaverActive = true;
      screensaverEncoderPos = readUiEncoder();  /* Save position when entering screensaver */
      showScreensaver();
    }
    /* Auto power save: triggers after screensaverTimeout + powerSaveTimeout min of inactivity.
     * Uses g_lastEncoderInteraction so encoder noise cannot reset the timer. */
    if (g_storedVar.powerSaveTimeout > 0 &&
        millis() - g_lastEncoderInteraction > (g_storedVar.screensaverTimeout * 1000UL) + ((uint32_t)g_storedVar.powerSaveTimeout * 60000UL)) {
      showPowerSave(g_lastEncoderInteraction);
      g_lastEncoderInteraction = millis();  /* Restart timers after waking from soft sleep */
      screensaverActive = false;
    }
    /* Auto deep sleep: triggers after screensaverTimeout + deepSleepTimeout of inactivity. */
    if (g_storedVar.deepSleepTimeout > 0 &&
        millis() - g_lastEncoderInteraction > (g_storedVar.screensaverTimeout * 1000UL) + ((uint32_t)g_storedVar.deepSleepTimeout * 60000UL)) {
      showDeepSleep();  /* Never returns */
    }
    /* Screensaver is active - don't draw menu.
     * Yield to FreeRTOS so the IDLE task can feed the watchdog timer. */
    vTaskDelay(10);
  }
  else if (!screensaverActive)
  {
    /* Not in screensaver - show normal menu */
    /* Determine font size and dimensions based on setting */
    uint8_t menuFont = (g_storedVar.listFontSize == FONT_SIZE_SMALL) ? FONT_8x8 : FONT_12x16;
    uint8_t charWidth = (g_storedVar.listFontSize == FONT_SIZE_SMALL) ? WIDTH8x8 : WIDTH12x16;
    uint8_t lineHeight = (g_storedVar.listFontSize == FONT_SIZE_SMALL) ? 8 : HEIGHT12x16;
    static char lastBrakeListValue[10] = "";
    static char lastSensiListValue[10] = "";
    static char lastAntiSpinListValue[10] = "";
    static char lastDecimalListValue[10] = "";
    static uint8_t lastBrakeListFont = 0xFF;
    static uint8_t lastSensiListFont = 0xFF;
    static uint8_t lastAntiSpinListFont = 0xFF;
    static uint8_t lastDecimalListFont = 0xFF;

    for (uint8_t i = 0; i < visibleLines; i++)
    {
      uint16_t menuIndex = frameUpper - 1 + i;
      if (menuIndex >= mainMenuItems) break;
      bool isBrakeRow = (strcmp(g_mainMenu.item[menuIndex].name, getMenuName(g_storedVar.language, 0)) == 0);
      bool isSensiRow = (strcmp(g_mainMenu.item[menuIndex].name, getMenuName(g_storedVar.language, 1)) == 0);
      bool isAntiSpinRow = (strcmp(g_mainMenu.item[menuIndex].name, getMenuName(g_storedVar.language, 2)) == 0);
      /* Print item name */
      /* Item color: WHITE if item is selected, black otherwise */
      obdWriteString(&g_obd, 0, 0, i * lineHeight, g_mainMenu.item[menuIndex].name, menuFont, (g_encoderMainSelector - frameUpper == i) ? OBD_WHITE : OBD_BLACK, 1);

      /* Only print value if value != ITEM_NO_VALUE */
      /* Value color: WHITE if corresponding item is selected AND menu state is VALUE_SELECTION, black otherwise */
      if (g_mainMenu.item[menuIndex].value != ITEM_NO_VALUE) 
      {
        bool isSelectedValueEditing = ((g_encoderMainSelector - frameUpper == i) && (currMenuState == VALUE_SELECTION));
        /* if the value is a number, cast to *(unit16_t *), then print number and unit */
        if (g_mainMenu.item[menuIndex].type == VALUE_TYPE_INTEGER)
        {
          if (isBrakeRow) {
            if (isExtPotBrakeTarget() && !isSelectedValueEditing) {
              formatExtPotLabel(msgStr, sizeof(msgStr), getExtPotIndexForTarget(EXT_POT_TARGET_BRAKE));
            } else {
              formatBrakeValue(msgStr, sizeof(msgStr), getEffectiveBrakeRaw());
            }
          }
          /* SENSI is stored in 0.1% steps and shown with one decimal */
          else if (isSensiRow) {
            if (isExtPotSensiTarget() && !isSelectedValueEditing) {
              formatExtPotLabel(msgStr, sizeof(msgStr), getExtPotIndexForTarget(EXT_POT_TARGET_SENSI));
            } else {
              uint16_t sensiRaw = getEffectiveSensiRaw();
              formatSensiValue(msgStr, sizeof(msgStr), sensiRaw);
            }
          }
          else if (isAntiSpinRow) {
            formatAntiSpinValue(msgStr, sizeof(msgStr), g_storedVar.carParam[g_carSel].antiSpin);
          }
          else {
            /* value is a generic pointer to void, so first cast to uint16_t pointer, then take the pointed value */
            uint16_t shownValue = *(uint16_t *)(g_mainMenu.item[menuIndex].value);
            if (g_mainMenu.item[menuIndex].value == &g_storedVar.carParam[g_carSel].maxSpeed &&
                isLimitShownAsChoke(g_storedVar.terminology)) {
              shownValue = limitPctToChokePct(shownValue);  /* CS/ACD: show LIMIT as CHOKE */
            }
            snprintf(msgStr, sizeof(msgStr), "%3d%s", shownValue, g_mainMenu.item[menuIndex].unit);
          }
          /* Right-align: calculate text width and position from right edge */
          if (isBrakeRow) {
            uint8_t clearChars = max((uint8_t)strlen(msgStr), (uint8_t)strlen(lastBrakeListValue));
            if (clearChars > 0U && (strcmp(msgStr, lastBrakeListValue) != 0 || menuFont != lastBrakeListFont)) {
              clearRightAlignedField(OLED_WIDTH, i * lineHeight, menuFont, clearChars);
            }
            strncpy(lastBrakeListValue, msgStr, sizeof(lastBrakeListValue) - 1U);
            lastBrakeListValue[sizeof(lastBrakeListValue) - 1U] = '\0';
            lastBrakeListFont = menuFont;
          } else if (isSensiRow) {
            uint8_t clearChars = max((uint8_t)strlen(msgStr), (uint8_t)strlen(lastSensiListValue));
            if (clearChars > 0U && (strcmp(msgStr, lastSensiListValue) != 0 || menuFont != lastSensiListFont)) {
              clearRightAlignedField(OLED_WIDTH, i * lineHeight, menuFont, clearChars);
            }
            strncpy(lastSensiListValue, msgStr, sizeof(lastSensiListValue) - 1U);
            lastSensiListValue[sizeof(lastSensiListValue) - 1U] = '\0';
            lastSensiListFont = menuFont;
          } else if (isAntiSpinRow) {
            uint8_t clearChars = max((uint8_t)strlen(msgStr), (uint8_t)strlen(lastAntiSpinListValue));
            if (clearChars > 0U && (strcmp(msgStr, lastAntiSpinListValue) != 0 || menuFont != lastAntiSpinListFont)) {
              clearRightAlignedField(OLED_WIDTH, i * lineHeight, menuFont, clearChars);
            }
            strncpy(lastAntiSpinListValue, msgStr, sizeof(lastAntiSpinListValue) - 1U);
            lastAntiSpinListValue[sizeof(lastAntiSpinListValue) - 1U] = '\0';
            lastAntiSpinListFont = menuFont;
          }
          int textWidth = strlen(msgStr) * charWidth;
          obdWriteString(&g_obd, 0, OLED_WIDTH - textWidth, i * lineHeight, msgStr, menuFont, (isSelectedValueEditing ? OBD_WHITE : OBD_BLACK), 1);
        }
        /* If the value is a decimal, cast to *(unit16_t *), divide by 10^decimalPoint then print number and unit */
        else if (g_mainMenu.item[menuIndex].type == VALUE_TYPE_DECIMAL)
        {
          /* value is a generic pointer to void, so first cast to uint16_t pointer, then take the pointed value */
          tmp = *(uint16_t *)(g_mainMenu.item[menuIndex].value);
          snprintf(msgStr, sizeof(msgStr), "%u.%01u%s",
                   (unsigned int)(tmp / 10U),
                   (unsigned int)(tmp % 10U),
                   g_mainMenu.item[menuIndex].unit);
          uint8_t clearChars = max((uint8_t)strlen(msgStr), (uint8_t)strlen(lastDecimalListValue));
          if (clearChars > 0U && (strcmp(msgStr, lastDecimalListValue) != 0 || menuFont != lastDecimalListFont)) {
            clearRightAlignedField(OLED_WIDTH, i * lineHeight, menuFont, clearChars);
          }
          strncpy(lastDecimalListValue, msgStr, sizeof(lastDecimalListValue) - 1U);
          lastDecimalListValue[sizeof(lastDecimalListValue) - 1U] = '\0';
          lastDecimalListFont = menuFont;
          /* Right-align: calculate text width and position from right edge */
          int textWidth = strlen(msgStr) * charWidth;
          obdWriteString(&g_obd, 0, OLED_WIDTH - textWidth, i * lineHeight, msgStr, menuFont, (((g_encoderMainSelector - frameUpper == i) && (currMenuState == VALUE_SELECTION)) ? OBD_WHITE : OBD_BLACK), 1);
        }
        /* If the value is a string, cast to (char *) then print the string */
        else if (g_mainMenu.item[menuIndex].type == VALUE_TYPE_STRING)
        {
          /* Special handling for VIEW menu item - display language-specific labels */
          if (strcmp(g_mainMenu.item[menuIndex].name, "VIEW") == 0) {
            uint16_t raceViewMode = *(uint16_t *)(g_mainMenu.item[menuIndex].value);
            uint16_t lang = g_storedVar.language;
            snprintf(msgStr, sizeof(msgStr), "%6s", VIEW_MODE_LABELS[lang][raceViewMode]);
          }
          /* Special handling for LANG menu item */
          else if (strcmp(g_mainMenu.item[menuIndex].name, "LANG") == 0) {
            uint16_t language = *(uint16_t *)(g_mainMenu.item[menuIndex].value);
            const char* langLabel = (language <= LANG_MAX) ? LANG_LABELS[language] : LANG_LABELS[LANG_ENG];
            snprintf(msgStr, sizeof(msgStr), "%3s", langLabel);
          }
          else {
            /* value is a generic pointer to void, so cast to string pointer */
            snprintf(msgStr, sizeof(msgStr), "%s", (char *)(g_mainMenu.item[menuIndex].value));
          }
          /* Right-align: calculate text width and position from right edge */
          int textWidth = strlen(msgStr) * charWidth;
          obdWriteString(&g_obd, 0, OLED_WIDTH - textWidth, i * lineHeight, msgStr, menuFont, (((g_encoderMainSelector - frameUpper == i) && (currMenuState == VALUE_SELECTION)) ? OBD_WHITE : OBD_BLACK), 1);
        }
      }
    }

    initMenuItems();  /* update menu items with the storedVar that could have been changed in previous for cycle */
    /* Print LIMITER warning if LIMIT is any value other than 100% */
    if (g_storedVar.carParam[g_carSel].maxSpeed < MAX_SPEED_DEFAULT) 
    {
      obdWriteString(&g_obd, 0, WIDTH8x8, 3 * HEIGHT12x16, (char *)getLimiterLabel(g_storedVar.language), FONT_8x8, OBD_WHITE, 1);
    } 
    else
    {
      obdWriteString(&g_obd, 0, WIDTH8x8, 3 * HEIGHT12x16, (char *)"             ", FONT_8x8, OBD_BLACK, 1);
    }
  }
  } /* End of else (LIST mode) */

  /* Display bottom status line – only when screensaver is not active */
  if (!screensaverActive) {
    displayStatusLine();
  }
}
