/*********************************************************************************************************************/
/*                                                   Includes                                                        */
/*********************************************************************************************************************/
#include "HAL.h"
#include "slot_ESC.h"
#include "tle493d.h"
#include <math.h>
#include <esp_system.h>
#include <Preferences.h>

static constexpr int BOOT_SOUND_NOTE_MS = 20;
/* Include appropriate sensor library based on selection */
#ifdef AS5600_MAG
  #include "AS5600.h"
  AS5600 as5600(&Wire1);  /* AS5600 magnetic sensor instance */
  #define ADDRESS 0x36

#elif defined(AS5600L_MAG)
  #include "AS5600L.h"
  AS5600L as5600;  /* AS5600L magnetic sensor instance (different I2C address) */

#elif defined(MT6701_MAG)
  #include "MT6701.hpp"
  MT6701 mt6701;  /* MT6701 magnetic sensor instance */

#endif

/*********************************************************************************************************************/
/*                                            Function Implementations                                              */
/*********************************************************************************************************************/

static const char* HAL_GetResetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:   return "UNKNOWN";
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "OTHER";
  }
}

/**
 * @brief Initialize hardware components
 * @details Sets up serial communication, I2C, and PWM channels
 */
void HAL_InitHW() {
  /* Initialize serial for debugging */
  Serial.begin(115200);
  delay(20);
  Serial.println();
  Serial.print("[BOOT] Reset reason: ");
  Serial.println(HAL_GetResetReasonName(esp_reset_reason()));
  Serial.print("[BOOT] Build: v");
  Serial.print(SW_MAJOR_VERSION);
  Serial.print(".");
  Serial.println(SW_MINOR_VERSION);
#ifdef AS5600_MAG
  Wire1.begin(SDA0_PIN, SCL0_PIN, 400000L);
#endif
  /* Configure ADC for current sensing on GPIO25 */
  analogSetAttenuation(ADC_11db);  /* Set ADC range to 0-3.3V */
  pinMode(HB_AN_PIN, INPUT);       /* Explicitly set pin as input */

#ifdef TLE493D_MAG
  /* Initialize I2C for TLE493D sensor */
  Wire1.begin(SDA0_PIN, SCL0_PIN, 100000L);
  delay(TLE493D_I2C_STABILIZE_MS);  /* Wait for I2C stabilization */
  g_tleOverrideMode = TLE493D_LoadOverrideMode();
  TLE493D_ApplyMode(g_tleOverrideMode);
#endif

  /* Configure motor control PWM channels */
  ledcAttachChannel(HB_IN_PIN, PWM_FREQ_DEFAULT * 1000, THR_PWM_RES_BIT, THR_IN_PWM_CHAN);
  ledcAttachChannel(HB_INH_PIN, PWM_FREQ_DEFAULT * 1000, THR_PWM_RES_BIT, THR_INH_PWM_CHAN);
}

/**
 * @brief Write PWM value to motor control channel
 * @param pwmChan PWM channel number
 * @param value PWM duty cycle value (0-THR_PWM_MAX_DUTY)
 * @note Adapted for ESP32 3.0.0 library (ledcWrite takes PIN, not CHANNEL)
 */
void HALanalogWrite(const int pwmChan, int value) {
  switch (pwmChan) {
    case THR_IN_PWM_CHAN:
      ledcWrite(HB_IN_PIN, (uint32_t)value);
      break;

    case THR_INH_PWM_CHAN:
      ledcWrite(HB_INH_PIN, (uint32_t)value);
      break;
    
    default:
      break;
  }
}

/**
 * @brief Read raw trigger value from configured sensor
 * @return Raw trigger value (sensor-dependent scale)
 */
