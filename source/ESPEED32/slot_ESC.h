#ifndef SLOT_ESC_H_
#define SLOT_ESC_H_

/*********************************************************************************************************************/
/*                                                   Includes                                                        */
/*********************************************************************************************************************/
#include <OneBitDisplay.h>
#include <AiEsp32RotaryEncoder.h>
#include <AiEsp32RotaryEncoderNumberSelector.h>
#include "half_bridge.h"
#include "HAL.h"
#include <Preferences.h>

/*********************************************************************************************************************/
/*                                              Configuration Constants                                             */
/*********************************************************************************************************************/

/* Firmware Version */
#define SW_MAJOR_VERSION 6
#define SW_MINOR_VERSION 9
#define SW_PATCH_VERSION 2

/* Stored Variable Version */
#define STORED_VAR_VERSION 24 /* Increment when stored value scale/format changes */

/* Menu Configuration */
#define MENU_ITEMS_COUNT    12    /* Number of possible items in main menu (including optional STATS, LOCK and CAR) */
#define SETTINGS_ITEMS_COUNT 12   /* Number of items in settings menu (including BACK) */
#define POWER_ITEMS_COUNT    6    /* Number of items in power submenu (SCRSV, SLEEP, D-SLEEP, STARTUP, VIN CAL., BACK) */
#define DISPLAY_ITEMS_COUNT  7    /* Number of items in display submenu (VIEW, LANG, CASE, FSIZE, STEPS, STATUS, BACK) */
#define STEPS_ITEMS_COUNT    4    /* Number of items in steps submenu (ANTISPIN, BRAKE STEP, SENSI STEP, BACK) */
#define HARDWARE_ITEMS_COUNT 5    /* Number of items in hardware submenu (ENC INV, EXT POT, TRIGGER, TEST, BACK) */
#define POWER_SAVE_TIMEOUT_DEFAULT 5    /* [min] Default auto power save delay (0=manual only) */
#define POWER_SAVE_TIMEOUT_MAX     10   /* [min] Maximum auto power save delay */
#define DEEP_SLEEP_TIMEOUT_DEFAULT 10   /* [min] Default auto deep sleep delay (0=manual only) */
#define DEEP_SLEEP_TIMEOUT_MAX     30   /* [min] Maximum auto deep sleep delay */
#define DEEP_SLEEP_TIMEOUT_MIN     2    /* [min] Minimum auto deep sleep delay */
#define MENU_ACCELERATION   0     /* Encoder acceleration in menu navigation */
#define SEL_ACCELERATION    100   /* Encoder acceleration when adjusting values */
#define ITEM_NO_CALLBACK    0     /* Indicates menu item has no callback function */
#define ITEM_NO_VALUE       0     /* Indicates menu item has no displayable value */
#define MAX_ITEMS           20    /* Maximum items in any menu */

/* View Modes */
#define VIEW_MODE_LIST      0     /* List view (classic menu) */
#define VIEW_MODE_GRID      1     /* Grid view (race mode) */

/* Race Mode View Types */
#define RACE_VIEW_OFF       0     /* No race mode (list view only) */
#define RACE_VIEW_FULL      1     /* Full race mode (4 parameters: BRAKE, SENSI, ANTIS, CURVE) */
#define RACE_VIEW_SIMPLE    2     /* Simple race mode (2 parameters: BRAKE, SENSI) */
#define RACE_VIEW_DEFAULT   RACE_VIEW_FULL

/* Language Settings */
#define LANG_NOR            0     /* Norwegian language */
#define LANG_ENG            1     /* English language */
#define LANG_CS             2     /* CarSteen terminology (English-based) */
#define LANG_ACD            3     /* ACD terminology (English-based) */
#define LANG_ESP            4     /* Spanish language */
#define LANG_DEU            5     /* German language */
#define LANG_ITA            6     /* Italian language */
#define LANG_NLD            7     /* Dutch language */
#define LANG_POR            8     /* Portuguese language */
#define LANG_MAX            LANG_POR
#define LANG_DEFAULT        LANG_ENG

