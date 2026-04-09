/*********************************************************************************************************************/
/*                                                   Includes                                                        */
/*********************************************************************************************************************/
#include "slot_ESC.h"
#include <esp_ota_ops.h>
#include "HAL.h"
#include "ui_strings.h"
#include "ui_text_access.h"
#include "connectivity_portal.h"
#include "power_ui.h"
#include "app_init.h"
#include "settings_display_submenus.h"
#include "settings_display_menu.h"
#include "settings_power_sleep_submenus.h"
#include "settings_sound_wifi_submenus.h"
#include "settings_ext_pot_menu.h"
#include "settings_power_menu.h"
#include "settings_menu_root.h"
#include "settings_quick_brake_menu.h"
#include "settings_reset_menu.h"
#include "diagnostics_self_test.h"
#include "diagnostics_lap_stats.h"
#include "settings_about_screen.h"
#include "menu_car.h"
#include "ui_render.h"
#include "task_control_loop.h"
#include "ext_pot.h"
#include "telemetry_logging.h"

/* Version defined in slot_ESC.h */

/*********************************************************************************************************************/
/*                                                   UI Text Modules                                                 */
/*********************************************************************************************************************/
/* Text arrays and text-case helpers are implemented in:
 * - ui_strings.cpp/.h
 * - ui_text_access.cpp/.h
 */

/*********************************************************************************************************************/
/*                                                   Global Variables                                                */
/*********************************************************************************************************************/

/* FreeRTOS Task Handles */
TaskHandle_t Task1;
TaskHandle_t Task2;
TaskHandle_t WiFiTask;

/* State Machine */
StateMachine_enum g_currState = INIT;

/* Display Components */
#ifdef USE_BACKBUFFER
uint8_t ucBackBuffer[1024];
#else
uint8_t *ucBackBuffer = NULL;
#endif

OBDISP g_obd;         /* OLED display instance */
char msgStr[50];      /* Display message buffer */

/* Car Selection */
uint16_t g_carSel;    /* Currently selected car model index */

/* Rotary Encoder */
AiEsp32RotaryEncoder g_rotaryEncoder = AiEsp32RotaryEncoder(ENCODER_A_PIN, ENCODER_B_PIN, ENCODER_BUTTON_PIN, ENCODER_VCC_PIN, ENCODER_STEPS);

uint8_t g_encoderMainSelector = 1;                    /* Main menu item selector */
static uint16_t g_encoderSecondarySelector = 0;       /* Secondary value selector */
static uint16_t *g_encoderSelectedValuePtr = NULL;    /* Pointer to currently selected value */
static uint16_t g_originalValueBeforeEdit = 0;        /* Store original value when entering VALUE_SELECTION (for cancel) */
static bool g_isEditingCarSelection = false;          /* Flag to prevent g_carSel update during CAR edit */
static bool g_antiSpinStepEditActive = false;         /* True while ANTIS uses stepped encoder editing */
static uint16_t g_antiSpinEditLastEncoder = 0;        /* Raw encoder position used to detect ANTIS step changes */
static bool g_brakeStepEditActive = false;            /* True while BRAKE uses stepped encoder editing */
static uint16_t g_brakeEditStartEncoder = 0;          /* Encoder position at BRAKE edit start */
static uint16_t g_brakeEditBaseValue = 0;             /* BRAKE raw value at edit start */
static bool g_sensiStepEditActive = false;            /* True while SENSI uses stepped encoder editing */
static uint16_t g_sensiEditStartEncoder = 0;          /* Encoder position at SENSI edit start */
static uint16_t g_sensiEditBaseValue = 0;             /* SENSI raw value at edit start */

/* Stored Variables (EEPROM/Preferences) */
StoredVar_type g_storedVar;
uint16_t g_statsEnabled = STATS_ENABLED_DEFAULT;  /* Main menu STATS visibility: 0=hidden, 1=shown */
uint16_t g_antiSpinStepMs = ANTISPIN_STEP_DEFAULT; /* Global encoder step when editing ANTIS */
uint16_t g_antiSpinStepPct = ANTISPIN_STEP_PCT_DEFAULT; /* Global encoder step when editing ANTIS in percent mode */
uint16_t g_antiSpinDisplayMode = ANTISPIN_UI_MODE_DEFAULT; /* Global ANTIS display/edit mode: ms, %, or text */
uint16_t g_brakeStep = BRAKE_STEP_DEFAULT;   /* Global encoder step when editing BRAKE [raw units = 0.1% each] */
uint16_t g_sensiStep = SENSI_STEP_DEFAULT;   /* Global encoder step when editing SENSI [raw units = 0.1% each] */
uint16_t g_encoderInvertEnabled = ENCODER_INVERT_DEFAULT; /* Global encoder direction: 0=default, 1=inverted */
uint16_t g_adcVoltageRange_mV = ACD_VOLTAGE_RANGE_DEFAULT_MVOLTS; /* Global ADC voltage scale used for VIN/current conversion */

/* ESC Runtime Variables */
ESC_type g_escVar {
  .outputSpeed_pct = 0,
  .trigger_raw = 0,
  .trigger_norm = 0,
  .encoderPos = 1,
  .Vin_mV = 0,
  .motorCurrent_mA = 0,
  .effectiveBrake_raw = BRAKE_DEFAULT,
  .effectiveSensi_raw = MIN_SPEED_DEFAULT,
  .lapCount = 0,
  .bestLapTime_ms = 0,
  .lapStartTime_ms = 0
};

/* Menu Structures */
Menu_type g_mainMenu {
  .lines = 3
};

Menu_type g_settingsMenu {
  .lines = 3
};

Menu_type g_carMenu {
  .lines = 3
};

/* Preferences (NVM storage) */
Preferences g_pref;

/* UI Timing */
uint32_t g_lastEncoderInteraction = 0;         /* Timestamp of last encoder interaction for display power saving */
bool g_forceRaceRedraw = false; /* Force race mode display to redraw */

/* Settings lock — volatile, resets on reboot */
static bool g_settingsLocked = false;
static void showSettingsLockConfirmationOverlay() {
  if (!g_storedVar.lockConfirmEnabled) return;

  obdFill(&g_obd, OBD_WHITE, 1);
  const char* msg = g_settingsLocked ? "LOCKED" : "UNLOCKED";
  uint8_t msgW = strlen(msg) * WIDTH8x8;
  obdWriteString(&g_obd, 0, (OLED_WIDTH - msgW) / 2, 3 * HEIGHT8x8, (char *)msg, FONT_8x8, OBD_BLACK, 1);
  obdDumpBuffer(&g_obd, NULL, 1, 0, 0);
  delay(800);
  obdFill(&g_obd, OBD_WHITE, 1);
}

bool isSettingsLocked() { return g_settingsLocked; }
void toggleSettingsLock() {
  g_settingsLocked = !g_settingsLocked;
  showSettingsLockConfirmationOverlay();
  g_forceRaceRedraw = true;
}

/* Menu Navigation State */
static bool g_inSettingsMenu = false;  /* Track if we're currently in the settings submenu */
static bool g_escapeToMain = false;    /* Set by any submenu long press → cascade-breaks to RUNNING for race mode toggle */
static bool g_raceToggleReleaseGuardActive = false;  /* Swallow release click after race-mode long press. */
static uint32_t g_raceToggleReleaseAtMs = 0;  /* Release timestamp used to re-arm short presses after race toggle. */
static uint16_t g_wifiTimedMinutes = 10;      /* Runtime-only default for timed WiFi activation */
static bool g_wifiTimedActive = false;        /* True when background WiFi should auto-stop on deadline */
static uint32_t g_wifiTimedStopAtMs = 0;      /* millis() deadline for auto-stop */
static uint16_t g_loggingTimedMinutes = 30;   /* Runtime-only default for timed telemetry logging */
static bool g_loggingTimedActive = false;     /* True when logging should auto-stop on deadline */
static uint32_t g_loggingTimedStopAtMs = 0;   /* millis() deadline for telemetry logging stop */
static const char* PREF_KEY_SENSI_HALF = "sensi_half_v1"; /* migration marker for 0.5% SENSI storage */
static const char* PREF_KEY_STATS_ENABLED = "stats_en_v1"; /* persistent STATS visibility toggle */
static const char* PREF_KEY_ANTIS_STEP = "antis_step_v1";  /* persistent ANTIS encoder step */
static const char* PREF_KEY_ANTIS_STEP_PCT = "antis_pct_v1"; /* persistent ANTIS percent-step */
static const char* PREF_KEY_ANTIS_MODE = "antis_mode_v1";  /* persistent ANTIS display/edit mode */
static const char* PREF_KEY_BRAKE_STEP = "brake_step_v1";  /* persistent BRAKE encoder step */
static const char* PREF_KEY_SENSI_STEP = "sensi_step_v1";  /* persistent SENSI encoder step */
static const char* PREF_KEY_ENC_INVERT = "enc_inv_v1";     /* persistent encoder inversion toggle */
static const char* PREF_KEY_ADC_RANGE = "adc_rng_mv_v1";   /* persistent ADC voltage calibration */
static const char* PREF_KEY_EXT_POT1_TARGET = "ext_pot1_tgt";
static const char* PREF_KEY_EXT_POT2_TARGET = "ext_pot2_tgt";
static const char* PREF_KEY_EXT_POT_ENABLED_LEGACY = "ext_pot_en";
static const char* PREF_KEY_EXT_POT_TARGET_LEGACY = "ext_pot_tgt";
static bool g_startWiFiAfterOtaBoot = false;

static bool canMigrateStoredVarVersion(uint8_t storedVarVersion) {
  return storedVarVersion == 22U || storedVarVersion == 23U;
}

static void migrateStoredVarFromHalfPctToTenthPct(StoredVar_type* storedVar) {
  if (storedVar == nullptr) return;

  for (uint8_t i = 0; i < CAR_MAX_COUNT; i++) {
    CarParam_type& car = storedVar->carParam[i];
    uint32_t migratedSensi = (uint32_t)car.minSpeed * (SENSI_SCALE / 2U);  /* old raw unit was 0.5% */
    uint32_t migratedBrake = (uint32_t)car.brake * BRAKE_SCALE;            /* old brake unit was 1% */

    uint32_t carMaxSensi = (uint32_t)car.maxSpeed * SENSI_SCALE;
    car.minSpeed = (uint16_t)min((uint32_t)MIN_SPEED_MAX_VALUE, min(carMaxSensi, migratedSensi));
    car.brake = (uint16_t)min((uint32_t)BRAKE_MAX_VALUE, migratedBrake);
  }
}

