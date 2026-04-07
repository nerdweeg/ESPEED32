#ifndef HAL_H_
#define HAL_H_

/*********************************************************************************************************************/
/*                                                   Includes                                                        */
/*********************************************************************************************************************/
#include <Wire.h>
#include <Arduino.h>

/*********************************************************************************************************************/
/*                                             Hardware Definitions                                                 */
/*********************************************************************************************************************/

/* Display Configuration */
#define MY_OLED     OLED_128x64
#define OLED_WIDTH  128
#define OLED_HEIGHT 64
#define USE_BACKBUFFER  /* Comment out if not enough RAM for back buffer */

/* Voltage Divider and Shunt Resistors */
#define RVIFBL 2200UL    /* [Ohm] VIN ADC resistor divider, lower resistor */
#define RVIFBH 10000UL   /* [Ohm] VIN ADC resistor divider, upper resistor */

/* ADC Configuration */
#define THROTTLE_NORMALIZED         256
#define THROTTLE_DEADBAND_PERC      3   /* [%] Throttle deadband percentage */
#define THROTTLE_DEADBAND_NORM      ((THROTTLE_DEADBAND_PERC * THROTTLE_NORMALIZED) / 100)
#define THROTTLE_NOISE_PERC         2
#define THROTTLE_NOISE_NORM         ((THROTTLE_NOISE_PERC * THROTTLE_NORMALIZED) / 100)
#define ACD_RESOLUTION_STEPS        4095

/* ADC Voltage Calibration */
#define VIN_CAL_SET  1200
#define VIN_CAL_READ 1108
#define ACD_VOLTAGE_RANGE_DEFAULT_MVOLTS  ((3300 * VIN_CAL_SET) / VIN_CAL_READ)  /* Default ADC voltage range */
#define ADC_VOLTAGE_RANGE_MIN_MVOLTS     2500U
#define ADC_VOLTAGE_RANGE_MAX_MVOLTS     4500U
#define VIN_CAL_TARGET_MIN_MVOLTS        8000U
#define VIN_CAL_TARGET_MAX_MVOLTS        20000U
#define VIN_CAL_TARGET_STEP_MVOLTS       10U

/* Motor Current Sense Profiles
 * Keep BTN99X0 as the default. Custom builders can switch this to NONE
 * or add another profile implementation later (for example BTS7960). */
#define CURRENT_SENSE_PROFILE_BTN99X0  0
#define CURRENT_SENSE_PROFILE_NONE     1
#define CURRENT_SENSE_PROFILE_BTS7960  2
#ifndef CURRENT_SENSE_PROFILE
#define CURRENT_SENSE_PROFILE CURRENT_SENSE_PROFILE_BTN99X0
#endif
#ifndef BTN99X0_CURRENT_SENSE_MA_PER_V
#define BTN99X0_CURRENT_SENSE_MA_PER_V 7752UL
#endif
/* BTS7960 / IBT_2 current-sense defaults.
 * These are only an initial approximation and may need adjustment
 * to match the actual module's IS resistor/filter network. */
#ifndef BTS7960_CURRENT_SENSE_KILIS
#define BTS7960_CURRENT_SENSE_KILIS               8500UL  /* Datasheet-typical kILIS starting point */
#endif
#ifndef BTS7960_CURRENT_SENSE_EFFECTIVE_R_OHMS
#define BTS7960_CURRENT_SENSE_EFFECTIVE_R_OHMS    1000UL  /* Effective IS load seen by the ADC input */
#endif
#ifndef BTS7960_CURRENT_SENSE_OFFSET_MV
#define BTS7960_CURRENT_SENSE_OFFSET_MV           0UL     /* Optional offset trim for module-specific idle bias */
#endif

#define MAX_INT16   32767   /* Maximum int16 value for calibration */
#define MIN_INT16   -32768  /* Minimum int16 value for calibration */

/* PWM Configuration */
#define THR_IN_PWM_CHAN   0     /* PWM channel for motor input */
#define THR_INH_PWM_CHAN  1     /* PWM channel for motor inhibit */
#define BUZZ_CHAN         6     /* PWM channel for buzzer */
#define THR_PWM_RES_BIT   10    /* Motor PWM resolution: 10-bit gives ~0.1% duty granularity */
#define THR_PWM_MAX_DUTY  ((1U << THR_PWM_RES_BIT) - 1U)