/* Default Parameter Values */
#define BRAKE_SCALE              10     /* 0.1% resolution: stored value = percent * 10 */
#define SENSI_SCALE              10     /* 0.1% resolution: stored value = percent * 10 */
#define MIN_SPEED_DEFAULT        200    /* [0.1%] 20.0% default sensitivity */
#define BRAKE_DEFAULT            950    /* [0.1%] 95.0% brake strength */
#define ANTISPIN_DEFAULT          30    /* [ms] Anti-spin ramp time */
#define ANTISPIN_STEP_DEFAULT      5    /* [ms] Default encoder step when editing ANTIS */
#define ANTISPIN_STEP_PCT_DEFAULT  1    /* [%] Default encoder step when editing ANTIS in percent mode */
#define ANTISPIN_UI_MODE_DEFAULT   0    /* Default ANTIS UI/edit scale = ms */
#define ENCODER_INVERT_DEFAULT     0    /* Encoder rotation follows default hardware direction */
#define MAX_SPEED_DEFAULT         100   /* [%] Maximum motor speed */
#define THROTTLE_CURVE_INPUT_THROTTLE_DEFAULT   (THROTTLE_NORMALIZED / 2)  /* Throttle curve vertex X */
#define THROTTLE_CURVE_SPEED_DIFF_DEFAULT       50                          /* Throttle curve vertex Y */
#define FADE_DEFAULT              0     /* [%] Fade disabled by default for legacy behavior */
#define PWM_FREQ_DEFAULT          40    /* [100*Hz] Motor PWM frequency (4.0 kHz) */
#define BRAKE_BUTTON_REDUCTION_DEFAULT  0    /* [%] Alt brake off by default (0 = no effect) */
#define QUICK_BRAKE_ENABLED_DEFAULT     0    /* Quick brake off by default */
#define QUICK_BRAKE_THRESHOLD_DEFAULT   10   /* [%] Trigger position where quick brake engages */
#define QUICK_BRAKE_STRENGTH_DEFAULT    60   /* [%] Brake force in quick brake zone */

/* Parameter Limits */
#define MIN_SPEED_MAX_VALUE       900   /* [0.1%] 90.0% max sensitivity */
#define DRAG_MAX_VALUE            100   /* [%] Maximum drag brake */
#define FREQ_MAX_VALUE            5000  /* [Hz] Maximum PWM frequency */
#define BRAKE_MAX_VALUE          1000   /* [0.1%] Maximum brake strength */
#define THROTTLE_CURVE_SPEED_DIFF_MAX_VALUE  90   /* [%] Throttle curve max */
#define THROTTLE_CURVE_SPEED_DIFF_MIN_VALUE  10   /* [%] Throttle curve min */
#define FADE_MAX_VALUE            30    /* [%] Maximum trigger travel used for fade-in to SENSI */
#define ANTISPIN_MAX_VALUE        999   /* [ms] Maximum anti-spin time */
#define ANTISPIN_STEP_MIN           1   /* [ms] Minimum ANTIS encoder step */
#define ANTISPIN_STEP_MAX          50   /* [ms] Maximum ANTIS encoder step */
#define ANTISPIN_STEP_PCT_MIN       1   /* [%] Minimum ANTIS encoder step in percent mode */
#define ANTISPIN_STEP_PCT_MAX     100   /* [%] Maximum ANTIS encoder step in percent mode */
/* Brake encoder step (raw units, 1 unit = 0.1%) */
#define BRAKE_STEP_DEFAULT         10   /* [0.1% raw] Default brake encoder step (1.0%) */
#define BRAKE_STEP_MIN              1   /* [0.1% raw] Minimum brake step (0.1%) */
#define BRAKE_STEP_MAX            500   /* [0.1% raw] Maximum brake step (50.0%) */
/* Sensi encoder step (raw units, 1 unit = 0.1%) */
#define SENSI_STEP_DEFAULT         10   /* [0.1% raw] Default sensi encoder step (1.0%) */
#define SENSI_STEP_MIN              1   /* [0.1% raw] Minimum sensi step (0.1%) */
#define SENSI_STEP_MAX             50   /* [0.1% raw] Maximum sensi step (5.0%) */
#define FREQ_MIN_VALUE            1000  /* [Hz] Minimum PWM frequency */
#define QUICK_BRAKE_THRESHOLD_MAX 50    /* [%] Maximum quick brake threshold */
#define QUICK_BRAKE_STRENGTH_MAX  100   /* [%] Maximum quick brake strength */
#define RELEASE_BRAKE_OFF          0    /* Release brake disabled */
#define RELEASE_BRAKE_QUICK        1    /* Release brake uses full quick-brake cut */
#define RELEASE_BRAKE_DRAG         2    /* Release brake blends output with drag while releasing */
#define MAX_UINT16                32767 /* Maximum 16-bit unsigned value */

