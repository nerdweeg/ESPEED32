#include "app_init.h"
#include <Arduino.h>
#include "slot_ESC.h"
#include "ui_text_access.h"
#include "menu_car.h"
#include "settings_quick_brake_menu.h"
#include "settings_menu_root.h"
#include "diagnostics_lap_stats.h"

extern OBDISP g_obd;
extern StoredVar_type g_storedVar;
extern uint16_t g_statsEnabled;
extern uint16_t g_antiSpinStepMs;
extern uint16_t g_antiSpinDisplayMode;
extern uint16_t g_brakeStep;
extern uint16_t g_sensiStep;
extern uint16_t g_advancedMenuEnabled;
extern uint16_t g_encoderInvertEnabled;
extern uint16_t g_pwmFreqMaxProfile;
extern Menu_type g_mainMenu;
extern Menu_type g_settingsMenu;
extern Menu_type g_carMenu;
extern uint16_t g_carSel;
extern AiEsp32RotaryEncoder g_rotaryEncoder;
extern uint8_t getMainMenuItemsCount();
extern void toggleSettingsLock();

#ifdef USE_BACKBUFFER
extern uint8_t ucBackBuffer[1024];
#else
extern uint8_t *ucBackBuffer;
#endif

void IRAM_ATTR readEncoderISR();

/*********************************************************************************************************************/

/**
 * @brief Initialize OLED display and rotary encoder
 * @details Performed separately from HW init to reduce startup time
 */
void initDisplayAndEncoder() 
{
  uint16_t rc;
  /***** OLED Display Setup *****/
  rc = obdI2CInit(&g_obd, MY_OLED, OLED_ADDR, FLIP180, INVERT_DISP, USE_HW_I2C, SDA1_PIN, SCL1_PIN, RESET_PIN, 800000L);  // use standard I2C bus at 400Khz
  if (rc != OLED_NOT_FOUND) 
  {
    obdSetBackBuffer(&g_obd, ucBackBuffer);
    obdFill(&g_obd, OBD_WHITE, 1);
  } 
  else 
  {
    Serial.println("Error! Failed OLED Display initialization!");
  }

  /***** Encoder Setup *****/
  g_rotaryEncoder.begin();
  g_rotaryEncoder.setup(readEncoderISR);
  setUiEncoderBoundaries(1, getMainMenuItemsCount(), false); /* minValue, maxValue, circleValues true|false (when max go to min and vice versa) */
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);        /* Larger number = more accelearation; 0 or 1 means disabled acceleration */
}


/* Rotary Encoder ISR */
void IRAM_ATTR readEncoderISR() {
  g_rotaryEncoder.readEncoder_ISR();
}


/**
 * Initialize the stored variables with default values.
   This is done for every CAR in the CarParam array.
 */