/* Trigger Sensor Family Selection
 * Select exactly one sensor family at compile time.
 * For TLE493D, the concrete variant/address is auto-detected at runtime in HAL.cpp
 * (for example W2B6, W2B6_A0, or P3B6).
 *
 * Default build keeps TLE493D selected.
 *
 * Recommended compile-time override:
 *   -DTRIGGER_SENSOR_FAMILY=TRIGGER_SENSOR_FAMILY_AS5600
 *
 * Legacy per-family defines such as AS5600_MAG / TLE493D_MAG are still accepted. */
#define TRIGGER_SENSOR_FAMILY_AS5600   1
#define TRIGGER_SENSOR_FAMILY_TLE493D  2
#define TRIGGER_SENSOR_FAMILY_AS5600L  3
#define TRIGGER_SENSOR_FAMILY_ANALOG   4
#define TRIGGER_SENSOR_FAMILY_MT6701   5

#define TRIGGER_SENSOR_TYPE_AUTO       0
#define TRIGGER_SENSOR_TYPE_W2B6       1
#define TRIGGER_SENSOR_TYPE_W2B6_A0    2
#define TRIGGER_SENSOR_TYPE_P3B6       3
#define TRIGGER_SENSOR_TYPE_MAX        TRIGGER_SENSOR_TYPE_P3B6

#ifndef TRIGGER_SENSOR_FAMILY
  #if defined(AS5600_MAG)
    #define TRIGGER_SENSOR_FAMILY TRIGGER_SENSOR_FAMILY_AS5600
  #elif defined(TLE493D_MAG)
    #define TRIGGER_SENSOR_FAMILY TRIGGER_SENSOR_FAMILY_TLE493D
  #elif defined(AS5600L_MAG)
    #define TRIGGER_SENSOR_FAMILY TRIGGER_SENSOR_FAMILY_AS5600L
  #elif defined(ANALOG_TRIG)
    #define TRIGGER_SENSOR_FAMILY TRIGGER_SENSOR_FAMILY_ANALOG
  #elif defined(MT6701_MAG)
    #define TRIGGER_SENSOR_FAMILY TRIGGER_SENSOR_FAMILY_MT6701
  #else
    #define TRIGGER_SENSOR_FAMILY TRIGGER_SENSOR_FAMILY_TLE493D
  #endif
#endif

#if !defined(AS5600_MAG) && !defined(TLE493D_MAG) && !defined(AS5600L_MAG) && !defined(ANALOG_TRIG) && !defined(MT6701_MAG)
  #if TRIGGER_SENSOR_FAMILY == TRIGGER_SENSOR_FAMILY_AS5600
    #define AS5600_MAG      /* AMS AS5600 magnetic trigger sensor */
  #elif TRIGGER_SENSOR_FAMILY == TRIGGER_SENSOR_FAMILY_TLE493D
    #define TLE493D_MAG     /* Infineon TLE493D family (runtime variant auto-detect) */
  #elif TRIGGER_SENSOR_FAMILY == TRIGGER_SENSOR_FAMILY_AS5600L
    #define AS5600L_MAG     /* AMS AS5600L variant with different I2C address */
  #elif TRIGGER_SENSOR_FAMILY == TRIGGER_SENSOR_FAMILY_ANALOG
    #define ANALOG_TRIG     /* Analog potentiometer trigger */
  #elif TRIGGER_SENSOR_FAMILY == TRIGGER_SENSOR_FAMILY_MT6701
    #define MT6701_MAG      /* MagnTek MT6701 magnetic trigger sensor */
  #else
    #error "Unsupported TRIGGER_SENSOR_FAMILY"
  #endif
#endif

/* Trigger Reversal Configuration */
#if defined(AS5600_MAG) || defined(AS5600L_MAG)
  #define THROTTLE_REV  1  /* 1 = trigger inverted (full press = minimum ADC value) */
#elif defined(MT6701_MAG)
  #define THROTTLE_REV  1