/* Long press tracking shared across all submenus (only one active at a time) */
static uint32_t g_lpRaceStart = 0;
static bool g_lpRaceActive = false;
void serviceTimedWiFiPortal();
void showPowerSave();
void showPowerSave(uint32_t inactivityStartMs);
void showDeepSleep();

static long g_uiEncoderMin = 1;
static long g_uiEncoderMax = MENU_ITEMS_COUNT;

long readUiEncoder() {
  long rawValue = g_rotaryEncoder.readEncoder();
  if (!g_encoderInvertEnabled) {
    return rawValue;
  }
  return (g_uiEncoderMin + g_uiEncoderMax) - rawValue;
}

void resetUiEncoder(long logicalValue) {
  logicalValue = constrain(logicalValue, g_uiEncoderMin, g_uiEncoderMax);
  long rawValue = g_encoderInvertEnabled ? ((g_uiEncoderMin + g_uiEncoderMax) - logicalValue) : logicalValue;
  g_rotaryEncoder.reset(rawValue);
}

void setUiEncoderBoundaries(long minValue, long maxValue, bool circleValues) {
  g_uiEncoderMin = minValue;
  g_uiEncoderMax = maxValue;
  g_rotaryEncoder.setBoundaries(minValue, maxValue, circleValues);
}

void applyEncoderInvertSetting(uint16_t enabled) {
  uint16_t normalized = enabled ? 1 : 0;
  if (g_encoderInvertEnabled == normalized) {
    return;
  }
  long logicalValue = readUiEncoder();
  g_encoderInvertEnabled = normalized;
  resetUiEncoder(logicalValue);
}

static bool isAntiSpinEditTarget() {
  return g_encoderSelectedValuePtr == &g_storedVar.carParam[g_carSel].antiSpin;
}

static bool isBrakeEditTarget() {
  return g_encoderSelectedValuePtr == &g_storedVar.carParam[g_carSel].brake;
}

static bool isSensiEditTarget() {
  return g_encoderSelectedValuePtr == &g_storedVar.carParam[g_carSel].minSpeed;
}

static uint16_t ceilDivU16(uint16_t value, uint16_t divisor) {
  if (divisor == 0) return 0;
  return (uint16_t)(((uint32_t)value + (uint32_t)divisor - 1U) / (uint32_t)divisor);
}

static uint16_t getBrakeEditStepRaw() {
  return constrain(g_brakeStep, BRAKE_STEP_MIN, BRAKE_STEP_MAX);
}

static uint16_t getSensiEditStepRaw() {
  return constrain(g_sensiStep, SENSI_STEP_MIN, SENSI_STEP_MAX);
}

static uint16_t getSelectedSensiMaxRaw() {
  return min((uint16_t)MIN_SPEED_MAX_VALUE,
             (uint16_t)(g_storedVar.carParam[g_carSel].maxSpeed * SENSI_SCALE));
}

static uint16_t snapRawValueDownToStep(uint16_t rawValue, uint16_t stepRaw) {
  if (stepRaw == 0U) return rawValue;
  return (uint16_t)((rawValue / stepRaw) * stepRaw);
}

static uint16_t getSteppedEditStartEncoder(uint16_t rawValue, uint16_t stepRaw) {
  if (stepRaw == 0U) return 0U;
  return (uint16_t)(rawValue / stepRaw);
}

static uint16_t getSteppedEditMaxEncoder(uint16_t maxRaw, uint16_t stepRaw) {
  if (stepRaw == 0U) return 0U;
  return (uint16_t)(maxRaw / stepRaw);
}

static uint16_t configureEncoderForSelectedValueEdit(uint16_t minRaw, uint16_t maxRaw) {
  if (g_encoderSelectedValuePtr == NULL) {
    setUiEncoderBoundaries(minRaw, maxRaw, false);
    resetUiEncoder(minRaw);
    return minRaw;
  }

  if (isBrakeEditTarget() || isSensiEditTarget()) {
    uint16_t stepRaw = isBrakeEditTarget() ? getBrakeEditStepRaw() : getSensiEditStepRaw();
    uint16_t rawValue = constrain(*g_encoderSelectedValuePtr, minRaw, maxRaw);
    rawValue = snapRawValueDownToStep(rawValue, stepRaw);
    *g_encoderSelectedValuePtr = rawValue;
    uint16_t startEncoder = getSteppedEditStartEncoder(rawValue, stepRaw);
    uint16_t maxEncoder = getSteppedEditMaxEncoder(maxRaw, stepRaw);
    setUiEncoderBoundaries(0, maxEncoder, false);
    resetUiEncoder(startEncoder);
    return startEncoder;
  }

  uint16_t startValue = isAntiSpinEditTarget()
    ? antiSpinMsToUiValue(*g_encoderSelectedValuePtr, g_antiSpinDisplayMode)
    : *g_encoderSelectedValuePtr;
  setUiEncoderBoundaries(minRaw, maxRaw, false);
  resetUiEncoder(startValue);
  return startValue;
}

static void beginSteppedValueEdit() {
  g_antiSpinStepEditActive = isAntiSpinEditTarget();
  g_brakeStepEditActive    = isBrakeEditTarget();
  g_sensiStepEditActive    = isSensiEditTarget();
  g_antiSpinEditLastEncoder = g_antiSpinStepEditActive
    ? antiSpinMsToUiValue(*g_encoderSelectedValuePtr, g_antiSpinDisplayMode)
    : 0;
  g_brakeEditBaseValue = g_brakeStepEditActive ? *g_encoderSelectedValuePtr : 0;
  g_brakeEditStartEncoder = g_brakeStepEditActive ? getSteppedEditStartEncoder(g_brakeEditBaseValue, getBrakeEditStepRaw()) : 0;
  g_sensiEditBaseValue = g_sensiStepEditActive ? *g_encoderSelectedValuePtr : 0;
  g_sensiEditStartEncoder = g_sensiStepEditActive ? getSteppedEditStartEncoder(g_sensiEditBaseValue, getSensiEditStepRaw()) : 0;
}

static void endSteppedValueEdit() {
  g_antiSpinStepEditActive = false;
  g_brakeStepEditActive    = false;
  g_sensiStepEditActive    = false;
  g_antiSpinEditLastEncoder = 0;
  g_brakeEditStartEncoder   = 0;
  g_brakeEditBaseValue      = 0;
  g_sensiEditStartEncoder   = 0;
  g_sensiEditBaseValue      = 0;
}

static void applyValueEditFromEncoder(uint16_t encoderValue) {
  if (g_encoderSelectedValuePtr == NULL) {
    return;
  }

  if (g_antiSpinStepEditActive && isAntiSpinEditTarget()) {
    if (g_antiSpinDisplayMode == ANTISPIN_UI_MODE_TEXT) {
      *g_encoderSelectedValuePtr = antiSpinUiValueToMs(encoderValue, g_antiSpinDisplayMode);
      g_antiSpinEditLastEncoder = encoderValue;
      return;
    }
    int32_t deltaTicks = (int32_t)encoderValue - (int32_t)g_antiSpinEditLastEncoder;
    if (deltaTicks == 0) return;
    if (g_antiSpinDisplayMode == ANTISPIN_UI_MODE_PERCENT) {
      int32_t currentPercent = (int32_t)antiSpinMsToPercent(*g_encoderSelectedValuePtr);
      int32_t newPercent = currentPercent + (deltaTicks * (int32_t)g_antiSpinStepPct);
      if (newPercent < 0) newPercent = 0;
      if (newPercent > 100) newPercent = 100;
      *g_encoderSelectedValuePtr = antiSpinPercentToMs((uint16_t)newPercent);
    } else {
      int32_t newValue = (int32_t)(*g_encoderSelectedValuePtr) + (deltaTicks * (int32_t)g_antiSpinStepMs);
      *g_encoderSelectedValuePtr = (uint16_t)constrain(newValue, 0, ANTISPIN_MAX_VALUE);
    }
    g_antiSpinEditLastEncoder = encoderValue;
  } else if (g_brakeStepEditActive && isBrakeEditTarget()) {
    int32_t delta = (int32_t)encoderValue - (int32_t)g_brakeEditStartEncoder;
    if (delta == 0) return;
    int32_t newValue = (int32_t)g_brakeEditBaseValue + (delta * (int32_t)getBrakeEditStepRaw());
    *g_encoderSelectedValuePtr = (uint16_t)constrain(newValue, 0, BRAKE_MAX_VALUE);
  } else if (g_sensiStepEditActive && isSensiEditTarget()) {
    int32_t delta = (int32_t)encoderValue - (int32_t)g_sensiEditStartEncoder;
    if (delta == 0) return;
    int32_t newValue = (int32_t)g_sensiEditBaseValue + (delta * (int32_t)getSensiEditStepRaw());
    uint16_t maxSensiRaw = getSelectedSensiMaxRaw();
    *g_encoderSelectedValuePtr = (uint16_t)constrain(newValue, 0, maxSensiRaw);
  } else {
    *g_encoderSelectedValuePtr = encoderValue;
  }
}

/**
 * @brief Detect long press on encoder button (shared across all submenus).
 * @return true once per long press when threshold is exceeded.
 */
bool checkRaceModeEscape() {
  serviceTimedWiFiPortal();

  if (digitalRead(ENCODER_BUTTON_PIN) == BUTTON_PRESSED) {
    if (!g_lpRaceActive) { g_lpRaceStart = millis(); g_lpRaceActive = true; }
    if (millis() - g_lpRaceStart > BUTTON_LONG_PRESS_MS) {
      g_lpRaceActive = false;
      return true;
    }
  } else {
    g_lpRaceActive = false;
  }
  return false;
}

uint8_t getMainMenuItemsCount();

/**
 * @brief Toggle race/list view mode and reset encoder for new mode.
 * Called from RUNNING when g_escapeToMain is set, or on direct long press.
 * @param menuState Reference to RUNNING's menuState (reset to ITEM_SELECTION).
 * @param lastLongPressTime Reference to guard timestamp (prevent double-trigger).
 */