/* ANTIS UI/edit display modes */
#define ANTISPIN_UI_MODE_MS        0
#define ANTISPIN_UI_MODE_PERCENT   1
#define ANTISPIN_UI_MODE_TEXT      2

/* ANTIS text-level display/edit values */
#define ANTISPIN_TEXT_OFF          0
#define ANTISPIN_TEXT_LOW          1
#define ANTISPIN_TEXT_MED          2
#define ANTISPIN_TEXT_HIGH         3

static inline uint16_t antiSpinPercentToMs(uint16_t percent) {
  if (percent > 100U) percent = 100U;
  return (uint16_t)((((uint32_t)percent * (uint32_t)ANTISPIN_MAX_VALUE) + 50U) / 100U);
}

static inline uint16_t antiSpinMsToPercent(uint16_t antiSpinMs) {
  if (antiSpinMs > ANTISPIN_MAX_VALUE) antiSpinMs = ANTISPIN_MAX_VALUE;
  return (uint16_t)((((uint32_t)antiSpinMs * 100U) + ((uint32_t)ANTISPIN_MAX_VALUE / 2U)) / (uint32_t)ANTISPIN_MAX_VALUE);
}

static inline uint16_t antiSpinTextLevelToMs(uint16_t level) {
  switch (level) {
    case ANTISPIN_TEXT_OFF:
      return 0;
    case ANTISPIN_TEXT_LOW:
      return antiSpinPercentToMs(33U);
    case ANTISPIN_TEXT_MED:
      return antiSpinPercentToMs(66U);
    case ANTISPIN_TEXT_HIGH:
    default:
      return ANTISPIN_MAX_VALUE;
  }
}

static inline uint16_t antiSpinMsToTextLevel(uint16_t antiSpinMs) {
  if (antiSpinMs == 0U) {
    return ANTISPIN_TEXT_OFF;
  }

  uint16_t percent = antiSpinMsToPercent(antiSpinMs);
  if (percent <= 33U) return ANTISPIN_TEXT_LOW;
  if (percent <= 66U) return ANTISPIN_TEXT_MED;
  return ANTISPIN_TEXT_HIGH;
}

static inline uint16_t antiSpinUiMaxValue(uint16_t uiMode) {
  switch (uiMode) {
    case ANTISPIN_UI_MODE_PERCENT:
      return 100U;
    case ANTISPIN_UI_MODE_TEXT:
      return ANTISPIN_TEXT_HIGH;
    case ANTISPIN_UI_MODE_MS:
    default:
      return ANTISPIN_MAX_VALUE;
  }
}

static inline uint16_t antiSpinMsToUiValue(uint16_t antiSpinMs, uint16_t uiMode) {
  switch (uiMode) {
    case ANTISPIN_UI_MODE_PERCENT:
      return antiSpinMsToPercent(antiSpinMs);
    case ANTISPIN_UI_MODE_TEXT:
      return antiSpinMsToTextLevel(antiSpinMs);
    case ANTISPIN_UI_MODE_MS:
    default:
      return (antiSpinMs > ANTISPIN_MAX_VALUE) ? ANTISPIN_MAX_VALUE : antiSpinMs;
  }
}