int16_t HAL_ReadTriggerRaw() {
  uint16_t retVal = 0;

  #if defined(AS5600_MAG) || defined(AS5600L)
    retVal = as5600.readAngle();

  #elif defined(MT6701_MAG)
    retVal = mt6701.getAngleDegrees();

  #elif defined(ANALOG_TRIG)
    retVal = analogRead(AN_THROT_PIN);

  #elif defined(TLE493D_MAG)
    if (g_tleVariant == TLE493DVariant::W2B6 || g_tleVariant == TLE493DVariant::W2B6_A0) {
      uint8_t data[7];
      if (!TLE493D_ReadFrame(g_tleAddress, data, sizeof(data))) {
        retVal = g_tleLastAngleValid ? (uint16_t)g_tleLastAngle : 0;
      } else {
        int16_t x = ((int16_t)data[0] << 4) | (data[4] >> 4);
        if (x >= 2048) x -= 4096;

        int16_t y = ((int16_t)data[1] << 4) | (data[4] & 0x0F);
        if (y >= 2048) y -= 4096;

        retVal = (uint16_t)TLE493D_ComputeAngle10(x, y);
      }
    } else if (g_tleVariant == TLE493DVariant::P3B6) {
      uint8_t data[4];
      if (!TLE493D_ReadFrame(g_tleAddress, data, sizeof(data))) {
        retVal = g_tleLastAngleValid ? (uint16_t)g_tleLastAngle : 0;
      } else {
        int16_t x = ((int16_t)data[0] << 6) | (data[1] & 0x3F);  /* 14-bit signed */
        if (x >= 8192) x -= 16384;

        int16_t y = ((int16_t)data[2] << 6) | (data[3] & 0x3F);  /* 14-bit signed */
        if (y >= 8192) y -= 16384;

        retVal = (uint16_t)TLE493D_ComputeAngle10(x, y);
      }
    } else {
      retVal = 0;
    }
    
  #endif

  return retVal;
}

/**
 * @brief Compile-time sensor family name, shared by the label getters below.
 *        For TLE493D this is just the family name; callers that need the
 *        detected variant/address handle that themselves.
 */
static const char* HAL_GetCompileTimeSensorFamilyName() {
#if defined(TLE493D_MAG)
  return "TLE493D";
#elif defined(AS5600L_MAG)
  return "AS5600L";
#elif defined(AS5600_MAG)
  return "AS5600";
#elif defined(MT6701_MAG)
  return "MT6701";
#elif defined(ANALOG_TRIG)
  return "ANALOG";
#else
  return "UNKNOWN";
#endif
}

/**
 * @brief Get human-readable trigger sensor info for About screen.
 */
void HAL_GetTriggerSensorInfo(char* buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) return;

#if defined(TLE493D_MAG)
  if (g_tleVariant == TLE493DVariant::W2B6) {
    snprintf(buffer, bufferSize, "TLE493D W2B6 0x%02X", g_tleAddress);
  } else if (g_tleVariant == TLE493DVariant::W2B6_A0) {
    snprintf(buffer, bufferSize, "TLE493D W2B6_A0 0x%02X", g_tleAddress);
  } else if (g_tleVariant == TLE493DVariant::P3B6) {
    snprintf(buffer, bufferSize, "TLE493D P3B6 0x%02X", g_tleAddress);
  } else {
    snprintf(buffer, bufferSize, "TLE493D not detected");
  }
#else
  snprintf(buffer, bufferSize, "%s", HAL_GetCompileTimeSensorFamilyName());
#endif
}

void HAL_GetTriggerSensorFamilyLabel(char* buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) return;

  snprintf(buffer, bufferSize, "%s", HAL_GetCompileTimeSensorFamilyName());
}

void HAL_GetTriggerSensorActiveTypeLabel(char* buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) return;

#if defined(TLE493D_MAG)
  if (g_tleVariant == TLE493DVariant::W2B6) {
    snprintf(buffer, bufferSize, "W2B6");
  } else if (g_tleVariant == TLE493DVariant::W2B6_A0) {
    snprintf(buffer, bufferSize, "W2B6_A0");
  } else if (g_tleVariant == TLE493DVariant::P3B6) {
    snprintf(buffer, bufferSize, "P3B6");
  } else {
    snprintf(buffer, bufferSize, "NONE");
  }