static void applyRaceModeToggle(MenuState_enum &menuState, uint32_t &lastLongPressTime) {
  if (g_storedVar.viewMode == VIEW_MODE_LIST && g_storedVar.raceViewMode != RACE_VIEW_OFF) {
    g_storedVar.viewMode = VIEW_MODE_GRID;
  } else if (g_storedVar.viewMode == VIEW_MODE_GRID) {
    g_storedVar.viewMode = VIEW_MODE_LIST;
  }
  obdFill(&g_obd, OBD_WHITE, 1);
  menuState = ITEM_SELECTION;
  if (g_storedVar.viewMode == VIEW_MODE_GRID) {
    uint8_t gridItems = (g_storedVar.raceViewMode == RACE_VIEW_SIMPLE)
      ? (g_storedVar.gridCarSelectEnabled ? 3 : 2)
      : (g_storedVar.gridCarSelectEnabled ? 5 : 4);
    setUiEncoderBoundaries(1, gridItems, false);
  } else {
    setUiEncoderBoundaries(1, getMainMenuItemsCount(), false);
  }
  resetUiEncoder(1);
  g_encoderMainSelector = 1;
  g_escVar.encoderPos = 1;
  lastLongPressTime = millis();
  g_raceToggleReleaseGuardActive = true;
  g_raceToggleReleaseAtMs = 0;
  saveEEPROM(g_storedVar);
  if (g_storedVar.soundRace) keySound();
  g_lastEncoderInteraction = millis();
}

/**
 * @brief Hold off short-press handling until the encoder button is released after a race-mode long press.
 * @details RUNNING uses raw press/release detection, so we only need to wait for a clean release
 *          plus a short debounce window before re-arming short clicks.
 * @param encoderButtonPressed Current raw encoder button state.
 * @return true while short presses should remain blocked.
 */
static bool serviceRaceModeToggleReleaseGuard(bool encoderButtonPressed) {
  if (!g_raceToggleReleaseGuardActive) {
    return false;
  }

  if (encoderButtonPressed) {
    g_raceToggleReleaseAtMs = 0;
    return true;
  }

  if (g_raceToggleReleaseAtMs == 0) {
    g_raceToggleReleaseAtMs = millis();
    return true;
  }

  if ((uint32_t)(millis() - g_raceToggleReleaseAtMs) < BUTTON_DEBOUNCE_AFTER_LONG_MS) {
    return true;
  }

  g_raceToggleReleaseGuardActive = false;
  g_raceToggleReleaseAtMs = 0;
  return false;
}

void requestEscapeToMain() {
  g_escapeToMain = true;
}

bool isEscapeToMainRequested() {
  return g_escapeToMain;
}

void setLastEncoderInteraction(uint32_t interactionMs) {
  g_lastEncoderInteraction = interactionMs;
}

void setInSettingsMenu(bool active) {
  g_inSettingsMenu = active;
}

uint8_t getMainMenuSelector() {
  uint8_t maxItems = getMainMenuItemsCount();
  if (g_encoderMainSelector < 1) g_encoderMainSelector = 1;
  if (g_encoderMainSelector > maxItems) g_encoderMainSelector = maxItems;
  return g_encoderMainSelector;
}

uint8_t getMainMenuItemsCount() {
  uint8_t count = MENU_ITEMS_COUNT;
  if (!g_statsEnabled) count--;
  if (!g_storedVar.lockMenuEnabled) count--;
  return count;
}

void resetEncoderForMainMenu() {
  uint8_t menuItems = getMainMenuItemsCount();
  if (g_encoderMainSelector < 1) g_encoderMainSelector = 1;
  if (g_encoderMainSelector > menuItems) g_encoderMainSelector = menuItems;
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(1, menuItems, false);
  resetUiEncoder(g_encoderMainSelector);
}

void serviceTimedWiFiPortal() {
  serviceConnectivityPortal();

  if (g_loggingTimedActive) {
    if (!telemetryIsLoggingActive()) {
      g_loggingTimedActive = false;
    } else if ((int32_t)(millis() - g_loggingTimedStopAtMs) >= 0) {
      telemetryStopLogging();
      g_loggingTimedActive = false;
    }
  }

  if (!g_wifiTimedActive) {
    return;
  }

  if (!isWiFiPortalActive()) {
    g_wifiTimedActive = false;
    return;
  }

  if ((int32_t)(millis() - g_wifiTimedStopAtMs) >= 0) {
    if (isOtaInProgress()) {
      /* Keep portal alive during OTA and retry timed stop later. */
      g_wifiTimedStopAtMs = millis() + 1000UL;
      return;
    }
    if (telemetryIsLoggingActive()) {
      /* Keep WiFi available while telemetry is being logged over the portal. */
      g_wifiTimedStopAtMs = millis() + 1000UL;
      return;
    }
    stopWiFiPortal();
    g_wifiTimedActive = false;
  }
}

void startTimedWiFiPortal(uint16_t minutes) {
  minutes = constrain(minutes, 1, 120);
  if (startWiFiPortal()) {
    g_wifiTimedMinutes = minutes;
    g_wifiTimedActive = true;
    g_wifiTimedStopAtMs = millis() + ((uint32_t)minutes * 60000UL);
  }
}

void stopTimedWiFiPortal() {
  if (isOtaInProgress()) {
    return;
  }
  g_wifiTimedActive = false;
  if (isWiFiPortalActive()) {
    stopWiFiPortal();
  }
}

uint16_t getWiFiTimedMinutes() {
  return g_wifiTimedMinutes;
}

void setWiFiTimedMinutes(uint16_t minutes) {
  g_wifiTimedMinutes = constrain(minutes, 1, 120);
}

void startTimedTelemetryLogging(uint16_t minutes) {
  g_loggingTimedMinutes = constrain(minutes, 1, 120);
  g_loggingTimedActive = true;
  g_loggingTimedStopAtMs = millis() + ((uint32_t)g_loggingTimedMinutes * 60000UL);
}

void stopTimedTelemetryLogging() {
  g_loggingTimedActive = false;
}

uint16_t getTelemetryTimedMinutes() {
  return g_loggingTimedMinutes;
}

void setTelemetryTimedMinutes(uint16_t minutes) {
  g_loggingTimedMinutes = constrain(minutes, 1, 120);
}

/*********************************************************************************************************************/
/*                                                Function Prototypes                                                */
/*********************************************************************************************************************/
void IRAM_ATTR readEncoderISR();
void showPowerSave();
void showPowerSave(uint32_t inactivityStartMs);
void showDeepSleep();
void serviceTimedWiFiPortal();
void startTimedWiFiPortal(uint16_t minutes);
void stopTimedWiFiPortal();
uint16_t getWiFiTimedMinutes();
void setWiFiTimedMinutes(uint16_t minutes);
void startTimedTelemetryLogging(uint16_t minutes);
void stopTimedTelemetryLogging();
uint16_t getTelemetryTimedMinutes();
void setTelemetryTimedMinutes(uint16_t minutes);
bool isEscapeToMainRequested();
void setLastEncoderInteraction(uint32_t interactionMs);
uint8_t getMainMenuSelector();
void resetEncoderForMainMenu();
void initDisplayMenuItems();

/*********************************************************************************************************************/
/*                                                Setup Function                                                     */
/*********************************************************************************************************************/

/**
 * @brief Main setup function - initializes hardware and creates FreeRTOS tasks
 */
void setup() {
  /* Pin and Serial Setup */
  HAL_PinSetup();

  /* HalfBridge & Hardware Setup */
  HalfBridge_Setup();

  /* Mark this firmware as valid unconditionally so the ESP32 bootloader
   * does not roll back regardless of stored-var migration state. */
  esp_ota_mark_app_valid_cancel_rollback();

  /* Create FreeRTOS Tasks */
  /* Task 1: UI and state machine (low priority, core 0) */
  xTaskCreatePinnedToCore(
    Task1code,   /* Task function */
    "Task1",     /* Task name */
    10000,       /* Stack size */
    NULL,        /* Parameters */
    1,           /* Priority */
    &Task1,      /* Task handle */
    0);          /* Core 0 */
    
  /* Task 2: Trigger reading and motor control (high priority, core 1) */
  xTaskCreatePinnedToCore(
    Task2code,   /* Task function */
    "Task2",     /* Task name */
    10000,       /* Stack size */
    NULL,        /* Parameters */
    2,           /* Priority */
    &Task2,      /* Task handle */
    1);          /* Core 1 */

  /* WiFiTask: web server client handling (same priority as Task1, core 0).
   * Pinned to Core 0 alongside Task1 — FreeRTOS runs only one at a time so
   * no mutex is needed. Equal priority means FreeRTOS time-slices them, giving
   * WiFiTask enough CPU for fast OTA transfers while keeping encoder responsive
   * (each task gets ~1 tick per slice instead of WiFiTask being starved). */
  xTaskCreatePinnedToCore(
    WiFiTaskcode, /* Task function */
    "WiFiTask",   /* Task name */
    8192,         /* Stack size (larger than Task1: handles HTTP file serving) */
    NULL,         /* Parameters */
    1,            /* Priority 1 — same as Task1, time-sliced fairly */
    &WiFiTask,    /* Task handle */
    0);           /* Core 0 — same core as Task1, safe without mutex */
}

void applyAdcVoltageRangeMilliVolts(uint16_t range_mV) {
  g_adcVoltageRange_mV = constrain(range_mV, ADC_VOLTAGE_RANGE_MIN_MVOLTS, ADC_VOLTAGE_RANGE_MAX_MVOLTS);
  HalfBridge_SetAdcVoltageRangeMilliVolts(g_adcVoltageRange_mV);
}


/*********************************************************************************************************************/
/*                                                FreeRTOS Tasks                                                     */
/*********************************************************************************************************************/

/**
 * @brief Task 1: UI and State Machine
 * @details Manages OLED display, rotary encoder, and main state machine
 * @note Runs on Core 0 with lower priority
 */
