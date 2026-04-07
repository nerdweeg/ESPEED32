/*********************************************************************************************************************/
/*                                                   Includes                                                        */
/*********************************************************************************************************************/
#include "HAL.h"
#include "slot_ESC.h"
#include <math.h>
#include <Preferences.h>

/* Datasheet tAPC is in the sub-millisecond range; keep a small margin plus retries. */
static constexpr uint16_t TLE493D_I2C_STABILIZE_MS = 2;
static constexpr uint8_t TLE493D_INIT_RETRIES = 6;
static constexpr uint16_t TLE493D_INIT_RETRY_DELAY_MS = 2;
static constexpr int32_t TLE493D_MIN_VECTOR_SQ = 16;
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

#elif defined(TLE493D_MAG)
  #define TLE493D_W2B6_A0_ADDR 0x35
  #define TLE493D_W2B6_A3_ADDR 0x44
  #define TLE493D_W2B6_MOD1_REG 0x11
  #define TLE493D_W2B6_MOD1_CONFIG 0b11110111
  /* TLE493D-W2B6 A0: configuration sequence per datasheet/example (CFG and MOD1 registers). */
  #define TLE493D_W2B6_A0_CFG_REG    0x10
  #define TLE493D_W2B6_A0_CFG_VALUE  0x11
  #define TLE493D_W2B6_A0_MOD1_VALUE 0x91
  #define TLE493D_P3B6_A0_ADDR 0x5D
  #define TLE493D_P3B6_A1_ADDR 0x13
  #define TLE493D_P3B6_A2_ADDR 0x29
  #define TLE493D_P3B6_A3_ADDR 0x46
  #include <Wire.h>

  enum class TLE493DVariant : uint8_t {
    NONE = 0,
    W2B6,
    W2B6_A0,
    P3B6
  };

  static TLE493DVariant g_tleVariant = TLE493DVariant::NONE;
  static uint8_t g_tleAddress = TLE493D_W2B6_A3_ADDR;
  static int16_t g_tleXavg = 0;
  static int16_t g_tleYavg = 0;
  static bool g_tleFilterInit = false;
  static int16_t g_tleLastAngle = 0;
  static bool g_tleLastAngleValid = false;
  static constexpr const char* TLE493D_CFG_NS = "sensor_cfg";
  static constexpr const char* TLE493D_CFG_KEY_VAR = "tle_var";
  static constexpr const char* TLE493D_CFG_KEY_ADDR = "tle_addr";
  static constexpr const char* TLE493D_CFG_KEY_MODE = "tle_mode";
  static uint8_t g_tleOverrideMode = TRIGGER_SENSOR_TYPE_AUTO;

  static uint8_t TLE493D_EncodeVariant(TLE493DVariant variant) {
    switch (variant) {
      case TLE493DVariant::W2B6: return 1;
      case TLE493DVariant::W2B6_A0: return 3;
      case TLE493DVariant::P3B6: return 2;
      default: return 0;
    }
  }

  static TLE493DVariant TLE493D_DecodeVariant(uint8_t value) {
    switch (value) {
      case 1: return TLE493DVariant::W2B6;
      case 2: return TLE493DVariant::P3B6;
      case 3: return TLE493DVariant::W2B6_A0;
      default: return TLE493DVariant::NONE;
    }
  }

  static bool TLE493D_IsValidP3Address(uint8_t address) {
    return address == TLE493D_P3B6_A0_ADDR || address == TLE493D_P3B6_A1_ADDR
      || address == TLE493D_P3B6_A2_ADDR || address == TLE493D_P3B6_A3_ADDR;
  }

  static bool TLE493D_LoadCachedConfig(TLE493DVariant* variant, uint8_t* address) {
    if (variant == nullptr || address == nullptr) return false;

    Preferences pref;
    if (!pref.begin(TLE493D_CFG_NS, true)) return false;

    uint8_t storedVariant = pref.getUChar(TLE493D_CFG_KEY_VAR, 0);
    uint8_t storedAddress = pref.getUChar(TLE493D_CFG_KEY_ADDR, 0);
    pref.end();

    TLE493DVariant decoded = TLE493D_DecodeVariant(storedVariant);
    if (decoded == TLE493DVariant::W2B6) {
      if (storedAddress != TLE493D_W2B6_A3_ADDR) return false;
    } else if (decoded == TLE493DVariant::W2B6_A0) {
      if (storedAddress != TLE493D_W2B6_A0_ADDR) return false;
    } else if (decoded == TLE493DVariant::P3B6) {
      if (!TLE493D_IsValidP3Address(storedAddress)) return false;
    } else {
      return false;
    }

    *variant = decoded;
    *address = storedAddress;
    return true;
  }

  static void TLE493D_SaveCachedConfig(TLE493DVariant variant, uint8_t address) {
    uint8_t encoded = TLE493D_EncodeVariant(variant);
    if (encoded == 0) return;

    Preferences pref;
    if (!pref.begin(TLE493D_CFG_NS, false)) return;

    uint8_t oldVariant = pref.getUChar(TLE493D_CFG_KEY_VAR, 0);
    uint8_t oldAddress = pref.getUChar(TLE493D_CFG_KEY_ADDR, 0);
    if (oldVariant != encoded || oldAddress != address) {
      pref.putUChar(TLE493D_CFG_KEY_VAR, encoded);
      pref.putUChar(TLE493D_CFG_KEY_ADDR, address);
    }
    pref.end();
  }

  static bool TLE493D_ReadFrame(uint8_t address, uint8_t* data, uint8_t bytes) {
    size_t rxCount = Wire1.requestFrom(address, bytes);
    if (rxCount != bytes) {
      while (Wire1.available()) { (void)Wire1.read(); }
      return false;
    }

    for (uint8_t i = 0; i < bytes; ++i) {
      if (!Wire1.available()) return false;
      int v = Wire1.read();
      if (v < 0) return false;
      data[i] = (uint8_t)v;
    }

    while (Wire1.available()) { (void)Wire1.read(); }
    return true;
  }

  static bool TLE493D_TryInitW2B6(uint8_t address, uint8_t retries = TLE493D_INIT_RETRIES) {
    for (uint8_t attempt = 0; attempt < retries; ++attempt) {
      Wire1.beginTransmission(address);
      Wire1.write(TLE493D_W2B6_MOD1_REG);
      Wire1.write(TLE493D_W2B6_MOD1_CONFIG);
      uint8_t txStatus = Wire1.endTransmission();
      if (txStatus == 0) {
        uint8_t frame[7];
        if (TLE493D_ReadFrame(address, frame, sizeof(frame))) return true;
      }
      delay(TLE493D_INIT_RETRY_DELAY_MS);
    }
    return false;
  }

  static bool TLE493D_TryInitW2B6_A0(uint8_t address, uint8_t retries = TLE493D_INIT_RETRIES) {
    for (uint8_t attempt = 0; attempt < retries; ++attempt) {
      Wire1.beginTransmission(address);
      Wire1.write(TLE493D_W2B6_A0_CFG_REG);
      Wire1.write(TLE493D_W2B6_A0_CFG_VALUE);
      Wire1.write(TLE493D_W2B6_A0_MOD1_VALUE);
      uint8_t txStatus = Wire1.endTransmission();
      if (txStatus == 0) {
        uint8_t frame[7];
        if (TLE493D_ReadFrame(address, frame, sizeof(frame))) return true;
      }
      delay(TLE493D_INIT_RETRY_DELAY_MS);
    }
    return false;
  }

  static bool TLE493D_TryInitP3B6(uint8_t address, uint8_t retries = TLE493D_INIT_RETRIES) {
    /* P3B6 defaults to 1-byte read mode, starting at register 0x00. */
    for (uint8_t attempt = 0; attempt < retries; ++attempt) {
      uint8_t frame[4];
      if (TLE493D_ReadFrame(address, frame, sizeof(frame))) return true;
      delay(TLE493D_INIT_RETRY_DELAY_MS);
    }
    return false;
  }

  static int16_t TLE493D_ComputeAngle10(int16_t x, int16_t y) {
    if (!g_tleFilterInit) {
      g_tleXavg = x;
      g_tleYavg = y;
      g_tleFilterInit = true;
    }
    g_tleXavg = (g_tleXavg * 3 + x) / 4;
    g_tleYavg = (g_tleYavg * 3 + y) / 4;

    int32_t vecSq = (int32_t)g_tleXavg * (int32_t)g_tleXavg + (int32_t)g_tleYavg * (int32_t)g_tleYavg;
    if (vecSq < TLE493D_MIN_VECTOR_SQ && g_tleLastAngleValid) {
      return g_tleLastAngle;
    }

    float angleRad = atan2((float)g_tleYavg, (float)g_tleXavg);
    float angleDeg = angleRad * 180.0f / PI;
    if (angleDeg < 0.0f) angleDeg += 360.0f;

    int16_t angle10 = (int16_t)(angleDeg * 10.0f);
    g_tleLastAngle = angle10;
    g_tleLastAngleValid = true;
    return angle10;
  }

  static uint8_t TLE493D_NormalizeOverrideMode(uint16_t mode) {
    if (mode > TRIGGER_SENSOR_TYPE_MAX) return TRIGGER_SENSOR_TYPE_AUTO;
    return (uint8_t)mode;
  }

  static uint8_t TLE493D_LoadOverrideMode() {
    Preferences pref;
    if (!pref.begin(TLE493D_CFG_NS, true)) return TRIGGER_SENSOR_TYPE_AUTO;
    uint8_t mode = pref.getUChar(TLE493D_CFG_KEY_MODE, TRIGGER_SENSOR_TYPE_AUTO);
    pref.end();
    return TLE493D_NormalizeOverrideMode(mode);
  }

  static void TLE493D_SaveOverrideMode(uint16_t mode) {
    uint8_t normalized = TLE493D_NormalizeOverrideMode(mode);
    Preferences pref;
    if (!pref.begin(TLE493D_CFG_NS, false)) return;
    if (pref.getUChar(TLE493D_CFG_KEY_MODE, TRIGGER_SENSOR_TYPE_AUTO) != normalized) {
      pref.putUChar(TLE493D_CFG_KEY_MODE, normalized);
    }
    pref.end();
  }

  static void TLE493D_ClearStoredConfig(bool clearMode) {
    Preferences pref;
    if (!pref.begin(TLE493D_CFG_NS, false)) return;
    pref.remove(TLE493D_CFG_KEY_VAR);
    pref.remove(TLE493D_CFG_KEY_ADDR);
    if (clearMode) pref.remove(TLE493D_CFG_KEY_MODE);
    pref.end();
  }

  static void TLE493D_ResetRuntimeState() {
    g_tleVariant = TLE493DVariant::NONE;
    g_tleAddress = TLE493D_W2B6_A3_ADDR;
    g_tleXavg = 0;
    g_tleYavg = 0;
    g_tleFilterInit = false;
    g_tleLastAngle = 0;
    g_tleLastAngleValid = false;
  }

  static bool TLE493D_DetectAuto(TLE493DVariant* detectedVariant, uint8_t* detectedAddress) {
    if (detectedVariant == nullptr || detectedAddress == nullptr) return false;

    bool tleReady = false;
    TLE493DVariant cachedVariant = TLE493DVariant::NONE;
    uint8_t cachedAddress = 0;
    if (TLE493D_LoadCachedConfig(&cachedVariant, &cachedAddress)) {
      if (cachedVariant == TLE493DVariant::W2B6) {
        tleReady = TLE493D_TryInitW2B6(cachedAddress, 1);
      } else if (cachedVariant == TLE493DVariant::W2B6_A0) {
        tleReady = TLE493D_TryInitW2B6_A0(cachedAddress, 1);
      } else if (cachedVariant == TLE493DVariant::P3B6) {
        tleReady = TLE493D_TryInitP3B6(cachedAddress, 1);
      }
      if (tleReady) {
        *detectedVariant = cachedVariant;
        *detectedAddress = cachedAddress;
        return true;
      }
    }

    if (TLE493D_TryInitW2B6_A0(TLE493D_W2B6_A0_ADDR)) {
      *detectedVariant = TLE493DVariant::W2B6_A0;
      *detectedAddress = TLE493D_W2B6_A0_ADDR;
      return true;
    }
    if (TLE493D_TryInitW2B6(TLE493D_W2B6_A3_ADDR)) {
      *detectedVariant = TLE493DVariant::W2B6;
      *detectedAddress = TLE493D_W2B6_A3_ADDR;
      return true;
    }

    const uint8_t p3Addresses[] = {
      TLE493D_P3B6_A0_ADDR,
      TLE493D_P3B6_A1_ADDR,
      TLE493D_P3B6_A2_ADDR,
      TLE493D_P3B6_A3_ADDR
    };
    for (uint8_t i = 0; i < sizeof(p3Addresses); ++i) {
      if (TLE493D_TryInitP3B6(p3Addresses[i])) {
        *detectedVariant = TLE493DVariant::P3B6;
        *detectedAddress = p3Addresses[i];
        return true;
      }
    }

    return false;
  }

  static bool TLE493D_DetectForced(uint8_t overrideMode, TLE493DVariant* detectedVariant, uint8_t* detectedAddress) {
    if (detectedVariant == nullptr || detectedAddress == nullptr) return false;

    if (overrideMode == TRIGGER_SENSOR_TYPE_W2B6_A0) {
      if (!TLE493D_TryInitW2B6_A0(TLE493D_W2B6_A0_ADDR)) return false;
      *detectedVariant = TLE493DVariant::W2B6_A0;
      *detectedAddress = TLE493D_W2B6_A0_ADDR;
      return true;
    }
    if (overrideMode == TRIGGER_SENSOR_TYPE_W2B6) {
      if (!TLE493D_TryInitW2B6(TLE493D_W2B6_A3_ADDR)) return false;
      *detectedVariant = TLE493DVariant::W2B6;
      *detectedAddress = TLE493D_W2B6_A3_ADDR;
      return true;
    }
    if (overrideMode == TRIGGER_SENSOR_TYPE_P3B6) {
      const uint8_t p3Addresses[] = {
        TLE493D_P3B6_A0_ADDR,
        TLE493D_P3B6_A1_ADDR,
        TLE493D_P3B6_A2_ADDR,
        TLE493D_P3B6_A3_ADDR
      };
      for (uint8_t i = 0; i < sizeof(p3Addresses); ++i) {
        if (TLE493D_TryInitP3B6(p3Addresses[i])) {
          *detectedVariant = TLE493DVariant::P3B6;
          *detectedAddress = p3Addresses[i];
          return true;
        }
      }
    }

    return false;
  }

  static bool TLE493D_ApplyMode(uint8_t overrideMode) {
    TLE493D_ResetRuntimeState();

    TLE493DVariant detectedVariant = TLE493DVariant::NONE;
    uint8_t detectedAddress = 0;
    bool tleReady = (overrideMode == TRIGGER_SENSOR_TYPE_AUTO)
      ? TLE493D_DetectAuto(&detectedVariant, &detectedAddress)
      : TLE493D_DetectForced(overrideMode, &detectedVariant, &detectedAddress);

    if (tleReady) {
      g_tleVariant = detectedVariant;
      g_tleAddress = detectedAddress;
      TLE493D_SaveCachedConfig(detectedVariant, detectedAddress);
    }

    Serial.print("TLE493D mode=");
    switch (overrideMode) {
      case TRIGGER_SENSOR_TYPE_W2B6: Serial.print("W2B6"); break;
      case TRIGGER_SENSOR_TYPE_W2B6_A0: Serial.print("W2B6_A0"); break;
      case TRIGGER_SENSOR_TYPE_P3B6: Serial.print("P3B6"); break;
      default: Serial.print("AUTO"); break;
    }

    if (tleReady) {
      Serial.print(", active=");
      if (g_tleVariant == TLE493DVariant::W2B6) {
        Serial.print("W2B6");
      } else if (g_tleVariant == TLE493DVariant::W2B6_A0) {
        Serial.print("W2B6_A0");
      } else {
        Serial.print("P3B6");
      }
      Serial.print(", addr=0x");
      Serial.println(g_tleAddress, HEX);
    } else {
      Serial.println(", not detected");
    }

    return tleReady;
  }