void initStoredVariables() {
  for (int i = 0; i < CAR_MAX_COUNT; i++) {
    g_storedVar.carParam[i].minSpeed = MIN_SPEED_DEFAULT;
    g_storedVar.carParam[i].brake = BRAKE_DEFAULT;
    g_storedVar.carParam[i].maxSpeed = MAX_SPEED_DEFAULT;
    g_storedVar.carParam[i].throttleCurveVertex = { THROTTLE_CURVE_INPUT_THROTTLE_DEFAULT, THROTTLE_CURVE_SPEED_DIFF_DEFAULT };
    g_storedVar.carParam[i].fade = FADE_DEFAULT;
    g_storedVar.carParam[i].antiSpin = ANTISPIN_DEFAULT;
    g_storedVar.carParam[i].freqPWM = PWM_FREQ_DEFAULT;
    g_storedVar.carParam[i].carNumber = i;
    g_storedVar.carParam[i].brakeButtonReduction = BRAKE_BUTTON_REDUCTION_DEFAULT;
    g_storedVar.carParam[i].quickBrakeEnabled = QUICK_BRAKE_ENABLED_DEFAULT;
    g_storedVar.carParam[i].quickBrakeThreshold = QUICK_BRAKE_THRESHOLD_DEFAULT;
    g_storedVar.carParam[i].quickBrakeStrength = QUICK_BRAKE_STRENGTH_DEFAULT;
    sprintf(g_storedVar.carParam[i].carName, "CAR%d", i);
  }
  g_storedVar.selectedCarNumber = 0;
  g_storedVar.minTrigger_raw = 0;
#if defined(TLE493D_MAG)
  g_storedVar.maxTrigger_raw = 3600;              /* TLE493D returns angle * 10 (0-3600) */
#else
  g_storedVar.maxTrigger_raw = ACD_RESOLUTION_STEPS; /* ADC / other sensors (0-4095) */
#endif
  g_storedVar.viewMode = VIEW_MODE_LIST;
  g_storedVar.screensaverTimeout = SCREENSAVER_TIMEOUT_DEFAULT;
  g_storedVar.powerSaveTimeout   = POWER_SAVE_TIMEOUT_DEFAULT;
  g_storedVar.deepSleepTimeout   = DEEP_SLEEP_TIMEOUT_DEFAULT;
  g_storedVar.soundBoot = SOUND_BOOT_DEFAULT;
  g_storedVar.soundRace = SOUND_RACE_DEFAULT;
  g_storedVar.gridCarSelectEnabled = GRID_CAR_SELECT_DEFAULT;  /* Car select in grid view default */
  g_storedVar.raceViewMode = RACE_VIEW_DEFAULT;  /* Default race view mode */
  g_storedVar.language = LANG_DEFAULT;  /* Default language */
  g_storedVar.textCase = TEXT_CASE_DEFAULT;  /* Default text case style */
  g_storedVar.listFontSize = FONT_SIZE_DEFAULT;  /* Default list view font size */
  g_storedVar.startupDelay = STARTUP_DELAY_DEFAULT;  /* Default startup delay (0 = immediate) */
  strncpy(g_storedVar.screensaverLine1, SCREENSAVER_LINE1_DEFAULT, SCREENSAVER_TEXT_MAX - 1);
  g_storedVar.screensaverLine1[SCREENSAVER_TEXT_MAX - 1] = '\0';
  strncpy(g_storedVar.screensaverLine2, SCREENSAVER_LINE2_DEFAULT, SCREENSAVER_TEXT_MAX - 1);
  g_storedVar.screensaverLine2[SCREENSAVER_TEXT_MAX - 1] = '\0';
  g_storedVar.statusSlot[0] = STATUS_SLOT0_DEFAULT;
  g_storedVar.statusSlot[1] = STATUS_SLOT1_DEFAULT;
  g_storedVar.statusSlot[2] = STATUS_SLOT2_DEFAULT;
  g_storedVar.statusSlot[3] = STATUS_SLOT3_DEFAULT;
  g_storedVar.lockMenuEnabled    = LOCK_MENU_ENABLED_DEFAULT;
  g_storedVar.lockShortcutIdx    = LOCK_SHORTCUT_IDX_DEFAULT;
  g_storedVar.lockConfirmEnabled = LOCK_CONFIRM_DEFAULT;
  g_statsEnabled = STATS_ENABLED_DEFAULT;
  g_antiSpinStepMs = ANTISPIN_STEP_DEFAULT;
  g_brakeStep = BRAKE_STEP_DEFAULT;
  g_sensiStep = SENSI_STEP_DEFAULT;
  g_advancedMenuEnabled = ADVANCED_MENU_ENABLED_DEFAULT;
  g_encoderInvertEnabled = ENCODER_INVERT_DEFAULT;
  g_pwmFreqMaxProfile = PWM_FREQ_MAX_PROFILE_DEFAULT;
}


/**
 * Initialize the menu items with the value from the stored variables.
   Insert the items one by one in the main menu global instance.
   Then insert the items (cars) in the car menu.
   To add an item:
    - update the value of MENU_ITEMS_COUNT to match the amount of items in the menu.
    - update MAX_ITEMS so that it is greater or equal MENU_ITEMS_COUNT.
    - Add the item here, in the desired position (check the comment in slot_ESC.h MenuItem_type to know more).
 */