void Task1code(void *pvParameters) {
  for (;;) {
    StateMachine_enum prevState = g_currState;
    static uint16_t prevFreqPWM = 0;
    static MenuState_enum menuState = ITEM_SELECTION;
    static uint8_t swMajVer, swMinVer, storedVarVersion;

    /* Read motor current (voltage is read exclusively in Task2 to avoid ADC contention) */
    g_escVar.motorCurrent_mA = HAL_ReadMotorCurrent();
    serviceTimedWiFiPortal();

    /* Update selected car if initialization complete */
    if (g_currState != INIT) {
      g_carSel = g_storedVar.selectedCarNumber;
    }

    /* Task 1 state machine */
    switch (g_currState) {
      case INIT:

        g_pref.begin("stored_var", false); /* Open the "stored" namespace in read/write mode. If it doesn't exist, it creates it */

        if (g_pref.isKey("stored_var_ver") && g_pref.isKey("sw_maj_ver") && g_pref.isKey("sw_min_ver") && g_pref.isKey("user_param")) /* If all keys exists, then check their value */
        {
          /* Get the values of the sw version */
          swMajVer = g_pref.getUChar("sw_maj_ver");
          swMinVer = g_pref.getUChar("sw_min_ver");
          storedVarVersion = g_pref.getUChar("stored_var_ver");

          if ((storedVarVersion == STORED_VAR_VERSION) || canMigrateStoredVarVersion(storedVarVersion)) /* Load current storage directly, or migrate v22/v23 in-place to 0.1% BRAKE/SENSI */
          {
            g_pref.getBytes("user_param", &g_storedVar, sizeof(g_storedVar)); /* Get the value of the stored user_param */
            bool migratedToTenths = false;
            if (storedVarVersion != STORED_VAR_VERSION) {
              migrateStoredVarFromHalfPctToTenthPct(&g_storedVar);
              migratedToTenths = true;
            }
            g_statsEnabled = g_pref.getUChar(PREF_KEY_STATS_ENABLED, STATS_ENABLED_DEFAULT) ? 1 : 0;
            g_encoderInvertEnabled = g_pref.getUChar(PREF_KEY_ENC_INVERT, ENCODER_INVERT_DEFAULT) ? 1 : 0;
            applyAdcVoltageRangeMilliVolts(g_pref.getUShort(PREF_KEY_ADC_RANGE, ACD_VOLTAGE_RANGE_DEFAULT_MVOLTS));
            if (g_pref.isKey(PREF_KEY_EXT_POT1_TARGET) || g_pref.isKey(PREF_KEY_EXT_POT2_TARGET)) {
              g_extPotTarget[0] = constrain(g_pref.getUChar(PREF_KEY_EXT_POT1_TARGET, EXT_POT1_TARGET_DEFAULT),
                                            EXT_POT_TARGET_MIN, EXT_POT_TARGET_MAX);
              g_extPotTarget[1] = constrain(g_pref.getUChar(PREF_KEY_EXT_POT2_TARGET, EXT_POT2_TARGET_DEFAULT),
                                            EXT_POT_TARGET_MIN, EXT_POT_TARGET_MAX);
            } else {
              bool legacyEnabled = g_pref.getUChar(PREF_KEY_EXT_POT_ENABLED_LEGACY, 0) != 0;
              uint16_t legacyTarget = g_pref.getUChar(PREF_KEY_EXT_POT_TARGET_LEGACY, 0);
              g_extPotTarget[0] = EXT_POT1_TARGET_DEFAULT;
              g_extPotTarget[1] = EXT_POT2_TARGET_DEFAULT;
              if (legacyEnabled) {
                g_extPotTarget[0] = (legacyTarget == 0) ? EXT_POT_TARGET_BRAKE : EXT_POT_TARGET_SENSI;
              }
            }
            sanitizeExtPotTargets(0);
            resetExtPotFilter();
            {
              uint32_t storedBrakeStep = g_pref.getUShort(PREF_KEY_BRAKE_STEP, BRAKE_STEP_DEFAULT);
              uint32_t storedSensiStep = g_pref.getUShort(PREF_KEY_SENSI_STEP, SENSI_STEP_DEFAULT);
              if (storedVarVersion == 23U && g_pref.isKey(PREF_KEY_BRAKE_STEP)) {
                storedBrakeStep *= BRAKE_SCALE;          /* v23 stored BRAKE STEP as whole-percent units */
              }
              if (storedVarVersion == 23U && g_pref.isKey(PREF_KEY_SENSI_STEP)) {
                storedSensiStep *= (SENSI_SCALE / 2U);   /* v23 stored SENSI STEP as 0.5%-raw units */
              }
              g_brakeStep = constrain((uint16_t)min(storedBrakeStep, (uint32_t)UINT16_MAX),
                                      BRAKE_STEP_MIN, BRAKE_STEP_MAX);
              g_sensiStep = constrain((uint16_t)min(storedSensiStep, (uint32_t)UINT16_MAX),
                                      SENSI_STEP_MIN, SENSI_STEP_MAX);
            }
            g_antiSpinStepMs = constrain(g_pref.getUShort(PREF_KEY_ANTIS_STEP, ANTISPIN_STEP_DEFAULT),
                                         ANTISPIN_STEP_MIN, ANTISPIN_STEP_MAX);
            g_antiSpinStepPct = constrain(g_pref.getUShort(PREF_KEY_ANTIS_STEP_PCT, ANTISPIN_STEP_PCT_DEFAULT),
                                          ANTISPIN_STEP_PCT_MIN, ANTISPIN_STEP_PCT_MAX);
            g_antiSpinDisplayMode = constrain(g_pref.getUChar(PREF_KEY_ANTIS_MODE, ANTISPIN_UI_MODE_DEFAULT),
                                              ANTISPIN_UI_MODE_MS, ANTISPIN_UI_MODE_TEXT);

            /* One-time migration: old firmware stored SENSI in whole-percent units. */
            if (!migratedToTenths && !g_pref.getBool(PREF_KEY_SENSI_HALF, false)) {
              for (uint8_t i = 0; i < CAR_MAX_COUNT; i++) {
                uint32_t migrated = (uint32_t)g_storedVar.carParam[i].minSpeed * SENSI_SCALE;
                g_storedVar.carParam[i].minSpeed = (uint16_t)min((uint32_t)MIN_SPEED_MAX_VALUE, migrated);
              }
              g_pref.putBytes("user_param", &g_storedVar, sizeof(g_storedVar));
              g_pref.putBool(PREF_KEY_SENSI_HALF, true);
            }
            if (migratedToTenths) {
              g_pref.putBytes("user_param", &g_storedVar, sizeof(g_storedVar));
              g_pref.putUChar("sw_maj_ver", SW_MAJOR_VERSION);
              g_pref.putUChar("sw_min_ver", SW_MINOR_VERSION);
              g_pref.putUChar("stored_var_ver", STORED_VAR_VERSION);
              g_pref.putBool(PREF_KEY_SENSI_HALF, true);
              g_pref.putUShort(PREF_KEY_BRAKE_STEP, g_brakeStep);
              g_pref.putUShort(PREF_KEY_SENSI_STEP, g_sensiStep);
            }
            initMenuItems();                                                  /* init menu items with EEPROM stored variables */
            g_startWiFiAfterOtaBoot = consumeWiFiAutoStartOnNextBootRequest();

            /* If calibration reset was requested from settings, go straight to calibration */
            if (g_pref.getBool("force_calib", false)) {
              g_pref.putBool("force_calib", false);
              g_pref.end();
              g_currState = CALIBRATION;
              g_storedVar.minTrigger_raw = MAX_INT16;
              g_storedVar.maxTrigger_raw = MIN_INT16;
              if (g_storedVar.soundBoot) {
                calibSound();
              }
              initDisplayAndEncoder();
              obdFill(&g_obd, OBD_WHITE, 1);
              break;
            }

            /* If button is pressed at startup, go to CALIBRATION state */
            if (digitalRead(ENCODER_BUTTON_PIN) == BUTTON_PRESSED)
            {
              g_currState = CALIBRATION;      /* Go to CALIBRATION state */
              /* Reset Min and Max to the opposite side, in order to have effective calibration */
              g_storedVar.minTrigger_raw = MAX_INT16;
              g_storedVar.maxTrigger_raw = MIN_INT16;
              if (g_storedVar.soundBoot) {
                calibSound();             /* Play calibration sound */
              }
              initDisplayAndEncoder();  /* init and clear OLED and Encoder */

              /* Wait until button is released, then go to CALIBRATION state */
              while (digitalRead(ENCODER_BUTTON_PIN) == BUTTON_PRESSED)
              {
                showScreenPreCalibration();
              }
              
              obdFill(&g_obd, OBD_WHITE, 1); /* Clear OLED */
            }
            else if (digitalRead(BUTT_PIN) == BUTTON_PRESSED)
            {
              /* Hold brake button at startup → run self-test */
              g_currState = WELCOME;
              g_carSel = g_storedVar.selectedCarNumber;
              initDisplayAndEncoder();
              showSelfTest();
            }
            else  /* If button is NOT pressed at startup, go to RUNNING state */
            {
              g_currState = WELCOME;                    /* Go to WELCOME state */
              g_carSel = g_storedVar.selectedCarNumber; /* now it is safe to address the proper car */
              initDisplayAndEncoder();  /* init and clear OLED and Encoder */
              if (g_startWiFiAfterOtaBoot) {
                startTimedWiFiPortal(getWiFiTimedMinutes());
                g_startWiFiAfterOtaBoot = false;
              }
              if (g_storedVar.soundBoot) {
                onSound();                /* Play ON sound */
              }
            }

            g_pref.end(); /* Close the namespace */
            break;        /* Break the switch case: if code reaches here, it means that the stored user param and sw versions are OK */
          }
        }

        /* If the code reaches here it means that:
        - the sw version keys are not present --> stored var are not initialized
        - the sw version stored are not up to date --> stored var are initialized but might be outdated

        Calibration values are NOT stored, go to CALIBRATION state */
        initDisplayAndEncoder();  /* init and clear OLED and Encoder */
                              
        g_pref.clear();           /* Clear all the keys in this namespace */
        /* Store the correct SW version */
        g_pref.putUChar("sw_maj_ver", SW_MAJOR_VERSION);
        g_pref.putUChar("sw_min_ver", SW_MINOR_VERSION);
        
        g_pref.putUChar("stored_var_ver", STORED_VAR_VERSION);
        g_pref.putBool(PREF_KEY_SENSI_HALF, true);
        g_pref.putUChar(PREF_KEY_STATS_ENABLED, STATS_ENABLED_DEFAULT);
        g_pref.putUShort(PREF_KEY_ANTIS_STEP, ANTISPIN_STEP_DEFAULT);
        g_pref.putUShort(PREF_KEY_ANTIS_STEP_PCT, ANTISPIN_STEP_PCT_DEFAULT);
        g_pref.putUChar(PREF_KEY_ANTIS_MODE, ANTISPIN_UI_MODE_DEFAULT);
        g_pref.putUShort(PREF_KEY_BRAKE_STEP, BRAKE_STEP_DEFAULT);
        g_pref.putUShort(PREF_KEY_SENSI_STEP, SENSI_STEP_DEFAULT);
        g_pref.putUChar(PREF_KEY_ENC_INVERT, ENCODER_INVERT_DEFAULT);
        g_pref.putUShort(PREF_KEY_ADC_RANGE, ACD_VOLTAGE_RANGE_DEFAULT_MVOLTS);
        g_pref.putUChar(PREF_KEY_EXT_POT1_TARGET, EXT_POT1_TARGET_DEFAULT);
        g_pref.putUChar(PREF_KEY_EXT_POT2_TARGET, EXT_POT2_TARGET_DEFAULT);
        HAL_ResetTriggerSensorConfig();

        initStoredVariables();  /* Initialize stored variables with default values */
        g_startWiFiAfterOtaBoot = false;
        g_statsEnabled = STATS_ENABLED_DEFAULT;
        g_antiSpinStepMs = ANTISPIN_STEP_DEFAULT;
        g_antiSpinStepPct = ANTISPIN_STEP_PCT_DEFAULT;
        g_antiSpinDisplayMode = ANTISPIN_UI_MODE_DEFAULT;
        g_brakeStep = BRAKE_STEP_DEFAULT;
        g_sensiStep = SENSI_STEP_DEFAULT;
        applyAdcVoltageRangeMilliVolts(ACD_VOLTAGE_RANGE_DEFAULT_MVOLTS);
        g_extPotTarget[0] = EXT_POT1_TARGET_DEFAULT;
        g_extPotTarget[1] = EXT_POT2_TARGET_DEFAULT;
        resetExtPotFilter();

        /* Reset Min and Max to the opposite side, in order to have effective calibration */
        g_storedVar.minTrigger_raw = MAX_INT16;
        g_storedVar.maxTrigger_raw = MIN_INT16;
        if (g_storedVar.soundBoot) {
          calibSound();                   /* Play calibration sound */
        }
        g_currState = CALIBRATION;      /* Go to CALIBRATION state */
        obdFill(&g_obd, OBD_WHITE, 1); /* Clear OLED */
        /* Press and release button to go to CALIBRATION state */
        while (!g_rotaryEncoder.isEncoderButtonClicked()) /* Loop until button is pressed */
        {
          showScreenNoEEPROM();
        }

        break;


      case CALIBRATION:
        /* Read Throttle */
        throttleCalibration(g_escVar.trigger_raw);    /* trigger raw is continuously read on task2 */
        showScreenCalibration(g_escVar.trigger_raw);  /* Show calibration screen */
        /* Exit calibration if button is presseded */
        if (g_rotaryEncoder.isEncoderButtonClicked())  /* exit calibration and save calibration data to EEPROM */
        {
          if (g_storedVar.soundBoot) {
            offSound();
          }
          initMenuItems();  /* Init Menu Items */
          saveEEPROM(g_storedVar);  /* Save modified calibration values to EEPROM */
          HalfBridge_Enable();    /* Enable HalfBridge */
          g_currState = WELCOME;  /* Go to WELCOME state */
        }
        break;


      case WELCOME:
        /* This WELCOME state is only used to show the SW versions on the Screen, but the controller is actually fully operational:
            - Calibration is OKAY (either just done, or using stored values)
            - User param are OKAY (either just initialized to default, or using stored values)
            - Trigger is being read on Task 2

          The user trigger input is being elaborated and the correct speed (PWM) output is being produced for the whole duration of the WELCOME state */

        if (g_storedVar.startupDelay > 0) {
          showScreenWelcome();    /* Show welcome screen */
          delay(g_storedVar.startupDelay * STARTUP_DELAY_STEP_MS);  /* Configurable startup delay */
        }
        g_currState = RUNNING;  /* Go to RUNNING state */
        break;


      case RUNNING: { /* when the global variable State is in RUNNING the Task2 will elaborate the trigger to produce the PWM out */

        /* Set encoder boundaries based on view mode (on first entry) */
        static bool encoderBoundariesSet = false;
        if (!encoderBoundariesSet) {
          /* Force LIST mode if race view is OFF */
          if (g_storedVar.viewMode == VIEW_MODE_GRID && g_storedVar.raceViewMode == RACE_VIEW_OFF) {
            g_storedVar.viewMode = VIEW_MODE_LIST;
          }

          if (g_storedVar.viewMode == VIEW_MODE_GRID) {
            uint8_t gridItems;
            if (g_storedVar.raceViewMode == RACE_VIEW_SIMPLE) {
              gridItems = g_storedVar.gridCarSelectEnabled ? 3 : 2;  /* SIMPLE: BRAKE, SENSI, CAR (optional) */
            } else {
              gridItems = g_storedVar.gridCarSelectEnabled ? 5 : 4;  /* FULL: BRAKE, SENSI, ANTIS, CURVE, CAR (optional) */
            }
            setUiEncoderBoundaries(1, gridItems, false);
          } else {
            setUiEncoderBoundaries(1, getMainMenuItemsCount(), false);  /* Normal menu items */
          }
          encoderBoundariesSet = true;
        }

        /* Handle race mode toggle: either from a submenu escape or direct long press */
        static uint32_t lastLongPressTime = 0;
        static uint32_t buttonPressStartTime = 0;
        static uint32_t lastShortPressTime = 0;
        static bool buttonWasPressed = false;
        static bool buttonLongPressHandled = false;
        bool encoderButtonPressed = (digitalRead(ENCODER_BUTTON_PIN) == BUTTON_PRESSED);
        bool raceToggleReleaseGuardActive = serviceRaceModeToggleReleaseGuard(encoderButtonPressed);
        bool encoderShortClick = false;

        if (g_escapeToMain) {
          /* Return from submenu long press → toggle race mode immediately */
          g_escapeToMain = false;
          applyRaceModeToggle(menuState, lastLongPressTime);
          raceToggleReleaseGuardActive = true;
          buttonPressStartTime = 0;
          lastShortPressTime = 0;
          buttonWasPressed = false;
          buttonLongPressHandled = true;
        } else if (!raceToggleReleaseGuardActive) {
          /* Direct press/release handling in RUNNING, independent of the encoder library click timeout logic. */
          if (encoderButtonPressed) {
            if (!buttonWasPressed) {
              buttonPressStartTime = millis();
              buttonWasPressed = true;
              buttonLongPressHandled = false;
            }
            if (!buttonLongPressHandled &&
                millis() - buttonPressStartTime > BUTTON_LONG_PRESS_MS &&
                millis() - lastLongPressTime > BUTTON_LONG_PRESS_MS) {
              applyRaceModeToggle(menuState, lastLongPressTime);
              raceToggleReleaseGuardActive = true;
              buttonLongPressHandled = true;
            }
          } else if (buttonWasPressed) {
            uint32_t pressDuration = millis() - buttonPressStartTime;
            buttonWasPressed = false;
            buttonPressStartTime = 0;

            if (!buttonLongPressHandled &&
                pressDuration >= BUTTON_CLICK_MIN_MS &&
                (lastShortPressTime == 0 || millis() - lastShortPressTime >= BUTTON_SHORT_PRESS_DEBOUNCE_MS)) {
              encoderShortClick = true;
              lastShortPressTime = millis();
            }

            buttonLongPressHandled = false;
          }
        } else {
          buttonPressStartTime = 0;
          buttonWasPressed = false;
          buttonLongPressHandled = false;
        }

        /* Check for brake button press - acts as "back" in edit mode */
        static bool brakeButtonWasPressedInMenu = false;
        static uint32_t lastBrakeButtonPressTime = 0;

        /* In GRID mode: brake button exits edit mode (in addition to reducing brake) */
        if (g_storedVar.viewMode == VIEW_MODE_GRID && menuState == VALUE_SELECTION && digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
          if (!brakeButtonWasPressedInMenu && millis() - lastBrakeButtonPressTime > BUTTON_SHORT_PRESS_DEBOUNCE_MS) {
            brakeButtonWasPressedInMenu = true;
            lastBrakeButtonPressTime = millis();

            /* Restore original value (cancel changes) */
            if (g_encoderSelectedValuePtr != NULL) {
              *g_encoderSelectedValuePtr = g_originalValueBeforeEdit;
              g_encoderSelectedValuePtr = NULL;
            }
            endSteppedValueEdit();

            menuState = ITEM_SELECTION;
            g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);

            uint8_t gridItems;
            if (g_storedVar.raceViewMode == RACE_VIEW_SIMPLE) {
              gridItems = g_storedVar.gridCarSelectEnabled ? 3 : 2;
            } else {
              gridItems = g_storedVar.gridCarSelectEnabled ? 5 : 4;
            }
            setUiEncoderBoundaries(1, gridItems, false);
            resetUiEncoder(g_encoderMainSelector);
            g_escVar.encoderPos = g_encoderMainSelector;
            g_isEditingCarSelection = false;
            obdFill(&g_obd, OBD_WHITE, 1);
            g_lastEncoderInteraction = millis();
          }
        }
        else if (g_storedVar.viewMode == VIEW_MODE_LIST && digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
          if (!brakeButtonWasPressedInMenu && millis() - lastBrakeButtonPressTime > BUTTON_SHORT_PRESS_DEBOUNCE_MS) {
            brakeButtonWasPressedInMenu = true;
            lastBrakeButtonPressTime = millis();

            /* If in VALUE_SELECTION, cancel changes and go back to ITEM_SELECTION */
            if (menuState == VALUE_SELECTION) {
              /* Restore original value (cancel changes) */
              if (g_encoderSelectedValuePtr != NULL) {
                *g_encoderSelectedValuePtr = g_originalValueBeforeEdit;
                g_encoderSelectedValuePtr = NULL;  /* Clear pointer after use */
              }
              endSteppedValueEdit();

              menuState = ITEM_SELECTION;
              g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
              setUiEncoderBoundaries(1, getMainMenuItemsCount(), false);
              resetUiEncoder(g_encoderMainSelector);
              g_escVar.encoderPos = g_encoderMainSelector;
              /* Clear car editing flag */
              g_isEditingCarSelection = false;
              /* Do NOT save to EEPROM - we're canceling the change */
              obdFill(&g_obd, OBD_WHITE, 1);  /* Clear screen */
              g_lastEncoderInteraction = millis();
            }
            /* If already in ITEM_SELECTION, brake button doesn't do anything in main menu */
          }
        }
        /* Reset flag when button is released */
        if (digitalRead(BUTT_PIN) != BUTTON_PRESSED) {
          brakeButtonWasPressedInMenu = false;
        }

        /* Configurable brake hold in ITEM_SELECTION toggles settings lock */
        if (menuState == ITEM_SELECTION && g_storedVar.lockShortcutIdx > 0) {
          static uint32_t brakeLockPressStartMs = 0;
          static bool brakeLockHandled = false;
          uint32_t holdMs = (uint32_t)g_storedVar.lockShortcutIdx * 1000UL;
          bool brakeHeld = (digitalRead(BUTT_PIN) == BUTTON_PRESSED);
          if (brakeHeld) {
            if (brakeLockPressStartMs == 0) brakeLockPressStartMs = millis();
            if (!brakeLockHandled && (millis() - brakeLockPressStartMs >= holdMs)) {
              brakeLockHandled = true;
              toggleSettingsLock();
            }
          } else {
            brakeLockPressStartMs = 0;
            brakeLockHandled = false;
          }
        }

        /* Change menu state if encoder button is clicked (short press) */
        if (encoderShortClick)
        {
          /* If screensaver is active (timeout exceeded), just wake up - don't process menu action */
          if (g_storedVar.screensaverTimeout > 0 && millis() - g_lastEncoderInteraction > (g_storedVar.screensaverTimeout * 1000UL)) {
            g_lastEncoderInteraction = millis();  /* Wake from screensaver */
          } else {
            menuState = rotary_onButtonClick(menuState);  /* This function is called if the encoder is pressed
                                                             Take the current menu state and returns the next menu state */
            g_lastEncoderInteraction = millis();          /* Update last encoder interaction */
          }
        }

        /* Get encoder position if it was changed*/
        if (g_rotaryEncoder.encoderChanged()) 
        {
          g_escVar.encoderPos = readUiEncoder();  /* Update the logical encoder position used by the UI */
          g_lastEncoderInteraction = millis();  /* Update last encoder interaction */
        }

        /* Change Encoder variable depending on menu state */
        if (menuState == ITEM_SELECTION) 
        {
          g_encoderMainSelector = g_escVar.encoderPos;  /* If in ITEM_SELECTION, update the encoder MainSelector with the encoder position */
        }
        if (menuState == VALUE_SELECTION)
        {
          g_encoderSecondarySelector = g_escVar.encoderPos;         /* If in ITEM_SELECTION, update the encoder SecondaryEncoder with the encoder position */
          uint16_t prevLanguage = g_storedVar.language;             /* Store previous language for change detection */
          applyValueEditFromEncoder(g_encoderSecondarySelector);    /* Update the selected parameter from encoder input */

          /* If language changed, reinitialize menu items with new language strings */
          if (g_storedVar.language != prevLanguage) {
            initMenuItems();
            obdFill(&g_obd, OBD_WHITE, 1);  /* Clear screen to prevent leftover text */
          }
        }

        /* Show Main Menu display */
        printMainMenu(menuState);
        if (g_storedVar.carParam[g_carSel].freqPWM != prevFreqPWM)  /* if PWM freq parameter is changed, update motor PWM */
        {
          prevFreqPWM = g_storedVar.carParam[g_carSel].freqPWM;
          ledcDetach(HB_IN_PIN);
          ledcDetach(HB_INH_PIN);
          uint16_t freqTmp = g_storedVar.carParam[g_carSel].freqPWM * 100;
          ledcAttachChannel(HB_IN_PIN, freqTmp, THR_PWM_RES_BIT, THR_IN_PWM_CHAN);
          ledcAttachChannel(HB_INH_PIN, freqTmp, THR_PWM_RES_BIT, THR_INH_PWM_CHAN);
        }
        if (g_escVar.outputSpeed_pct == 100) /* indicate 100% throttle also on the internal ESP32 LED*/
          digitalWrite(LED_BUILTIN, 1);
        else
          digitalWrite(LED_BUILTIN, 0);
        break;
      }


      case FAULT:
        /* FAULT state, not currently used */
        break;


      default:
        /* Default case, do nothing */
        break;
    }

    if (g_currState != prevState) /* Every time FSM machine change state */
      obdFill(&g_obd, OBD_WHITE, 1);

    vTaskDelay(1); /* Yield to WiFiTask (priority 0) every iteration */
  }
}