static inline uint16_t antiSpinUiValueToMs(uint16_t uiValue, uint16_t uiMode) {
  switch (uiMode) {
    case ANTISPIN_UI_MODE_PERCENT:
      return antiSpinPercentToMs(uiValue);
    case ANTISPIN_UI_MODE_TEXT:
      return antiSpinTextLevelToMs(uiValue);
    case ANTISPIN_UI_MODE_MS:
    default:
      return (uiValue > ANTISPIN_MAX_VALUE) ? ANTISPIN_MAX_VALUE : uiValue;
  }
}

static inline const char* antiSpinTextLevelToLabel(uint16_t level) {
  switch (level) {
    case ANTISPIN_TEXT_OFF:  return "OFF";
    case ANTISPIN_TEXT_LOW:  return "LOW";
    case ANTISPIN_TEXT_MED:  return "MED";
    case ANTISPIN_TEXT_HIGH:
    default:                 return "HIGH";
  }
}

/* Display Font Sizes */
#define HEIGHT12x16     16
#define HEIGHT8x8       8
#define WIDTH8x8        8
#define WIDTH12x16      12

/* Timing Constants */
#define ESC_PERIOD_US   500     /* ESC control loop period [µs] */
#define SCREENSAVER_TIMEOUT_DEFAULT  60  /* [s] Default screensaver timeout */
#define SCREENSAVER_TIMEOUT_MIN      1   /* [s] Minimum screensaver timeout (0 = OFF) */
#define SCREENSAVER_TIMEOUT_MAX      240 /* [s] Maximum screensaver timeout */
#define SCREENSAVER_WAKEUP_THRESHOLD 20  /* [%] Throttle threshold to wake from screensaver */
#define STARTUP_DELAY_STEP_MS       10  /* [ms] Step size for startupDelay setting */
#define STARTUP_DELAY_DEFAULT       0   /* [×STEP] Default startup delay (0 = immediate) */
#define STARTUP_DELAY_MIN           0   /* [×STEP] Minimum (0 = skip welcome) */
#define STARTUP_DELAY_MAX           99  /* [×STEP] Maximum (99 = 990ms with 10ms step) */

/* Button Press Timing */
#define BUTTON_LONG_PRESS_MS        1000  /* [ms] Duration to trigger long press (view mode toggle) */
#define BUTTON_DEBOUNCE_AFTER_LONG_MS   200  /* [ms] Require release plus short debounce after a long press */
#define BUTTON_CLICK_MIN_MS           30  /* [ms] Minimum hold time to treat a press/release as a real click */
#define BUTTON_SHORT_PRESS_DEBOUNCE_MS  200  /* [ms] Minimum time between button presses */
#define BUTTON_LOCK_HOLD_MS            5000  /* [ms] Brake hold duration to toggle settings lock */

/* Sound Configuration */
#define SOUND_BOOT_DEFAULT  1  /* Boot sounds on by default (startup, calib, on, off) */
#define SOUND_RACE_DEFAULT  1  /* Race mode toggle sound on by default */

/* Shared runtime helpers */
void applyAdcVoltageRangeMilliVolts(uint16_t range_mV);
#define SOUND_ITEMS_COUNT   3  /* Items in sound submenu: BOOT, RACE, BACK */
#define LOCK_ITEMS_COUNT    4  /* Items in lock submenu: MENU ITEM, SHORTCUT, CONFIRM, BACK */
#define LOCK_SHORTCUT_COUNT 10 /* Shortcut duration options: OFF, 1s, 2s, …, 10s (index = seconds) */
#define LOCK_MENU_ENABLED_DEFAULT  0  /* Hide LOCK menu item by default */
#define LOCK_SHORTCUT_IDX_DEFAULT  5  /* Default shortcut: 5s (index = seconds) */
#define LOCK_CONFIRM_DEFAULT       0  /* Hide fullscreen confirmation flash by default */
#define GRID_CAR_SELECT_DEFAULT 1  /* Grid car select (RACESWP): 0=OFF, 1=ON */
#define STATS_ENABLED_DEFAULT    0  /* Hide STATS menu item by default */
#define EXT_POT_COUNT            2  /* Two optional external ADC pots: GPIO35 and GPIO15 */
#define EXT_POT_TARGET_OFF       0
#define EXT_POT_TARGET_BRAKE     1
#define EXT_POT_TARGET_SENSI     2
#define EXT_POT_TARGET_MIN       EXT_POT_TARGET_OFF
#define EXT_POT_TARGET_MAX       EXT_POT_TARGET_SENSI
#define EXT_POT1_TARGET_DEFAULT  EXT_POT_TARGET_OFF
#define EXT_POT2_TARGET_DEFAULT  EXT_POT_TARGET_OFF