void initMenuItems() {
  int i = 0;
  uint8_t lang = g_storedVar.language;
  /* Init menu items - using language-specific names */

  sprintf(g_mainMenu.item[i].name, "%s", getMenuName(lang, 0));  /* BRAKE */
  g_mainMenu.item[i].value = (void *)&g_storedVar.carParam[g_carSel].brake;
  g_mainMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_mainMenu.item[i].unit, "%%");
  g_mainMenu.item[i].maxValue = BRAKE_MAX_VALUE;
  g_mainMenu.item[i].minValue = 0;
  g_mainMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_mainMenu.item[++i].name, "%s", getMenuName(lang, 1));  /* SENSI */
  g_mainMenu.item[i].value = (void *)&g_storedVar.carParam[g_carSel].minSpeed;
  g_mainMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_mainMenu.item[i].unit, "%%");
  g_mainMenu.item[i].maxValue = min(MIN_SPEED_MAX_VALUE, (int)g_storedVar.carParam[g_carSel].maxSpeed * SENSI_SCALE);
  g_mainMenu.item[i].minValue = 0;
  g_mainMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_mainMenu.item[++i].name, "%s", getMenuName(lang, 2));  /* ANTIS */
  g_mainMenu.item[i].value = (void *)&g_storedVar.carParam[g_carSel].antiSpin;
  g_mainMenu.item[i].type = VALUE_TYPE_INTEGER;
  if (g_antiSpinDisplayMode == ANTISPIN_UI_MODE_PERCENT) {
    sprintf(g_mainMenu.item[i].unit, "%%");
  } else if (g_antiSpinDisplayMode == ANTISPIN_UI_MODE_MS) {
    sprintf(g_mainMenu.item[i].unit, "ms");
  } else {
    sprintf(g_mainMenu.item[i].unit, "");
  }
  g_mainMenu.item[i].maxValue = antiSpinUiMaxValue(g_antiSpinDisplayMode);
  g_mainMenu.item[i].minValue = 0;
  g_mainMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_mainMenu.item[++i].name, "%s", getMenuName(lang, 3));  /* CURVE */
  g_mainMenu.item[i].value = (void *)&g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff;
  g_mainMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_mainMenu.item[i].unit, "%%");
  g_mainMenu.item[i].maxValue = THROTTLE_CURVE_SPEED_DIFF_MAX_VALUE;
  g_mainMenu.item[i].minValue = THROTTLE_CURVE_SPEED_DIFF_MIN_VALUE;
  g_mainMenu.item[i].callback = &showCurveSelection;

  if (g_advancedMenuEnabled) {
    sprintf(g_mainMenu.item[++i].name, "%s", getMenuName(lang, 4));  /* FADE */
    g_mainMenu.item[i].value = (void *)&g_storedVar.carParam[g_carSel].fade;
    g_mainMenu.item[i].type = VALUE_TYPE_INTEGER;
    sprintf(g_mainMenu.item[i].unit, "%%");
    g_mainMenu.item[i].maxValue = FADE_MAX_VALUE;
    g_mainMenu.item[i].minValue = 0;
    g_mainMenu.item[i].callback = &showFadeSelection;

    sprintf(g_mainMenu.item[++i].name, "%s", getMenuName(lang, 5));  /* PWM_F */
    g_mainMenu.item[i].value = (void *)&g_storedVar.carParam[g_carSel].freqPWM;
    g_mainMenu.item[i].type = VALUE_TYPE_DECIMAL;
    sprintf(g_mainMenu.item[i].unit, "k");
    g_mainMenu.item[i].maxValue = getConfiguredPwmFreqMaxRaw();
    g_mainMenu.item[i].minValue = FREQ_MIN_VALUE / 100;
    g_mainMenu.item[i].decimalPoint = 1;
    g_mainMenu.item[i].callback = ITEM_NO_CALLBACK;

    sprintf(g_mainMenu.item[++i].name, "%s", getMenuName(lang, 6));  /* BRAKE+ submenu */
    g_mainMenu.item[i].value = ITEM_NO_VALUE;
    g_mainMenu.item[i].type = VALUE_TYPE_INTEGER;
    sprintf(g_mainMenu.item[i].unit, "");
    g_mainMenu.item[i].maxValue = 0;
    g_mainMenu.item[i].minValue = 0;
    g_mainMenu.item[i].callback = &showAdvancedBrakeMenu;
  }

  sprintf(g_mainMenu.item[++i].name, "%s", getMenuName(lang, 7));  /* LIMIT */
  g_mainMenu.item[i].value = (void *)&g_storedVar.carParam[g_carSel].maxSpeed;
  g_mainMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_mainMenu.item[i].unit, "%%");
  g_mainMenu.item[i].maxValue = MAX_SPEED_DEFAULT;
  g_mainMenu.item[i].minValue = max(5, (int)sensiToWholePctCeil(g_storedVar.carParam[g_carSel].minSpeed) + 5);
  g_mainMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_mainMenu.item[++i].name, "%s", getMenuName(lang, 8));  /* SETTINGS */
  g_mainMenu.item[i].value = ITEM_NO_VALUE;
  g_mainMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_mainMenu.item[i].unit, "");
  g_mainMenu.item[i].maxValue = 0;
  g_mainMenu.item[i].minValue = 0;
  g_mainMenu.item[i].callback = &showSettingsMenu;

  if (g_statsEnabled) {
    sprintf(g_mainMenu.item[++i].name, "%s", getMenuName(lang, 9));  /* STATS */
    g_mainMenu.item[i].value = ITEM_NO_VALUE;
    g_mainMenu.item[i].type = VALUE_TYPE_INTEGER;
    sprintf(g_mainMenu.item[i].unit, "");
    g_mainMenu.item[i].maxValue = 0;
    g_mainMenu.item[i].minValue = 0;
    g_mainMenu.item[i].callback = &showLapStats;
  }

  if (g_storedVar.lockMenuEnabled) {
    sprintf(g_mainMenu.item[++i].name, "%s", getMenuName(lang, 10));  /* LOCK */
    g_mainMenu.item[i].value = ITEM_NO_VALUE;
    g_mainMenu.item[i].type = VALUE_TYPE_INTEGER;
    sprintf(g_mainMenu.item[i].unit, "");
    g_mainMenu.item[i].maxValue = 0;
    g_mainMenu.item[i].minValue = 0;
    g_mainMenu.item[i].callback = &toggleSettingsLock;
  }

  sprintf(g_mainMenu.item[++i].name, "%s", getMenuName(lang, 11));  /* CAR or BIL */
  g_mainMenu.item[i].value = (void *)&g_storedVar.carParam[g_carSel].carName;
  g_mainMenu.item[i].type = VALUE_TYPE_STRING;
  g_mainMenu.item[i].maxValue = CAR_MAX_COUNT - 1;  // so menu will scroll in the array (CAR_MAX_COUNT long)
  g_mainMenu.item[i].minValue = 0;
  g_mainMenu.item[i].callback = &showSelectRenameCar;

  /* Init Car selection menu items */
  for (uint8_t j = 0; j < CAR_MAX_COUNT; j++)
  {
    sprintf(g_carMenu.item[j].name, (const char *)g_storedVar.carParam[j].carName);
    g_carMenu.item[j].value = (void *)&g_storedVar.carParam[j].carNumber;
  }
}