/**
 * @brief WiFiTask: dedicated web server client handler
 * @details Calls handleClient() at lowest priority on Core 0 so it only runs
 *          in Task1's vTaskDelay gaps. This prevents large SPIFFS file transfers
 *          from blocking the encoder and state machine in Task1.
 */
void WiFiTaskcode(void *pvParameters) {
  for (;;) {
    serviceWiFiPortal();
    vTaskDelay(1);
  }
}

/**
 * @brief Main loop (unused - real loops are in FreeRTOS tasks)
 */
void loop() {}

/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*                                             Screen Functions                                                      */
/*********************************************************************************************************************/

/**
 * Show the Welcome screen
 */
void showScreenWelcome()
{
  /* Display "ESPEED" in large font */
  obdWriteString(&g_obd, 0, 24, 14, (char *)"ESPEED", FONT_12x16, OBD_BLACK, 1);

  /* Display version in smaller font, centered below with spacing */
  sprintf(msgStr, "v%d.%d", SW_MAJOR_VERSION, SW_MINOR_VERSION);
  uint8_t versionWidth = strlen(msgStr) * 6;  /* 6px per char for FONT_6x8 */
  obdWriteString(&g_obd, 0, (OLED_WIDTH - versionWidth) / 2, 36, msgStr, FONT_6x8, OBD_BLACK, 1);
}