/* Text Case Style */
#define TEXT_CASE_UPPER     0  /* BRAKE, SENSI, etc. */
#define TEXT_CASE_PASCAL    1  /* Brake, Sensi, etc. */
#define TEXT_CASE_DEFAULT   TEXT_CASE_PASCAL

/* List View Font Size */
#define FONT_SIZE_LARGE     0  /* 12x16 font (default) */
#define FONT_SIZE_SMALL     1  /* 8x8 font (compact) */
#define FONT_SIZE_DEFAULT   FONT_SIZE_LARGE

/* Car Configuration */
#define CAR_MAX_COUNT       20  /* Maximum number of car profiles */
#define CAR_NAME_MAX_SIZE   5   /* Car name length (4 chars + null terminator) */
#define SCREENSAVER_TEXT_MAX 22  /* Screensaver text max length (21 chars + null, fits FONT_6x8 line) */
#define SCREENSAVER_LINE1_DEFAULT "ESPEED32"   /* Default screensaver line 1 */
#define SCREENSAVER_LINE2_DEFAULT "Controller" /* Default screensaver line 2 */

/* Menu Options */
#define CAR_OPTION_SELECT   0
#define CAR_OPTION_RENAME   1
#define CAR_OPTION_GRID_SEL 2
#define CAR_OPTION_COPY     3
#define CAR_OPTION_RESET    4
#define CAR_OPTION_BACK     5

/* Rename Car Mode States */
#define RENAME_CAR_SELECT_OPTION_MODE   0
#define RENAME_CAR_SELECT_CHAR_MODE     1
#define RENAME_CAR_MIN_ASCII    32      /* First printable ASCII character */
#define RENAME_CAR_MAX_ASCII    122     /* Last lowercase letter */

/* Status Line Slot Configuration
 * 4 fixed columns at x=0,32,64,96 (32px each). Content is 5 chars (30px) per slot.
 * Blank slots leave their column empty; adjacent slots do not shift. */
#define STATUS_SLOTS    4   /* Number of fixed positional columns in the status bar */
/* Content type for each slot (value stored in statusSlot[]) */
#define STATUS_BLANK    0   /* Empty – column unused */
#define STATUS_OUTPUT   1   /* Motor output % (5 chars, e.g. " 75%O") */
#define STATUS_THROTTLE 2   /* Trigger input % (5 chars, e.g. " 75%T") */
#define STATUS_CAR      3   /* Car name (5 chars, e.g. "CAR1 ") */
#define STATUS_CURRENT  4   /* Hybrid motor current (5 chars, e.g. "850mA" / "2.7A ") */
#define STATUS_VOLTAGE  5   /* Input voltage (5 chars, e.g. " 3.7V") */
#define STATUS_CURRENT_MA  6   /* Legacy stored value; normalized to STATUS_CURRENT */
#define STATUS_ACTIVE_BRAKE 7   /* Active brake type/value (5 chars, e.g. "Q060%") */

static inline uint16_t normalizeStatusSlotValue(uint16_t slotValue) {
  if (slotValue == STATUS_CURRENT_MA) return STATUS_CURRENT;
  return (slotValue <= STATUS_ACTIVE_BRAKE) ? slotValue : STATUS_BLANK;
}
/* Default slot assignments: OUTPUT | CAR | VOLTAGE | blank */
#define STATUS_SLOT0_DEFAULT STATUS_OUTPUT
#define STATUS_SLOT1_DEFAULT STATUS_THROTTLE
#define STATUS_SLOT2_DEFAULT STATUS_CAR
#define STATUS_SLOT3_DEFAULT STATUS_VOLTAGE

