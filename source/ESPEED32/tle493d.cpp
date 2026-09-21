/*********************************************************************************************************************/
/*                                                   Includes                                                        */
/*********************************************************************************************************************/
#include "tle493d.h"

#ifdef TLE493D_MAG

#include "slot_ESC.h"
#include <math.h>
#include <Preferences.h>

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

static constexpr uint8_t TLE493D_INIT_RETRIES = 6;
static constexpr uint16_t TLE493D_INIT_RETRY_DELAY_MS = 2;
static constexpr int32_t TLE493D_MIN_VECTOR_SQ = 16;

TLE493DVariant g_tleVariant = TLE493DVariant::NONE;
uint8_t g_tleAddress = TLE493D_W2B6_A3_ADDR;
uint8_t g_tleOverrideMode = TRIGGER_SENSOR_TYPE_AUTO;
int16_t g_tleLastAngle = 0;
bool g_tleLastAngleValid = false;

static int16_t g_tleXavg = 0;
static int16_t g_tleYavg = 0;
static bool g_tleFilterInit = false;
static constexpr const char* TLE493D_CFG_NS = "sensor_cfg";
static constexpr const char* TLE493D_CFG_KEY_VAR = "tle_var";
static constexpr const char* TLE493D_CFG_KEY_ADDR = "tle_addr";
static constexpr const char* TLE493D_CFG_KEY_MODE = "tle_mode";

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

bool TLE493D_ReadFrame(uint8_t address, uint8_t* data, uint8_t bytes) {
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

int16_t TLE493D_ComputeAngle10(int16_t x, int16_t y) {
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

uint8_t TLE493D_NormalizeOverrideMode(uint16_t mode) {
  if (mode > TRIGGER_SENSOR_TYPE_MAX) return TRIGGER_SENSOR_TYPE_AUTO;
  return (uint8_t)mode;
}

uint8_t TLE493D_LoadOverrideMode() {
  Preferences pref;
  if (!pref.begin(TLE493D_CFG_NS, true)) return TRIGGER_SENSOR_TYPE_AUTO;
  uint8_t mode = pref.getUChar(TLE493D_CFG_KEY_MODE, TRIGGER_SENSOR_TYPE_AUTO);
  pref.end();
  return TLE493D_NormalizeOverrideMode(mode);
}

void TLE493D_SaveOverrideMode(uint16_t mode) {
  uint8_t normalized = TLE493D_NormalizeOverrideMode(mode);
  Preferences pref;
  if (!pref.begin(TLE493D_CFG_NS, false)) return;
  if (pref.getUChar(TLE493D_CFG_KEY_MODE, TRIGGER_SENSOR_TYPE_AUTO) != normalized) {
    pref.putUChar(TLE493D_CFG_KEY_MODE, normalized);
  }
  pref.end();
}

void TLE493D_ClearStoredConfig(bool clearMode) {
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

bool TLE493D_ApplyMode(uint8_t overrideMode) {
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

#endif  /* TLE493D_MAG */