/**
 * Show the Pre-Calibration screen, when the button is pressed during startup, but not yet released
 */
void showScreenPreCalibration() 
{
  sprintf(msgStr, "ESPEED32 v%d.%d", SW_MAJOR_VERSION, SW_MINOR_VERSION);  //print SW version
  obdWriteString(&g_obd, 0, 8, 0, msgStr, FONT_8x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, (OLED_WIDTH / 2) - 48, 1 * HEIGHT8x8, (char *)"Release button", FONT_8x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, (OLED_WIDTH / 2) - 48, 2 * HEIGHT8x8, (char *)"to calibrate", FONT_8x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, (OLED_WIDTH / 2) - 48, 3 * HEIGHT8x8, (char *)"     OR    ", FONT_8x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, (OLED_WIDTH / 2) - 48, 4 * HEIGHT8x8, (char *)"remove power", FONT_8x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, (OLED_WIDTH / 2) - 48, 5 * HEIGHT8x8, (char *)"  to exit  ", FONT_8x8, OBD_BLACK, 1);
}


/**
 * Show the screen indicating that the stored variables are not present in the EEPROM
 */
void showScreenNoEEPROM() 
{
  sprintf(msgStr, "ESPEED32 v%d.%d", SW_MAJOR_VERSION, SW_MINOR_VERSION);  //print SW version
  obdWriteString(&g_obd, 0, 0, 0, msgStr, FONT_8x8, OBD_WHITE, 1);
  obdWriteString(&g_obd, 0, (OLED_WIDTH / 2) - 64, 3 * HEIGHT8x8, (char *)"EEPROM NOT init!", FONT_8x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, (OLED_WIDTH / 2) - 48, 5 * HEIGHT8x8, (char *)"Press button", FONT_8x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, (OLED_WIDTH / 2) - 48, 6 * HEIGHT8x8, (char *)"to calibrate", FONT_8x8, OBD_BLACK, 1);
}


/**
 * Show the calibration screen. It displays the current trigger raw value, as well as the max and min encountered values.
 * 
 * @param adcRaw The raw trigger value read from the ADC
 */
void showScreenCalibration(int16_t adcRaw)
{
  sprintf(msgStr, "%s", STR_CALIBRATION[g_storedVar.language]);
  int calWidth = strlen(msgStr) * 6;
  obdWriteString(&g_obd, 0, (OLED_WIDTH - calWidth) / 2, 0, msgStr, FONT_6x8, OBD_WHITE, 1);

  sprintf(msgStr, "%s", STR_PRESS_RELEASE[g_storedVar.language]);
  obdWriteString(&g_obd, 0, 0, 8, msgStr, FONT_6x8, OBD_BLACK, 1);

  sprintf(msgStr, "Raw throttle %4d  ", adcRaw);
  obdWriteString(&g_obd, 0, 0, 24, msgStr, FONT_6x8, OBD_BLACK, 1);

  sprintf(msgStr, "Min throttle %4d   ", g_storedVar.minTrigger_raw);
  obdWriteString(&g_obd, 0, 0, 32, msgStr, FONT_6x8, OBD_BLACK, 1);

  sprintf(msgStr, "Max throttle %4d   ", g_storedVar.maxTrigger_raw);
  obdWriteString(&g_obd, 0, 0, 40, msgStr, FONT_6x8, OBD_BLACK, 1);

  sprintf(msgStr, "%s", STR_PUSH_DONE[g_storedVar.language]);
  obdWriteString(&g_obd, 0, 0, 56, msgStr, FONT_6x8, OBD_BLACK, 1);
}

#define ANTIS_SPEED_START_MIN 30  /* [%] Start anti-spin only for throttle requests above this threshold */
#define ANTIS_SPEED_START_MAX 65  /* [%] Maximum anti-spin start threshold */

/**
 * @brief Apply anti-spin control to prevent car drift
 * @details Applies a ramp to output speed to prevent sudden speed changes that cause wheel spin.
 *          Anti-spin time is the ramp time from the anti-spin start point up toward maxSpeed.
 *          It is not a pure trigger delay.
 *          Anti-spin is bypassed at low duty cycles (< antispinPercStart) where current is too low to cause spinning.
 *          The antispinPercStart threshold varies with the antiSpin setting:
 *          - High antiSpin (200ms) = Low threshold (powerful motor/slippery track)
 *          - Low antiSpin = High threshold (good traction)
 * 
 * @param requestedSpeedX10 [0.1%] Requested output speed (0-1000)
 * @return [0.1%] Actual output speed respecting anti-spin ramp limits
 */