/* Active brake runtime state */
#define ACTIVE_BRAKE_NONE  0   /* No brake command is currently active */
#define ACTIVE_BRAKE_BASE  1   /* Normal trigger-released brake */
#define ACTIVE_BRAKE_ALT   2   /* Alternate brake while brake button is held */
#define ACTIVE_BRAKE_QUICK 3   /* Release-brake QUICK mode is active */
#define ACTIVE_BRAKE_DRAG  4   /* Release-brake DRAG mode is active */

/* Lap Detection */
#define LAP_MAX_COUNT        20    /* Max stored lap times */
#define LAP_MIN_TIME_MS      3000  /* Minimum lap time [ms] */
#define LAP_GAP_MAX_MS       200   /* Max dead spot duration [ms] */
#define LAP_TRIGGER_ACTIVE_PCT      8    /* Current-based lap detect is active above this throttle [%] */
#define LAP_CURRENT_BASE_MIN_MA   150    /* Minimum rolling current baseline to arm current-based detect [mA] */
#define LAP_CURRENT_GAP_MIN_MA     80    /* Absolute low-current floor considered as dead spot [mA] */
#define LAP_CURRENT_RECOVER_MIN_MA 140   /* Absolute current needed to confirm recovery after dead spot [mA] */
/*********************************************************************************************************************/
/*                                                 Data Structures                                                   */
/*********************************************************************************************************************/

/**
 * @brief Main state machine states
 */
typedef enum { 
  INIT,         /* Initialization state */
  CALIBRATION,  /* Trigger calibration mode */
  WELCOME,      /* Welcome screen display */
  RUNNING,      /* Normal operation */
  FAULT         /* Fault state (currently unused) */
} StateMachine_enum;

/* Shared UI encoder helpers: values are mirrored automatically when ENC INV is enabled. */
long readUiEncoder();
void resetUiEncoder(long logicalValue);
void setUiEncoderBoundaries(long minValue, long maxValue, bool circleValues);
void applyEncoderInvertSetting(uint16_t enabled);

/**
 * @brief Menu navigation states
 */
typedef enum {
  ITEM_SELECTION,   /* Selecting which menu item to modify */
  VALUE_SELECTION   /* Adjusting the value of selected item */
} MenuState_enum;

/**
 * @brief Menu item value types
 * @note Decimal values: Value is stored as integer and divided by 10^decimalPoint for display
 *       Example: PWM freq 3.0 kHz is stored as 30 with decimalPoint=1
 */
typedef enum {
  VALUE_TYPE_INTEGER,   /* Whole number value */
  VALUE_TYPE_DECIMAL,   /* Decimal value (stored as integer) */
  VALUE_TYPE_STRING     /* String value */
} ItemValueType_enum;

/**
 * @brief Throttle curve vertex definition
 * @details The throttle curve consists of two line segments connected at a vertex point.
 *          X-axis = input throttle (0-100%), Y-axis = output speed (0-100%)
 */
typedef struct {
  uint16_t inputThrottle;      /* X coordinate of vertex (fixed at 50% by default) */
  uint16_t curveSpeedDiff;     /* Y coordinate as percentage of min-max speed difference */
} ThrottleCurveVertex_type;

/**
 * @brief Car profile parameters
 * @details Contains all ESC behavior settings for a specific car/track configuration
 */