/**
 * Initialize the settings submenu items
 */
void initSettingsMenuItems() {
  int i = 0;
  uint8_t lang = g_storedVar.language;

  sprintf(g_settingsMenu.item[i].name, "%s", getSettingsMenuName(lang, 0));  /* POWER - opens submenu */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getSettingsMenuName(lang, 1));  /* DISPLAY - opens submenu */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getSettingsMenuName(lang, 2));  /* SOUND - opens submenu */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getSettingsMenuName(lang, 3));  /* HARDWARE */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getSettingsMenuName(lang, 4));  /* STATS */
  g_settingsMenu.item[i].value = (void *)&g_statsEnabled;
  g_settingsMenu.item[i].type = VALUE_TYPE_STRING;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 1;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getSettingsMenuName(lang, 5));  /* WIFI */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_STRING;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getSettingsMenuName(lang, 6));  /* LOGGING */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_STRING;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getSettingsMenuName(lang, 7));  /* LOCK - opens submenu */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getSettingsMenuName(lang, 8));  /* USB */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getSettingsMenuName(lang, 9));  /* RESET */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getSettingsMenuName(lang, 10));  /* ABOUT/INFO */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getSettingsMenuName(lang, 11));  /* BACK */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_STRING;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;
}

void initDisplayMenuItems() {
  int i = 0;
  uint8_t lang = g_storedVar.language;

  sprintf(g_settingsMenu.item[i].name, "%s", getDisplayMenuName(lang, 0));  /* VIEW */
  g_settingsMenu.item[i].value = (void *)&g_storedVar.raceViewMode;
  g_settingsMenu.item[i].type = VALUE_TYPE_STRING;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = RACE_VIEW_SIMPLE;
  g_settingsMenu.item[i].minValue = RACE_VIEW_OFF;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getDisplayMenuName(lang, 1));  /* LANG */
  g_settingsMenu.item[i].value = (void *)&g_storedVar.language;
  g_settingsMenu.item[i].type = VALUE_TYPE_STRING;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = LANG_MAX;
  g_settingsMenu.item[i].minValue = LANG_NOR;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getDisplayMenuName(lang, 2));  /* CASE */
  g_settingsMenu.item[i].value = (void *)&g_storedVar.textCase;
  g_settingsMenu.item[i].type = VALUE_TYPE_STRING;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = TEXT_CASE_PASCAL;
  g_settingsMenu.item[i].minValue = TEXT_CASE_UPPER;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getDisplayMenuName(lang, 3));  /* FSIZE */
  g_settingsMenu.item[i].value = (void *)&g_storedVar.listFontSize;
  g_settingsMenu.item[i].type = VALUE_TYPE_STRING;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = FONT_SIZE_SMALL;
  g_settingsMenu.item[i].minValue = FONT_SIZE_LARGE;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getDisplayMenuName(lang, DISPLAY_ITEM_ADVANCED_IDX));  /* ADVANCED */
  g_settingsMenu.item[i].value = (void *)&g_advancedMenuEnabled;
  g_settingsMenu.item[i].type = VALUE_TYPE_STRING;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 1;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getDisplayMenuName(lang, DISPLAY_ITEM_STEPS_IDX));  /* STEPS - opens submenu */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getDisplayMenuName(lang, DISPLAY_ITEM_STATUS_IDX));  /* STATUS - opens submenu */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getDisplayMenuName(lang, DISPLAY_ITEM_BACK_IDX));  /* BACK */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_STRING;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;
}