#elif defined(ANALOG_TRIG)
  #define THROTTLE_REV  1
#elif defined(TLE493D_MAG)
  #define THROTTLE_REV  0  /* 0 = trigger normal (full press = maximum ADC value) */
#endif

/* Miscellaneous */
#define KEY_SOUND_MS      50
#define BUTTON_PRESSED    0

/*********************************************************************************************************************/
/*                                                 Pin Definitions                                                   */
/*********************************************************************************************************************/

/* I2C Pins */
#define SDA0_PIN    21    /* I2C #1 SDA: Magnetic sensor */
#define SCL0_PIN    22    /* I2C #1 SCL: Magnetic sensor */
#define SDA1_PIN    33    /* I2C #2 SDA: OLED display */
#define SCL1_PIN    32    /* I2C #2 SCL: OLED display */

/* OLED Display Configuration */
#define RESET_PIN   -1    /* Set to -1 to disable external reset */
#define OLED_ADDR   -1    /* Let OneBitDisplay auto-detect address */
#define FLIP180     0     /* Don't rotate display 180° */
#define INVERT_DISP 0     /* Don't invert display colors */
#define USE_HW_I2C  1     /* Use hardware I2C (not bit-bang) */

/* Analog Input Pins */
#define AN_VIN_DIV  36    /* Voltage divider input */
#define EXT_POT1_PIN 35   /* Optional external pot #1 input (ADC1) */
#define EXT_POT2_PIN 15   /* Optional external pot #2 input (ADC2; may be unreliable while WiFi is active) */

/* Rotary Encoder Pins */
#define ENCODER_A_PIN      16  /* Encoder signal A (S1) */
#define ENCODER_B_PIN      17  /* Encoder signal B (S2) */
#define ENCODER_BUTTON_PIN 4   /* Encoder button (KEY) */
#define ENCODER_VCC_PIN    -1  /* Set to -1 if encoder VCC connected directly to 3.3V */
#define ENCODER_STEPS      4

/* Motor Control Pins */
#define AN_MOT_BEMF  14   /* Motor back-EMF sensing */
#define HB_AN_PIN    25   /* Half-bridge analog feedback */
#define HB_IN_PIN    26   /* Half-bridge IN pin */
#define HB_INH_PIN   27   /* Half-bridge INH pin */
#define LED_BUILTIN  2    /* Built-in LED */

/* Other Pins */
#define BUTT_PIN   13   /* Button */
#define BUZZ_PIN   18   /* Buzzer */

/*********************************************************************************************************************/
/*                                              Function Prototypes                                                 */
/*********************************************************************************************************************/
void     HAL_InitHW();
uint16_t HAL_ReadVoltageDivider(int analogInput, uint32_t rvfbl, uint32_t rvfbh);
int16_t  HAL_ReadTriggerRaw();
void     HAL_GetTriggerSensorInfo(char* buffer, size_t bufferSize);
void     HAL_GetTriggerSensorFamilyLabel(char* buffer, size_t bufferSize);
void     HAL_GetTriggerSensorActiveTypeLabel(char* buffer, size_t bufferSize);
void     HAL_GetTriggerSensorTypeOptionLabel(uint16_t type, char* buffer, size_t bufferSize);
bool     HAL_TriggerSensorSupportsTypeOverride();
uint16_t HAL_GetTriggerSensorTypeOverride();
bool     HAL_SetTriggerSensorTypeOverride(uint16_t type);
void     HAL_ResetTriggerSensorConfig();
void     HALanalogWrite(int pwmChan, int value);
void     HAL_PinSetup();

/* Sound Functions */
void sound(note_t note, int ms);
void offSound();
void onSound();
void calibSound();
void keySound();

/* Current Sensing */
bool     HAL_HasMotorCurrentSense();
uint16_t HAL_ConvertMotorCurrentAdcToMilliAmps(uint32_t adcRaw);
uint16_t HAL_ReadMotorCurrent();
uint16_t HAL_ReadExternalPot1Raw();
uint16_t HAL_ReadExternalPot2Raw();
extern uint16_t g_adcVoltageRange_mV;

#endif  /* HAL_H_ */