typedef struct {
  uint16_t minSpeed;                        /* [0.1%] Minimum motor speed (0.0-90.0%) */
  uint16_t brake;                           /* [0.1%] Brake strength (0.0-100.0%) */
  uint16_t maxSpeed;                        /* [%] Maximum motor speed (5-100%) */
  ThrottleCurveVertex_type throttleCurveVertex;  /* Throttle response curve */
  uint16_t fade;                            /* [%] Initial trigger zone that ramps from 0 to SENSI */
  uint16_t antiSpin;                        /* [ms] Anti-spin ramp time (0-999ms) */
  char carName[CAR_NAME_MAX_SIZE];          /* Car profile name (4 chars + null) */
  uint16_t carNumber;                       /* Profile index in array */
  uint16_t freqPWM;                         /* [100*Hz] Motor PWM frequency */
  uint16_t brakeButtonReduction;            /* [%] Alternate brake value when button pressed (0-100%) */
  uint16_t quickBrakeEnabled;              /* Release brake mode: OFF / QUICK / DRAG */
  uint16_t quickBrakeThreshold;            /* [%] Release-brake zone near trigger release */
  uint16_t quickBrakeStrength;             /* [%] Release-brake level (quick or drag) */
} CarParam_type;

/**
 * @brief Non-volatile stored variables
 * @details Parameters saved to flash memory (Preferences library)
 */
typedef struct {
  CarParam_type carParam[CAR_MAX_COUNT];    /* Array of car profiles */
  uint16_t selectedCarNumber;               /* Currently active car profile */
  int16_t minTrigger_raw;                   /* Calibrated minimum trigger value */
  int16_t maxTrigger_raw;                   /* Calibrated maximum trigger value */
  uint16_t viewMode;                        /* View mode: LIST or GRID */
  uint16_t screensaverTimeout;              /* [s] Screensaver timeout in seconds */
  uint16_t soundBoot;                       /* Boot sounds: 0=OFF, 1=ON (startup, calib, on, off) */
  uint16_t soundRace;                       /* Race mode sound: 0=OFF, 1=ON (toggle beep) */
  uint16_t gridCarSelectEnabled;            /* Enable car selection from grid view (0=OFF, 1=ON) */
  uint16_t raceViewMode;                    /* Race view mode: OFF, FULL, or SIMPLE */
  uint16_t language;                        /* Language: NOR, ENG, CS, ACD, ESP, DEU, ITA, NLD, or POR */
  uint16_t textCase;                        /* Text case style: UPPER or PASCAL */
  uint16_t listFontSize;                    /* List view font size: LARGE or SMALL */
  uint16_t startupDelay;                    /* [×STEP] Startup welcome screen delay */
  char screensaverLine1[SCREENSAVER_TEXT_MAX];  /* Screensaver main text (FONT_16x32) */
  char screensaverLine2[SCREENSAVER_TEXT_MAX];  /* Screensaver subtitle (FONT_6x8) */
  /* Status line slot assignments (see STATUS_* defines) */
  uint16_t statusSlot[STATUS_SLOTS]; /* content type for each fixed-position column */
  uint16_t powerSaveTimeout;         /* [min] Auto power save delay (0=manual only, 1-10 min) */
  uint16_t deepSleepTimeout;         /* [min] Auto deep sleep delay (0=manual only, 2-30 min) */
  uint16_t lockMenuEnabled;          /* Show LOCK item in main menu: 0=OFF, 1=ON */
  uint16_t lockShortcutIdx;          /* Brake hold shortcut: 0=OFF, 1=1s, 2=2s, …, 10=10s */
  uint16_t lockConfirmEnabled;       /* Show fullscreen LOCKED/UNLOCKED flash: 0=OFF, 1=ON */
} StoredVar_type;

/**
 * @brief ESC runtime variables
 * @details Real-time state variables updated during operation
 */