#else
  snprintf(buffer, bufferSize, "%s", HAL_GetCompileTimeSensorFamilyName());
#endif
}

void HAL_GetTriggerSensorTypeOptionLabel(uint16_t type, char* buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) return;

  switch (type) {
    case TRIGGER_SENSOR_TYPE_W2B6:
      snprintf(buffer, bufferSize, "W2B6");
      break;
    case TRIGGER_SENSOR_TYPE_W2B6_A0:
      snprintf(buffer, bufferSize, "W2B6_A0");
      break;
    case TRIGGER_SENSOR_TYPE_P3B6:
      snprintf(buffer, bufferSize, "P3B6");
      break;
    case TRIGGER_SENSOR_TYPE_AUTO:
    default:
      snprintf(buffer, bufferSize, "AUTO");
      break;
  }
}

bool HAL_TriggerSensorSupportsTypeOverride() {
#if defined(TLE493D_MAG)
  return true;
#else
  return false;
#endif
}

uint16_t HAL_GetTriggerSensorTypeOverride() {
#if defined(TLE493D_MAG)
  return g_tleOverrideMode;
#else
  return TRIGGER_SENSOR_TYPE_AUTO;
#endif
}

bool HAL_SetTriggerSensorTypeOverride(uint16_t type) {
#if defined(TLE493D_MAG)
  uint8_t normalized = TLE493D_NormalizeOverrideMode(type);
  g_tleOverrideMode = normalized;
  TLE493D_SaveOverrideMode(normalized);
  TLE493D_ClearStoredConfig(false);
  delay(TLE493D_I2C_STABILIZE_MS);
  return TLE493D_ApplyMode(normalized);
#else
  (void)type;
  return false;
#endif
}

void HAL_ResetTriggerSensorConfig() {
#if defined(TLE493D_MAG)
  g_tleOverrideMode = TRIGGER_SENSOR_TYPE_AUTO;
  TLE493D_ClearStoredConfig(true);
  delay(TLE493D_I2C_STABILIZE_MS);
  (void)TLE493D_ApplyMode(g_tleOverrideMode);
#endif
}


/**
 * @brief Setup GPIO pins
 */