uint16_t throttleAntiSpin3(uint16_t requestedSpeedX10) {
  static uint32_t lastOutputSpeedx1000 = 0;
  static unsigned long prevCall_uS = 0;
  
  unsigned long currCall_uS = micros();
  unsigned long deltaTime_uS = currCall_uS - prevCall_uS;
  prevCall_uS = currCall_uS;

  /* Calculate dynamic anti-spin start threshold based on anti-spin setting */
  uint16_t antispinPercStart = map((long)g_storedVar.carParam[g_carSel].antiSpin, 0, (long)ANTISPIN_MAX_VALUE, 
                                    (long)ANTIS_SPEED_START_MAX, (long)ANTIS_SPEED_START_MIN);
  uint16_t antispinPercStartRaw = antispinPercStart * SENSI_SCALE;

  /* Bypass anti-spin if disabled */
  if (g_storedVar.carParam[g_carSel].antiSpin == 0) {
    lastOutputSpeedx1000 = (uint32_t)getEffectiveSensiRaw() * 100U;
    return requestedSpeedX10;
  }

  /* Bypass anti-spin at low speeds (insufficient current for wheel spin) */
  if (requestedSpeedX10 < antispinPercStartRaw) {
    lastOutputSpeedx1000 = (uint32_t)requestedSpeedX10 * 100U;
    return requestedSpeedX10;
  }

  /* Allow immediate deceleration (braking) */
  if ((uint32_t)requestedSpeedX10 * 100U <= lastOutputSpeedx1000) {
    lastOutputSpeedx1000 = (uint32_t)requestedSpeedX10 * 100U;
    return requestedSpeedX10;
  }

  /* Apply anti-spin ramp for acceleration */
  uint16_t minSpeedTmpRaw = max(getEffectiveSensiRaw(), antispinPercStartRaw);
  uint16_t maxSpeedRaw = g_storedVar.carParam[g_carSel].maxSpeed * SENSI_SCALE;
  if (maxSpeedRaw <= minSpeedTmpRaw) {
    lastOutputSpeedx1000 = (uint32_t)requestedSpeedX10 * 100U;
    return requestedSpeedX10;
  }
  
  /* Convert the 0.1%-unit span into x1000-percent ramp units.
   * deltaTime is in us while antiSpin is in ms, so denominator is antiSpin * scale. */
  uint32_t maxDeltaSpeedx1000 = ((uint32_t)(maxSpeedRaw - minSpeedTmpRaw) * deltaTime_uS) /
                                 ((uint32_t)g_storedVar.carParam[g_carSel].antiSpin * (uint32_t)SENSI_SCALE);

  uint32_t outputSpeedX1000;
  uint32_t targetSpeedX1000 = (uint32_t)requestedSpeedX10 * 100U;
  
  /* Check if we can increase speed by full delta or if we're close to target */
  if (maxDeltaSpeedx1000 < (targetSpeedX1000 - lastOutputSpeedx1000)) {
    outputSpeedX1000 = lastOutputSpeedx1000 + maxDeltaSpeedx1000;
  } else {
    outputSpeedX1000 = targetSpeedX1000;  /* Target reached */
  }

  /* Ensure ramp starts from minSpeed, not zero */
  if (outputSpeedX1000 < (uint32_t)getEffectiveSensiRaw() * 100U) {
    outputSpeedX1000 = (uint32_t)getEffectiveSensiRaw() * 100U;
  }

  lastOutputSpeedx1000 = outputSpeedX1000;
  return (uint16_t)((outputSpeedX1000 + 50U) / 100U);
}


/**
 * Accounts for a deadband in an input value.
 * 
 * @param inputVal The input value
 * @param minVal The lower bound of the input value range
 * @param maxVal The upper bound of the input value range
 * @param deadBand The deadband as absolute value (not a percentage)
 * @return If the input value is less than the minVal + deadBand, it returns minVal.
           If the input value is more than the maxVal - deadBand, it returns the maxVal.
           Otherwise, it returns the inputVal, scaled in order to range from minVal to maxVal.
 */
uint16_t addDeadBand(uint16_t inputVal, uint16_t minVal, uint16_t maxVal, uint16_t deadBand) 
{
  uint16_t retVal = 0;

  /* If the inputVal is less than deadBand_pct, return 0 */
  if (inputVal < minVal + deadBand) 
  {
    retVal = 0;
  } 
  /* If the inputVal is more than maxValue - deadBand, return maxValue */
  else if (inputVal > maxVal - deadBand) 
  {
    retVal = maxVal;
  } 
  else 
  {
    /* retVal = (THROTTLE_NORMALIZED * (inputVal - deadBand)) / (THROTTLE_NORMALIZED - 2 * deadBand); TODO: verify what this is suppose to do */
    retVal = map(inputVal, deadBand, maxVal - deadBand, minVal, maxVal);  /* Scale the inputValue (which ranges from (minVal + deadBand) to (maxVal - deadBand))
                                                                             so that it ranges from minVal to maxVal */
  }

  return retVal;
}

/**
 * throttleCurve2: Map trigger position(throttle) to speed (duty) on a broken line curve, with midpoint set as throttleCurveVertex
 * dual throttle curve: When decelerating , if dragB is higher than 100%-minSpeed, then set a lower minSpeed
 * @param inputThrottleNorm The input Trigger value, normalized between 0 and THROTTLE_NORMALIZED
 * @return duty cycle in 0.1% units to be applied at that trigger position on the selected curve
 */
uint16_t throttleCurve2(uint16_t inputThrottleNorm )
{
  uint32_t throttleCurveVertexSpeedRaw;  /* Output speed in 0.1% units at the curve vertex */
  uint16_t outputSpeedRaw = 0;           /* Output speed in 0.1% units */
  uint16_t tmpMinSpeedRaw;               /* Minimum speed in 0.1% units */
  uint16_t maxSpeedRaw;                  /* Maximum speed in 0.1% units */
  uint16_t fadeThrottleNorm;
  uint16_t curveVertexInputNorm;

  /* dual throttle curve: When decelerating , if dragB is higher than 100%-minSpeed, then set a lower minSpeed on the curve to allow the desired drag brake to be applied*/
  tmpMinSpeedRaw = getEffectiveSensiRaw();
  maxSpeedRaw = g_storedVar.carParam[g_carSel].maxSpeed * SENSI_SCALE;
  if (maxSpeedRaw < tmpMinSpeedRaw) {
    maxSpeedRaw = tmpMinSpeedRaw;
  }
  fadeThrottleNorm = fadePctToThrottleNorm(min((uint16_t)FADE_MAX_VALUE, g_storedVar.carParam[g_carSel].fade));
  curveVertexInputNorm = curveVertexInputWithFade(fadeThrottleNorm, g_storedVar.carParam[g_carSel].throttleCurveVertex.inputThrottle);

  /* Calculate the output speed of the throttle curve vertex
     This is calculated as the curveSpeedDiff (from 10% to 90%) percentage of the difference between minSpeed and maxSpeed */
  throttleCurveVertexSpeedRaw = tmpMinSpeedRaw + (((uint32_t)maxSpeedRaw - (uint32_t)tmpMinSpeedRaw) * ((uint32_t)g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff) / 100);

  if (inputThrottleNorm == 0)   /* If input throttle is 0 --> output speed is 0% */
  {
    outputSpeedRaw = 0;
  }
  else if (fadeThrottleNorm > 0 && inputThrottleNorm <= fadeThrottleNorm)
  {
    /* FADE fills the gap between 0 output and SENSI over the first part of trigger travel. */
    outputSpeedRaw = (uint16_t)map(inputThrottleNorm, 0, fadeThrottleNorm, 0, tmpMinSpeedRaw);
  }
  else if (inputThrottleNorm <= curveVertexInputNorm)
  {
    if (curveVertexInputNorm <= fadeThrottleNorm)
    {
      outputSpeedRaw = (uint16_t)throttleCurveVertexSpeedRaw;
    }
    else
    {
      outputSpeedRaw = (uint16_t)map(inputThrottleNorm, fadeThrottleNorm, curveVertexInputNorm, tmpMinSpeedRaw, throttleCurveVertexSpeedRaw);
    }
  }
  else
  {
    if (curveVertexInputNorm >= THROTTLE_NORMALIZED)
    {
      outputSpeedRaw = maxSpeedRaw;
    }
    else
    {
      outputSpeedRaw = (uint16_t)map(inputThrottleNorm, curveVertexInputNorm, THROTTLE_NORMALIZED, throttleCurveVertexSpeedRaw, maxSpeedRaw);
    }
  }

  return outputSpeedRaw;
}

/**
 * Call this when calibrating the throttle.
 * Check if the parameter adcRaw is bigger/smaller than the stored max/min values, and updates them accordingly.
 * @param inputThrottle The raw ADC value
 */
void throttleCalibration(int16_t adcRaw)
{
  if (g_storedVar.maxTrigger_raw < adcRaw)
    {
      g_storedVar.maxTrigger_raw = adcRaw;
    }
  if (g_storedVar.minTrigger_raw > adcRaw)
    {
      g_storedVar.minTrigger_raw = adcRaw;
    }
}


/**
 * Reacts to the rotary encoder being clicked.
 * If the menu is in ITEM_SELECTION, it must select the correct item (update the g_encoderSelectedValuePtr) or invoking the item's callback --> then return the next state VALUE_SELECTION
 * If the menu is in VALUE_SELECTION, it must return to ITEM_SELECTION
 * 
 * @param currMenuState The current menu state.
 * @return The next menu state.
 */