typedef struct {
  uint16_t outputSpeed_pct;   /* [%] Current motor duty cycle (0-100%) */
  int16_t trigger_raw;        /* Raw trigger sensor reading */
  uint16_t trigger_norm;      /* Normalized trigger value (0-THROTTLE_NORMALIZED) */
  uint16_t encoderPos;        /* Current rotary encoder position */
  uint16_t Vin_mV;            /* [mV] Input voltage */
  uint16_t motorCurrent_mA;   /* [mA] Motor current */
  uint16_t effectiveBrake_raw; /* [0.1%] Active brake value after external overrides */
  uint16_t effectiveSensi_raw; /* [0.1%] Active SENSI value after external overrides */
  uint8_t activeBrakeKind;     /* ACTIVE_BRAKE_* runtime state */
  uint8_t activeBrake_pct;     /* [%] Brake value currently being applied */
  /* Lap detection */
  uint16_t lapCount;                    /* Total laps completed */
  uint32_t lapTimes[LAP_MAX_COUNT];     /* Circular buffer: last 20 lap times [ms] */
  uint32_t bestLapTime_ms;              /* Best lap time [ms] */
  uint32_t lapStartTime_ms;             /* When current lap started [ms] */
} ESC_type;

/**
 * @brief Function pointer type for menu callbacks
 */
typedef void (*FunctionPointer_type)(void);

/**
 * @brief Menu item definition
 * @details Describes a single item in the menu system
 */
typedef struct {
  char name[10];                  /* Item name displayed in menu */
  void *value;                    /* Pointer to value (cast to appropriate type) */
  ItemValueType_enum type;        /* Value type for display formatting */
  uint16_t maxValue;              /* Maximum allowed value */
  uint16_t minValue;              /* Minimum allowed value */
  char unit[4];                   /* Measurement unit string (up to 3 chars + null) */
  uint8_t decimalPoint;           /* Decimal point position (1 or 2) */
  FunctionPointer_type callback;  /* Function called when item is selected */
} MenuItem_type;

/**
 * @brief Menu structure
 */
typedef struct {
  MenuItem_type item[MAX_ITEMS];  /* Array of menu items */
  uint16_t lines;                 /* Number of visible lines */
} Menu_type;

/* 0.1% conversion helpers */
static inline uint16_t pctX10ToWholePctRounded(uint16_t pctX10) {
  return (pctX10 + 5U) / 10U;
}

static inline uint16_t brakeToWholePctFloor(uint16_t brakeRaw) {
  return brakeRaw / BRAKE_SCALE;
}

static inline uint16_t brakeToWholePctRounded(uint16_t brakeRaw) {
  return pctX10ToWholePctRounded(brakeRaw);
}

static inline uint16_t brakeToPctX10(uint16_t brakeRaw) {
  return brakeRaw;
}

static inline uint8_t brakeFracDigit(uint16_t brakeRaw) {
  return (uint8_t)(brakeRaw % BRAKE_SCALE);
}

static inline uint16_t sensiToWholePctFloor(uint16_t sensiRaw) {
  return sensiRaw / SENSI_SCALE;
}

static inline uint16_t fadePctToThrottleNorm(uint16_t fadePct) {
  if (fadePct >= 100U) {
    return THROTTLE_NORMALIZED;
  }
  return (uint16_t)(((uint32_t)fadePct * (uint32_t)THROTTLE_NORMALIZED) / 100U);
}

static inline uint16_t curveVertexInputWithFade(uint16_t fadeThrottleNorm, uint16_t baseCurveInputNorm) {
  if (fadeThrottleNorm >= THROTTLE_NORMALIZED) {
    return THROTTLE_NORMALIZED;
  }
  return (uint16_t)(fadeThrottleNorm +
                    (((uint32_t)(THROTTLE_NORMALIZED - fadeThrottleNorm) * (uint32_t)baseCurveInputNorm) /
                     (uint32_t)THROTTLE_NORMALIZED));
}

static inline uint16_t sensiToWholePctCeil(uint16_t sensiRaw) {
  return (sensiRaw + (SENSI_SCALE - 1)) / SENSI_SCALE;
}

static inline uint16_t sensiToPctX10(uint16_t sensiRaw) {
  return sensiRaw;
}

static inline uint8_t sensiFracDigit(uint16_t sensiRaw) {
  return (uint8_t)(sensiRaw % SENSI_SCALE);
}

static inline uint16_t sensiFromWholePct(uint16_t pct) {
  return pct * SENSI_SCALE;
}

#endif  /* SLOT_ESC_H_ */