void HAL_PinSetup() {
  pinMode(BUZZ_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUTT_PIN, INPUT_PULLUP);
  pinMode(ENCODER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(EXT_POT1_PIN, INPUT);
  pinMode(EXT_POT2_PIN, INPUT);
}

/**
 * @brief Read voltage from voltage divider circuit
 * @param analogInput ADC pin number
 * @param rvfbl Lower resistor value [Ohm]
 * @param rvfbh Upper resistor value [Ohm]
 * @return Voltage applied to voltage divider [mV]
 */
uint16_t HAL_ReadVoltageDivider(int analogInput, uint32_t rvfbl, uint32_t rvfbh) {
  if (rvfbl == 0) return 0;

  uint32_t adcRaw = analogRead(analogInput);

  /* Calculate voltage at ADC pin */
  uint32_t voltage = ((uint32_t)g_adcVoltageRange_mV * adcRaw) / ACD_RESOLUTION_STEPS;

  /* Calculate voltage applied to voltage divider */
  voltage = (voltage * (rvfbl + rvfbh)) / rvfbl;

  return voltage;
}

/**
 * @brief Check whether the current build provides usable motor current sense
 */
bool HAL_HasMotorCurrentSense() {
#if CURRENT_SENSE_PROFILE == CURRENT_SENSE_PROFILE_NONE
  return false;
#else
  return true;
#endif
}

/**
 * @brief Convert motor current ADC reading to milliamps
 * @details Converts the raw ADC reading using the selected compile-time
 *          current-sense profile. BTN99X0 keeps the existing calibrated
 *          conversion. BTS7960 / IBT_2 uses an initial kILIS/RIS-based
 *          approximation which may need tuning for the actual module.
 * @param adcRaw Raw ADC reading from the current-sense input
 * @return Motor current in milliamps [mA]
 */
uint16_t HAL_ConvertMotorCurrentAdcToMilliAmps(uint32_t adcRaw) {
  /* Calculate voltage at ADC pin */
  uint32_t voltage_mV = ((uint32_t)g_adcVoltageRange_mV * adcRaw) / ACD_RESOLUTION_STEPS;

#if CURRENT_SENSE_PROFILE == CURRENT_SENSE_PROFILE_BTN99X0
  /* Calculate motor current based on BTN9960LV IS characteristic and voltage divider
     ILOAD [mA] = V_ADC [V] * 7752 = V_ADC [mV] * 7.752 */
  uint32_t current_mA = (voltage_mV * BTN99X0_CURRENT_SENSE_MA_PER_V) / 1000UL;

  return (uint16_t)current_mA;
#elif CURRENT_SENSE_PROFILE == CURRENT_SENSE_PROFILE_NONE
  (void)voltage_mV;
  return 0;
#elif CURRENT_SENSE_PROFILE == CURRENT_SENSE_PROFILE_BTS7960
  if (BTS7960_CURRENT_SENSE_EFFECTIVE_R_OHMS == 0) return 0;
  if (voltage_mV <= BTS7960_CURRENT_SENSE_OFFSET_MV) return 0;

  uint32_t senseVoltage_mV = voltage_mV - BTS7960_CURRENT_SENSE_OFFSET_MV;
  uint32_t current_mA = (senseVoltage_mV * BTS7960_CURRENT_SENSE_KILIS)
                        / BTS7960_CURRENT_SENSE_EFFECTIVE_R_OHMS;
  if (current_mA > 65535UL) current_mA = 65535UL;
  return (uint16_t)current_mA;
#else
  #error "Unsupported CURRENT_SENSE_PROFILE value"
#endif
}

/**
 * @brief Read motor current from BTN9960LV IS pin
 * @return Motor current in milliamps [mA]
 */
uint16_t HAL_ReadMotorCurrent() {
  if (!HAL_HasMotorCurrentSense()) return 0;
  return HAL_ConvertMotorCurrentAdcToMilliAmps((uint32_t)analogRead(HB_AN_PIN));
}

uint16_t HAL_ReadExternalPot1Raw() {
  return (uint16_t)analogRead(EXT_POT1_PIN);
}

uint16_t HAL_ReadExternalPot2Raw() {
  return (uint16_t)analogRead(EXT_POT2_PIN);
}

/**
 * @brief Play a tone on the buzzer
 * @param note Musical note to play
 * @param ms Duration in milliseconds
 */
void sound(note_t note, int ms) {
  ledcAttachChannel(BUZZ_PIN, 5000, 8, BUZZ_CHAN);
  ledcWriteNote(BUZZ_PIN, note, 7);
  delay(ms);
  ledcDetach(BUZZ_PIN);
}

/**
 * @brief Play power-off sound (E -> C)
 */
void offSound() { 
  sound(NOTE_E, 60);
  delay(60);
  sound(NOTE_C, 60);  
}

/**
 * @brief Play power-on sound (C -> E)
 */
void onSound() { 
  sound(NOTE_C, BOOT_SOUND_NOTE_MS);
  sound(NOTE_E, BOOT_SOUND_NOTE_MS);
}

/**
 * @brief Play calibration mode sound (C -> G -> A)
 */
void calibSound() { 
  sound(NOTE_C, 60);
  delay(60);
  sound(NOTE_G, 60);  
  delay(60);
  sound(NOTE_A, 60);  
}

/**
 * @brief Play key press sound
 */
void keySound() { 
  sound(NOTE_D, KEY_SOUND_MS);
}