MenuState_enum rotary_onButtonClick(MenuState_enum currMenuState)
{
  static unsigned long lastTimePressed = 0;
  static uint16_t selectedParamMaxValue = 100;
  static uint16_t selectedParamMinValue = 0;

  /* Ignore multiple presses that are too close together */
  if (millis() - lastTimePressed < BUTTON_SHORT_PRESS_DEBOUNCE_MS)
  {
    return currMenuState;
  }

  lastTimePressed = millis();

  if (currMenuState == ITEM_SELECTION) /* If the current state is ITEM_SELECTION */
  {
    /* Settings lock: block all edits except the LOCK item itself */
    if (g_settingsLocked) {
      bool isLockItem = (g_storedVar.viewMode == VIEW_MODE_LIST &&
                         g_mainMenu.item[g_encoderMainSelector - 1].callback == &toggleSettingsLock);
      if (!isLockItem) return ITEM_SELECTION;
    }

    /* Special handling for GRID mode - map grid item to car parameter */
    if (g_storedVar.viewMode == VIEW_MODE_GRID)
    {
      uint8_t gridItem = g_encoderMainSelector - 1;  /* 0-4 (FULL) or 0-2 (SIMPLE) */
      g_rotaryEncoder.setAcceleration(SEL_ACCELERATION);

      /* Map grid item to parameter pointer and limits based on race view mode */
      if (g_storedVar.raceViewMode == RACE_VIEW_SIMPLE) {
        /* SIMPLE mode: 0=BRAKE, 1=SENSI, 2=CAR */
        switch (gridItem) {
          case 0:  /* BRAKE */
            g_encoderSelectedValuePtr = &g_storedVar.carParam[g_carSel].brake;
            selectedParamMaxValue = BRAKE_MAX_VALUE;
            selectedParamMinValue = 0;
            break;
          case 1:  /* SENSI (minSpeed) */
            g_encoderSelectedValuePtr = &g_storedVar.carParam[g_carSel].minSpeed;
            selectedParamMaxValue = getSelectedSensiMaxRaw();
            selectedParamMinValue = 0;
            break;
          case 2:  /* CAR - select different car */
            g_encoderSelectedValuePtr = &g_storedVar.selectedCarNumber;
            selectedParamMaxValue = CAR_MAX_COUNT - 1;
            selectedParamMinValue = 0;
            g_isEditingCarSelection = true;  /* Prevent g_carSel update during edit */
            break;
          default:
            return ITEM_SELECTION;
        }
      }
      else {
        /* FULL mode: 0=BRAKE, 1=SENSI, 2=ANTIS, 3=CURVE, 4=CAR */
        switch (gridItem) {
          case 0:  /* BRAKE */
            g_encoderSelectedValuePtr = &g_storedVar.carParam[g_carSel].brake;
            selectedParamMaxValue = BRAKE_MAX_VALUE;
            selectedParamMinValue = 0;
            break;
          case 1:  /* SENSI (minSpeed) */
            g_encoderSelectedValuePtr = &g_storedVar.carParam[g_carSel].minSpeed;
            selectedParamMaxValue = getSelectedSensiMaxRaw();
            selectedParamMinValue = 0;
            break;
          case 2:  /* ANTIS (antiSpin) */
            g_encoderSelectedValuePtr = &g_storedVar.carParam[g_carSel].antiSpin;
            selectedParamMaxValue = antiSpinUiMaxValue(g_antiSpinDisplayMode);
            selectedParamMinValue = 0;
            break;
          case 3:  /* CURVE */
            g_encoderSelectedValuePtr = &g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff;
            selectedParamMaxValue = THROTTLE_CURVE_SPEED_DIFF_MAX_VALUE;
            selectedParamMinValue = THROTTLE_CURVE_SPEED_DIFF_MIN_VALUE;
            break;
          case 4:  /* CAR - select different car */
            g_encoderSelectedValuePtr = &g_storedVar.selectedCarNumber;
            selectedParamMaxValue = CAR_MAX_COUNT - 1;
            selectedParamMinValue = 0;
            g_isEditingCarSelection = true;  /* Prevent g_carSel update during edit */
            break;
          default:
            return ITEM_SELECTION;
        }
      }

      g_originalValueBeforeEdit = *g_encoderSelectedValuePtr;  /* Save original value for cancel before any stepped-edit snapping */
      uint16_t startValue = configureEncoderForSelectedValueEdit(selectedParamMinValue, selectedParamMaxValue);
      g_escVar.encoderPos = startValue;
      beginSteppedValueEdit();
      return VALUE_SELECTION;
    }
    /* LIST mode handling */
    /* If an item has no callback, go in menu state VALUE_SELECTION */
    else if (g_mainMenu.item[g_encoderMainSelector - 1].callback == ITEM_NO_CALLBACK)
    {
      g_rotaryEncoder.setAcceleration(SEL_ACCELERATION); /* Set higher encoder acceleration so it doesn't require too many turns to make a big value change */


      g_encoderSelectedValuePtr = (uint16_t *)g_mainMenu.item[g_encoderMainSelector - 1].value;   /* Update the g_encoderSelectedValuePtr to point to the value of the selected item.
                                                                                                     value is a generic pointer to void, so cast to uint16_t pointer */
      selectedParamMaxValue = g_mainMenu.item[g_encoderMainSelector - 1].maxValue;                /* Set Max and Min boundaries according to the selected items max and min value */
      selectedParamMinValue = g_mainMenu.item[g_encoderMainSelector - 1].minValue;
      g_originalValueBeforeEdit = *g_encoderSelectedValuePtr;  /* Save original value for cancel before any stepped-edit snapping */
      uint16_t startValue = configureEncoderForSelectedValueEdit(selectedParamMinValue, selectedParamMaxValue);
      g_escVar.encoderPos = startValue;   /* Set the encoderPos global variable to the current value of the selected item */
      beginSteppedValueEdit();
      return VALUE_SELECTION;                             /* Return the VALUE_SELECTION state */
    }
    /* if an item has a callback, execute it, then return to ITEM SELECTION */
    else
    {
      g_mainMenu.item[g_encoderMainSelector - 1].callback();  /* Invoke the selected item's callback */
      return ITEM_SELECTION;
    }
  }
  else /* If the current state is VALUE_SELECTION */
  {
    endSteppedValueEdit();
    g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);         /* Set the encoder acceleration to MENU_ACCELERATION */

    /* Set boundaries based on view mode */
    if (g_storedVar.viewMode == VIEW_MODE_GRID) {
      uint8_t gridItems;
      if (g_storedVar.raceViewMode == RACE_VIEW_SIMPLE) {
        gridItems = g_storedVar.gridCarSelectEnabled ? 3 : 2;  /* SIMPLE: BRAKE, SENSI, CAR (optional) */
      } else {
        gridItems = g_storedVar.gridCarSelectEnabled ? 5 : 4;  /* FULL: BRAKE, SENSI, ANTIS, CURVE, CAR (optional) */
      }
      setUiEncoderBoundaries(1, gridItems, false);
    } else {
      setUiEncoderBoundaries(1, getMainMenuItemsCount(), false);  /* Set the encoder boundaries to the menu boundaries */
    }

    resetUiEncoder(g_encoderMainSelector);               /* Reset the encoder value to g_encoderMainSelector, so that it doesn't change the selected item */
    g_escVar.encoderPos = g_encoderMainSelector;

    g_isEditingCarSelection = false;  /* Clear flag - editing complete */
    saveEEPROM(g_storedVar);  /* Save modified values to EEPROM */
    return ITEM_SELECTION;    /* Return the ITEM_SELECTION state */
  }
}

/**
 * Take an ADC raw value where the max and min has been recorded and returns the value scaled
 * 
 * @param raw The raw ADC value
 * @param minIn The lowest reading of the ADC
 * @param maxIn The highest reading of the ADC
 * @param normalizedMax The new max value of the scale
 * @param isReversed Whether the throttle is reversed or not
 * @return The raw ADC value scaled from 0 to normalizedMax
 */
uint16_t normalizeAndClamp(uint16_t raw, uint16_t minIn, uint16_t maxIn, uint16_t normalizedMax, bool isReversed)
{
  uint16_t retVal = 0;  /* From 0 to normalizedMax */

  if (maxIn == minIn)  /* If maxIn == minIn avoid division by 0 */
  {
    retVal = 0;
  }
  else
  {
    raw = constrain(raw, minIn, maxIn); /* Make sure the raw value is constrained between minIn and maxIn */

    if (isReversed == true)  /* If throttle is reversed (it goes to low values when pressed) */
    {
      raw = abs(maxIn - raw);
    }
    else
    {
      raw = abs(raw - minIn);
    }

    retVal = ((uint32_t)raw * normalizedMax) / (maxIn - minIn); /* Scale the raw reading */
  }

  return retVal;
}

/**
 * Saturate an input value between a upper and lower bound
 * 
 * @param paramValue The input value to be saturated
 * @param minValue The lower bound
 * @param maxValue The upper bound
 * @return The saturated input value.
 */
uint16_t saturateParamValue(uint16_t paramValue, uint16_t minValue, uint16_t maxValue) 
{
  uint16_t retValue = paramValue;

  if (paramValue > maxValue) 
  {
    retValue = maxValue;
  } else if (paramValue < minValue) 
  {
    retValue = minValue;
  }

  return retValue;
}


void saveEEPROM(StoredVar_type toSave) {
  g_pref.begin("stored_var", false);                      /* Open the "stored" namespace in read/write mode */
  g_pref.putBytes("user_param", &toSave, sizeof(toSave)); /* Put the value of the stored user_param */
  g_pref.putUChar(PREF_KEY_STATS_ENABLED, g_statsEnabled ? 1 : 0);
  g_pref.putUShort(PREF_KEY_ANTIS_STEP, constrain(g_antiSpinStepMs, ANTISPIN_STEP_MIN, ANTISPIN_STEP_MAX));
  g_pref.putUShort(PREF_KEY_ANTIS_STEP_PCT, constrain(g_antiSpinStepPct, ANTISPIN_STEP_PCT_MIN, ANTISPIN_STEP_PCT_MAX));
  g_pref.putUChar(PREF_KEY_ANTIS_MODE, constrain(g_antiSpinDisplayMode, ANTISPIN_UI_MODE_MS, ANTISPIN_UI_MODE_TEXT));
  g_pref.putUShort(PREF_KEY_BRAKE_STEP, constrain(g_brakeStep, BRAKE_STEP_MIN, BRAKE_STEP_MAX));
  g_pref.putUShort(PREF_KEY_SENSI_STEP, constrain(g_sensiStep, SENSI_STEP_MIN, SENSI_STEP_MAX));
  g_pref.putUChar(PREF_KEY_ENC_INVERT, g_encoderInvertEnabled ? 1 : 0);
  g_pref.putUShort(PREF_KEY_ADC_RANGE, constrain(g_adcVoltageRange_mV, ADC_VOLTAGE_RANGE_MIN_MVOLTS, ADC_VOLTAGE_RANGE_MAX_MVOLTS));
  g_pref.putUChar(PREF_KEY_EXT_POT1_TARGET, constrain(g_extPotTarget[0], EXT_POT_TARGET_MIN, EXT_POT_TARGET_MAX));
  g_pref.putUChar(PREF_KEY_EXT_POT2_TARGET, constrain(g_extPotTarget[1], EXT_POT_TARGET_MIN, EXT_POT_TARGET_MAX));
  g_pref.end();                                           /* Close the namespace */
  saveWiFiNetworkSettings();
}