#endif

/*********************************************************************************************************************/
/*                                            Function Implementations                                              */
/*********************************************************************************************************************/

/**
 * @brief Initialize hardware components
 * @details Sets up serial communication, I2C, and PWM channels
 */
void HAL_InitHW() {
  /* Initialize serial for debugging */
  Serial.begin(115200);
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
#elif defined(AS5600L_MAG)
  snprintf(buffer, bufferSize, "AS5600L");
#elif defined(AS5600_MAG)
  snprintf(buffer, bufferSize, "AS5600");
#elif defined(MT6701_MAG)
  snprintf(buffer, bufferSize, "MT6701");
#elif defined(ANALOG_TRIG)
  snprintf(buffer, bufferSize, "ANALOG");
#else
  snprintf(buffer, bufferSize, "UNKNOWN");
#endif
}

void HAL_GetTriggerSensorFamilyLabel(char* buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) return;

#if defined(TLE493D_MAG)
  snprintf(buffer, bufferSize, "TLE493D");
#elif defined(AS5600L_MAG)
  snprintf(buffer, bufferSize, "AS5600L");
#elif defined(AS5600_MAG)
  snprintf(buffer, bufferSize, "AS5600");
#elif defined(MT6701_MAG)
  snprintf(buffer, bufferSize, "MT6701");
#elif defined(ANALOG_TRIG)
  snprintf(buffer, bufferSize, "ANALOG");
#else
  snprintf(buffer, bufferSize, "UNKNOWN");
#endif
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
#elif defined(AS5600L_MAG)
  snprintf(buffer, bufferSize, "AS5600L");
#elif defined(AS5600_MAG)
  snprintf(buffer, bufferSize, "AS5600");
#elif defined(MT6701_MAG)
  snprintf(buffer, bufferSize, "MT6701");
#elif defined(ANALOG_TRIG)
  snprintf(buffer, bufferSize, "ANALOG");
#else
  snprintf(buffer, bufferSize, "UNKNOWN");
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