void initStepsMenuItems() {
  int i = 0;
  uint8_t lang = g_storedVar.language;

  sprintf(g_settingsMenu.item[i].name, "%s", getStepsMenuName(lang, 0));  /* ANTISPIN - opens submenu */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getStepsMenuName(lang, 1));  /* BRAKE STEP */
  g_settingsMenu.item[i].value = (void *)&g_brakeStep;
  g_settingsMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = BRAKE_STEP_MAX;
  g_settingsMenu.item[i].minValue = BRAKE_STEP_MIN;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getStepsMenuName(lang, 2));  /* SENSI STEP */
  g_settingsMenu.item[i].value = (void *)&g_sensiStep;
  g_settingsMenu.item[i].type = VALUE_TYPE_INTEGER;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = SENSI_STEP_MAX;
  g_settingsMenu.item[i].minValue = SENSI_STEP_MIN;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;

  sprintf(g_settingsMenu.item[++i].name, "%s", getStepsMenuName(lang, 3));  /* BACK */
  g_settingsMenu.item[i].value = ITEM_NO_VALUE;
  g_settingsMenu.item[i].type = VALUE_TYPE_STRING;
  sprintf(g_settingsMenu.item[i].unit, "");
  g_settingsMenu.item[i].maxValue = 0;
  g_settingsMenu.item[i].minValue = 0;
  g_settingsMenu.item[i].callback = ITEM_NO_CALLBACK;
}
