#include "connectivity_portal_internal.h"
#include "app_init.h"

/* Settings JSON: backup build/parse/validate, web UI schema and state, and live web patches. */

/**
 * @brief Build JSON backup string from the provided configuration snapshot.
 */
static String buildJsonBackupFromConfig(const StoredVar_type& storedVar,
                                        uint16_t antiSpinStepMs,
                                        uint16_t antiSpinStepPct,
                                        uint16_t antiSpinDisplayMode,
                                        uint16_t brakeStepRaw,
                                        uint16_t sensiStepRaw,
                                        uint16_t advancedMenuEnabled,
                                        uint16_t encoderInvertEnabled,
                                        uint16_t pwmFreqMaxProfile,
                                        uint16_t adcVoltageRange_mV,
                                        uint16_t wifiConfiguredMode,
                                        const char* wifiClientSsid,
                                        const char* wifiClientPassword,
                                        const char* uiAuthPassword) {
  (void)wifiClientPassword;
  (void)uiAuthPassword;
  String json;
  json.reserve(6000);
  char buf[128];
  char backupControllerId[13];

  getBackupControllerId(backupControllerId, sizeof(backupControllerId));
  json += "{\n";
  json += "  \"schemaVersion\": 1,\n";
  snprintf(buf, sizeof(buf), "  \"version\": %d,\n", STORED_VAR_VERSION);              json += buf;
  snprintf(buf, sizeof(buf), "  \"selectedCarNumber\": %u,\n", storedVar.selectedCarNumber); json += buf;
  snprintf(buf, sizeof(buf), "  \"minTrigger_raw\": %d,\n", storedVar.minTrigger_raw); json += buf;
  snprintf(buf, sizeof(buf), "  \"maxTrigger_raw\": %d,\n", storedVar.maxTrigger_raw); json += buf;
  snprintf(buf, sizeof(buf), "  \"viewMode\": %u,\n", storedVar.viewMode);           json += buf;
  snprintf(buf, sizeof(buf), "  \"screensaverTimeout\": %u,\n", storedVar.screensaverTimeout); json += buf;
  snprintf(buf, sizeof(buf), "  \"soundBoot\": %u,\n", storedVar.soundBoot);         json += buf;
  snprintf(buf, sizeof(buf), "  \"soundRace\": %u,\n", storedVar.soundRace);         json += buf;
  snprintf(buf, sizeof(buf), "  \"antiSpinStep\": %u,\n", antiSpinStepMs);           json += buf; /* legacy alias for ms-step */
  snprintf(buf, sizeof(buf), "  \"antiSpinStepMs\": %u,\n", antiSpinStepMs);         json += buf;
  snprintf(buf, sizeof(buf), "  \"antiSpinStepPct\": %u,\n", antiSpinStepPct);       json += buf;
  snprintf(buf, sizeof(buf), "  \"antiSpinDisplayMode\": %u,\n", antiSpinDisplayMode); json += buf;
  snprintf(buf, sizeof(buf), "  \"brakeStep\": %u.%u,\n", brakeStepRaw / BRAKE_SCALE, brakeFracDigit(brakeStepRaw)); json += buf;
  snprintf(buf, sizeof(buf), "  \"sensiStep\": %u.%u,\n", sensiStepRaw / SENSI_SCALE, sensiFracDigit(sensiStepRaw)); json += buf;
  snprintf(buf, sizeof(buf), "  \"advancedMenu\": %u,\n", advancedMenuEnabled ? 1 : 0); json += buf;
  snprintf(buf, sizeof(buf), "  \"encoderInvert\": %u,\n", encoderInvertEnabled ? 1 : 0); json += buf;
  snprintf(buf, sizeof(buf), "  \"pwmFreqMax\": %u,\n", pwmFreqMaxProfileToWholeKHz(pwmFreqMaxProfile)); json += buf;
  snprintf(buf, sizeof(buf), "  \"adcVoltageRangeMv\": %u,\n", adcVoltageRange_mV);  json += buf;
  snprintf(buf, sizeof(buf), "  \"gridCarSelectEnabled\": %u,\n", storedVar.gridCarSelectEnabled); json += buf;
  snprintf(buf, sizeof(buf), "  \"raceViewMode\": %u,\n", storedVar.raceViewMode);   json += buf;
  snprintf(buf, sizeof(buf), "  \"language\": %u,\n", storedVar.language);           json += buf;
  snprintf(buf, sizeof(buf), "  \"textCase\": %u,\n", storedVar.textCase);           json += buf;
  snprintf(buf, sizeof(buf), "  \"listFontSize\": %u,\n", storedVar.listFontSize);   json += buf;
  snprintf(buf, sizeof(buf), "  \"startupDelay\": %u,\n", storedVar.startupDelay);   json += buf;
  json += "  \"backupControllerId\": \"";
  appendJsonEscaped(json, backupControllerId);
  json += "\",\n";
  json += "  \"screensaverLine1\": \"";
  appendJsonEscaped(json, storedVar.screensaverLine1);
  json += "\",\n";
  json += "  \"screensaverLine2\": \"";
  appendJsonEscaped(json, storedVar.screensaverLine2);
  json += "\",\n";
  snprintf(buf, sizeof(buf), "  \"wifiMode\": %u,\n", wifiConfiguredMode); json += buf;
  json += "  \"wifiStaSsid\": \"";
  appendJsonEscaped(json, wifiClientSsid);
  json += "\",\n";
  json += "  \"wifiStaPasswordStored\": 0,\n";
  json += "  \"uiAuthPasswordStored\": 0,\n";

  json += "  \"cars\": [\n";
  for (int i = 0; i < CAR_MAX_COUNT; i++) {
    const CarParam_type& c = storedVar.carParam[i];
    json += "    {\n";
    json += "      \"name\": \"";
    appendJsonEscaped(json, c.carName);
    json += "\",\n";
    snprintf(buf, sizeof(buf), "      \"minSpeed\": %u.%u,\n", c.minSpeed / SENSI_SCALE, sensiFracDigit(c.minSpeed)); json += buf;
    snprintf(buf, sizeof(buf), "      \"brake\": %u.%u,\n", c.brake / BRAKE_SCALE, brakeFracDigit(c.brake)); json += buf;
    snprintf(buf, sizeof(buf), "      \"maxSpeed\": %u,\n", c.maxSpeed);               json += buf;
    snprintf(buf, sizeof(buf), "      \"curveInput\": %u,\n", c.throttleCurveVertex.inputThrottle); json += buf;
    snprintf(buf, sizeof(buf), "      \"curveDiff\": %u,\n", c.throttleCurveVertex.curveSpeedDiff); json += buf;
    snprintf(buf, sizeof(buf), "      \"fade\": %u,\n", c.fade);                       json += buf;
    snprintf(buf, sizeof(buf), "      \"antiSpin\": %u,\n", c.antiSpin);               json += buf;
    snprintf(buf, sizeof(buf), "      \"freqPWM\": %u,\n", c.freqPWM);                 json += buf;
    snprintf(buf, sizeof(buf), "      \"altBrake\": %u,\n", c.brakeButtonReduction);  json += buf;
    json += "      \"releaseMode\": \"";
    json += getBackupReleaseModeName(c.quickBrakeEnabled);
    json += "\",\n";
    snprintf(buf, sizeof(buf), "      \"releaseZone\": %u,\n", c.quickBrakeThreshold); json += buf;
    snprintf(buf, sizeof(buf), "      \"releaseLevel\": %u\n", c.quickBrakeStrength);  json += buf;
    json += "    }";
    if (i < CAR_MAX_COUNT - 1) json += ",";
    json += "\n";
  }
  json += "  ]\n}\n";
  return json;
}

/**
 * @brief Build JSON backup string from current runtime configuration.
 */
String buildJsonBackup() {
  loadWiFiNetworkSettingsIfNeeded();
  return buildJsonBackupFromConfig(g_storedVar,
                                   g_antiSpinStepMs,
                                   g_antiSpinStepPct,
                                   g_antiSpinDisplayMode,
                                   g_brakeStep,
                                   g_sensiStep,
                                   g_advancedMenuEnabled,
                                   g_encoderInvertEnabled,
                                   getConfiguredPwmFreqMaxProfile(),
                                   g_adcVoltageRange_mV,
                                   g_wifiConfiguredMode,
                                   g_wifiClientSsid,
                                   g_wifiClientPassword,
                                   g_uiAuthPassword);
}

/**
 * @brief Parse an integer value from JSON by key name
 */
static bool parseJsonInt(const String& json, const char* key, int32_t& outVal) {
  String search = String("\"") + key + "\"";
  int idx = json.indexOf(search);
  if (idx < 0) return false;
  int colon = json.indexOf(':', idx);
  if (colon < 0) return false;
  int start = colon + 1;
  while (start < (int)json.length() && (json[start] == ' ' || json[start] == '\n' || json[start] == '\r')) start++;
  int end = start;
  while (end < (int)json.length() && (isDigit(json[end]) || json[end] == '-')) end++;
  if (end == start) return false;
  outVal = json.substring(start, end).toInt();
  return true;
}

static bool parseJsonNumberToken(const String& json, const char* key, String& outToken) {
  String search = String("\"") + key + "\"";
  int idx = json.indexOf(search);
  if (idx < 0) return false;
  int colon = json.indexOf(':', idx);
  if (colon < 0) return false;
  int start = colon + 1;
  while (start < (int)json.length() && (json[start] == ' ' || json[start] == '\n' || json[start] == '\r')) start++;
  int end = start;
  while (end < (int)json.length() && (isDigit(json[end]) || json[end] == '-' || json[end] == '.')) end++;
  if (end == start) return false;
  outToken = json.substring(start, end);
  outToken.trim();
  return outToken.length() > 0;
}

static bool parseJsonTenthPercent(const String& json, const char* key, uint16_t maxRaw, uint16_t* outRaw) {
  if (outRaw == nullptr) return false;
  String token;
  if (!parseJsonNumberToken(json, key, token)) return false;

  int dot = token.indexOf('.');
  int32_t whole = 0;
  int32_t frac = 0;

  if (dot < 0) {
    whole = token.toInt();
  } else {
    String left = token.substring(0, dot);
    String right = token.substring(dot + 1);
    if (right.length() == 0) return false;
    for (int i = 0; i < (int)right.length(); i++) {
      if (!isDigit(right[i])) return false;
    }
    whole = left.toInt();
    frac = right[0] - '0';
    for (int i = 1; i < (int)right.length(); i++) {
      if (right[i] != '0') return false;  /* allow only one non-zero decimal digit */
    }
  }

  if (whole < 0) return false;
  if (frac < 0 || frac > 9) return false;

  uint32_t raw32 = (uint32_t)whole * 10U + (uint32_t)frac;
  if (raw32 > maxRaw) return false;
  uint16_t raw = (uint16_t)raw32;
  *outRaw = raw;
  return true;
}

/**
 * @brief Parse a string value from JSON by key name
 */
bool parseJsonStr(const String& json, const char* key, char* outStr, int maxLen) {
  String search = String("\"") + key + "\"";
  int idx = json.indexOf(search);
  if (idx < 0) return false;
  int colon = json.indexOf(':', idx);
  if (colon < 0) return false;
  int qStart = json.indexOf('"', colon + 1);
  if (qStart < 0) return false;
  int qEnd = json.indexOf('"', qStart + 1);
  if (qEnd < 0) return false;
  int len = qEnd - qStart - 1;
  if (len >= maxLen) len = maxLen - 1;
  json.substring(qStart + 1, qStart + 1 + len).toCharArray(outStr, maxLen);
  return true;
}

static bool jsonHasKey(const String& json, const char* key) {
  String search = String("\"") + key + "\"";
  return json.indexOf(search) >= 0;
}

static bool parseJsonIntAlias(const String& json, const char* primaryKey, const char* legacyKey, int32_t& outVal) {
  if (jsonHasKey(json, primaryKey)) return parseJsonInt(json, primaryKey, outVal);
  return parseJsonInt(json, legacyKey, outVal);
}

const char* getBackupReleaseModeName(uint16_t mode) {
  switch (mode) {
    case RELEASE_BRAKE_QUICK: return "QUICK";
    case RELEASE_BRAKE_DRAG:  return "DRAG";
    case RELEASE_BRAKE_OFF:
    default:                  return "OFF";
  }
}

static bool parseJsonReleaseMode(const String& json, const char* key, uint16_t* outMode) {
  if (outMode == nullptr) return false;

  int32_t v = 0;
  if (parseJsonInt(json, key, v)) {
    if (v < RELEASE_BRAKE_OFF || v > RELEASE_BRAKE_DRAG) return false;
    *outMode = (uint16_t)v;
    return true;
  }

  char modeStr[16];
  if (!parseJsonStr(json, key, modeStr, sizeof(modeStr))) return false;

  String mode = modeStr;
  mode.trim();
  if (mode.equalsIgnoreCase("OFF")) {
    *outMode = RELEASE_BRAKE_OFF;
    return true;
  }
  if (mode.equalsIgnoreCase("QUICK") || mode.equalsIgnoreCase("QCK")) {
    *outMode = RELEASE_BRAKE_QUICK;
    return true;
  }
  if (mode.equalsIgnoreCase("DRAG") || mode.equalsIgnoreCase("DRG")) {
    *outMode = RELEASE_BRAKE_DRAG;
    return true;
  }

  return false;
}

static bool parseJsonReleaseModeAlias(const String& json, const char* primaryKey, const char* legacyKey, uint16_t* outMode) {
  if (jsonHasKey(json, primaryKey)) return parseJsonReleaseMode(json, primaryKey, outMode);
  return parseJsonReleaseMode(json, legacyKey, outMode);
}

/**
 * @brief Validate integer is within range
 */
static bool inRange(int32_t val, int32_t minVal, int32_t maxVal) {
  return val >= minVal && val <= maxVal;
}

static bool parsePwmFreqMaxProfileValue(int32_t valueKHz, uint16_t* outProfile) {
  if (outProfile == nullptr) {
    return false;
  }

  switch (valueKHz) {
    case 5:
      *outProfile = PWM_FREQ_MAX_PROFILE_5K;
      return true;
    case 10:
      *outProfile = PWM_FREQ_MAX_PROFILE_10K;
      return true;
    case 20:
      *outProfile = PWM_FREQ_MAX_PROFILE_20K;
      return true;
    case 25:
      *outProfile = PWM_FREQ_MAX_PROFILE_25K;
      return true;
    default:
      return false;
  }
}

/**
 * @brief Parse and validate uploaded JSON, populate temporary StoredVar
 * @return true if valid, false with error message
 */
bool parseAndValidateJson(const String& json, StoredVar_type* sv, uint16_t* antiSpinStepMs,
                                 uint16_t* antiSpinStepPct, uint16_t* antiSpinDisplayMode,
                                 uint16_t* brakeStepRaw, uint16_t* sensiStepRaw,
                                 uint16_t* advancedMenuEnabled, uint16_t* encoderInvertEnabled, uint16_t* pwmFreqMaxProfile,
                                 uint16_t* adcVoltageRangeMv,
                                 uint16_t* wifiConfiguredMode, char* wifiClientSsid,
                                 size_t wifiClientSsidLen, char* wifiClientPassword,
                                 size_t wifiClientPasswordLen, char* uiAuthUsername,
                                 size_t uiAuthUsernameLen, char* uiAuthPassword,
                                 size_t uiAuthPasswordLen, String* errorMsg,
                                 String* warningMsg) {
  int32_t v;
  bool wifiPasswordStoredSpecified = false;
  bool wifiPasswordStored = false;
  bool hasEncryptedWifiPassword = jsonHasKey(json, "wifiStaPasswordEnc") ||
                                  jsonHasKey(json, "wifiStaPasswordNonce") ||
                                  jsonHasKey(json, "wifiStaPasswordTag");
  bool hasLegacyPlainWifiPassword = jsonHasKey(json, "wifiStaPassword");
  bool uiAuthPasswordStoredSpecified = false;
  bool uiAuthPasswordStored = false;
  bool hasEncryptedUiAuthPassword = jsonHasKey(json, "uiAuthPasswordEnc") ||
                                    jsonHasKey(json, "uiAuthPasswordNonce") ||
                                    jsonHasKey(json, "uiAuthPasswordTag");
  bool hasLegacyPlainUiAuthPassword = jsonHasKey(json, "uiAuthPassword");
  bool controllerLocalSettingsRestoreAllowed = true;
  char backupControllerId[13];
  char currentControllerId[13];

  if (warningMsg != nullptr) {
    *warningMsg = "";
  }
  if (parseJsonStr(json, "backupControllerId", backupControllerId, sizeof(backupControllerId)) &&
      backupControllerId[0] != '\0') {
    getBackupControllerId(currentControllerId, sizeof(currentControllerId));
    if (strcmp(backupControllerId, currentControllerId) != 0) {
      controllerLocalSettingsRestoreAllowed = false;
      if (warningMsg != nullptr) {
        *warningMsg = "WiFi/login settings skipped (backup is from another controller)";
      }
    }
  }
  if (controllerLocalSettingsRestoreAllowed &&
      (hasEncryptedWifiPassword || hasEncryptedUiAuthPassword) &&
      !ensureBackupSecretAvailable(false)) {
    controllerLocalSettingsRestoreAllowed = false;
    if (warningMsg != nullptr) {
      *warningMsg = "WiFi/login settings skipped (controller backup key is missing)";
    }
  }

  /* Initialize with current settings as defaults (missing fields keep current values) */
  *sv = g_storedVar;
  loadWiFiNetworkSettingsIfNeeded();
  if (antiSpinStepMs != nullptr) {
    *antiSpinStepMs = g_antiSpinStepMs;
  }
  if (antiSpinStepPct != nullptr) {
    *antiSpinStepPct = g_antiSpinStepPct;
  }
  if (antiSpinDisplayMode != nullptr) {
    *antiSpinDisplayMode = g_antiSpinDisplayMode;
  }
  if (brakeStepRaw != nullptr) {
    *brakeStepRaw = g_brakeStep;
  }
  if (sensiStepRaw != nullptr) {
    *sensiStepRaw = g_sensiStep;
  }
  if (advancedMenuEnabled != nullptr) {
    *advancedMenuEnabled = g_advancedMenuEnabled ? 1 : 0;
  }
  if (encoderInvertEnabled != nullptr) {
    *encoderInvertEnabled = g_encoderInvertEnabled ? 1 : 0;
  }
  if (pwmFreqMaxProfile != nullptr) {
    *pwmFreqMaxProfile = getConfiguredPwmFreqMaxProfile();
  }
  if (adcVoltageRangeMv != nullptr) {
    *adcVoltageRangeMv = g_adcVoltageRange_mV;
  }
  if (wifiConfiguredMode != nullptr) {
    *wifiConfiguredMode = g_wifiConfiguredMode;
  }
  if (wifiClientSsid != nullptr && wifiClientSsidLen > 0) {
    copyBoundedString(wifiClientSsid, wifiClientSsidLen, g_wifiClientSsid);
  }
  if (wifiClientPassword != nullptr && wifiClientPasswordLen > 0) {
    copyBoundedString(wifiClientPassword, wifiClientPasswordLen, g_wifiClientPassword);
  }
  if (uiAuthUsername != nullptr && uiAuthUsernameLen > 0) {
    copyBoundedString(uiAuthUsername, uiAuthUsernameLen, g_uiAuthUsername);
  }
  if (uiAuthPassword != nullptr && uiAuthPasswordLen > 0) {
    copyBoundedString(uiAuthPassword, uiAuthPasswordLen, g_uiAuthPassword);
  }

  /* Version check - warn but allow cross-version restore for car profiles */
  if (parseJsonInt(json, "version", v) && v != STORED_VAR_VERSION) {
    /* Different version: car profiles are restored, global settings use defaults for missing fields */
  }

  /* Parse global settings - use value if present and valid, otherwise keep default */
  if (parseJsonInt(json, "selectedCarNumber", v) && inRange(v, 0, CAR_MAX_COUNT - 1))
    sv->selectedCarNumber = v;
  if (parseJsonInt(json, "minTrigger_raw", v))
    sv->minTrigger_raw = (int16_t)v;
  if (parseJsonInt(json, "maxTrigger_raw", v))
    sv->maxTrigger_raw = (int16_t)v;
  if (parseJsonInt(json, "viewMode", v) && inRange(v, 0, 1))
    sv->viewMode = v;
  if (parseJsonInt(json, "screensaverTimeout", v) && inRange(v, 0, SCREENSAVER_TIMEOUT_MAX))
    sv->screensaverTimeout = v;
  if (parseJsonInt(json, "soundBoot", v) && inRange(v, 0, 1))
    sv->soundBoot = v;
  if (parseJsonInt(json, "soundRace", v) && inRange(v, 0, 1))
    sv->soundRace = v;
  if (antiSpinStepMs != nullptr &&
      parseJsonInt(json, "antiSpinStep", v) &&
      inRange(v, ANTISPIN_STEP_MIN, ANTISPIN_STEP_MAX)) {
    *antiSpinStepMs = (uint16_t)v;
  }
  if (antiSpinStepMs != nullptr &&
      parseJsonInt(json, "antiSpinStepMs", v) &&
      inRange(v, ANTISPIN_STEP_MIN, ANTISPIN_STEP_MAX)) {
    *antiSpinStepMs = (uint16_t)v;
  }
  if (antiSpinStepPct != nullptr &&
      parseJsonInt(json, "antiSpinStepPct", v) &&
      inRange(v, ANTISPIN_STEP_PCT_MIN, ANTISPIN_STEP_PCT_MAX)) {
    *antiSpinStepPct = (uint16_t)v;
  }
  {
    uint16_t stepRaw = 0;
    if (brakeStepRaw != nullptr &&
        parseJsonTenthPercent(json, "brakeStep", BRAKE_STEP_MAX, &stepRaw) &&
        stepRaw >= BRAKE_STEP_MIN) {
      *brakeStepRaw = stepRaw;
    }
    if (sensiStepRaw != nullptr &&
        parseJsonTenthPercent(json, "sensiStep", SENSI_STEP_MAX, &stepRaw) &&
        stepRaw >= SENSI_STEP_MIN) {
      *sensiStepRaw = stepRaw;
    }
  }
  if (antiSpinDisplayMode != nullptr &&
      parseJsonInt(json, "antiSpinDisplayMode", v) &&
      inRange(v, ANTISPIN_UI_MODE_MS, ANTISPIN_UI_MODE_TEXT)) {
    *antiSpinDisplayMode = (uint16_t)v;
  }
  if (advancedMenuEnabled != nullptr &&
      (parseJsonInt(json, "advancedMenu", v) || parseJsonInt(json, "advancedMenuEnabled", v)) &&
      inRange(v, 0, 1)) {
    *advancedMenuEnabled = (uint16_t)v;
  }
  if (encoderInvertEnabled != nullptr &&
      parseJsonInt(json, "encoderInvert", v) &&
      inRange(v, 0, 1)) {
    *encoderInvertEnabled = (uint16_t)v;
  }
  if (pwmFreqMaxProfile != nullptr &&
      parseJsonInt(json, "pwmFreqMax", v)) {
    uint16_t parsedProfile = PWM_FREQ_MAX_PROFILE_DEFAULT;
    if (!parsePwmFreqMaxProfileValue(v, &parsedProfile)) {
      *errorMsg = "Error: invalid pwmFreqMax";
      return false;
    }
    *pwmFreqMaxProfile = parsedProfile;
  }
  if (adcVoltageRangeMv != nullptr &&
      parseJsonInt(json, "adcVoltageRangeMv", v) &&
      inRange(v, ADC_VOLTAGE_RANGE_MIN_MVOLTS, ADC_VOLTAGE_RANGE_MAX_MVOLTS)) {
    *adcVoltageRangeMv = (uint16_t)v;
  }
  if (parseJsonInt(json, "gridCarSelectEnabled", v) && inRange(v, 0, 1))
    sv->gridCarSelectEnabled = v;
  if (parseJsonInt(json, "raceViewMode", v) && inRange(v, 0, 2))
    sv->raceViewMode = v;
  if (parseJsonInt(json, "language", v) && inRange(v, LANG_NOR, LANG_MAX))
    sv->language = v;
  if (parseJsonInt(json, "textCase", v) && inRange(v, TEXT_CASE_UPPER, TEXT_CASE_PASCAL))
    sv->textCase = v;
  if (parseJsonInt(json, "listFontSize", v) && inRange(v, FONT_SIZE_LARGE, FONT_SIZE_SMALL))
    sv->listFontSize = v;
  if (parseJsonInt(json, "startupDelay", v) && inRange(v, STARTUP_DELAY_MIN, STARTUP_DELAY_MAX))
    sv->startupDelay = v;
  if (controllerLocalSettingsRestoreAllowed &&
      wifiConfiguredMode != nullptr &&
      parseJsonInt(json, "wifiMode", v) &&
      inRange(v, WIFI_CONFIG_AP, WIFI_CONFIG_HOME)) {
    *wifiConfiguredMode = (uint16_t)v;
  }

  /* Parse screensaver text fields */
  char tempStr[SCREENSAVER_TEXT_MAX];
  if (parseJsonStr(json, "screensaverLine1", tempStr, SCREENSAVER_TEXT_MAX))
    strncpy(sv->screensaverLine1, tempStr, SCREENSAVER_TEXT_MAX);
  if (parseJsonStr(json, "screensaverLine2", tempStr, SCREENSAVER_TEXT_MAX))
    strncpy(sv->screensaverLine2, tempStr, SCREENSAVER_TEXT_MAX);
  if (controllerLocalSettingsRestoreAllowed &&
      wifiClientSsid != nullptr && wifiClientSsidLen > 0 &&
      parseJsonStr(json, "wifiStaSsid", wifiClientSsid, (int)wifiClientSsidLen)) {
    wifiClientSsid[wifiClientSsidLen - 1] = '\0';
  }
  if (controllerLocalSettingsRestoreAllowed && parseJsonInt(json, "wifiStaPasswordStored", v)) {
    if (!inRange(v, 0, 1)) { *errorMsg = "Error: invalid wifiStaPasswordStored"; return false; }
    wifiPasswordStoredSpecified = true;
    wifiPasswordStored = (v != 0);
  }
  if (controllerLocalSettingsRestoreAllowed && wifiClientPassword != nullptr && wifiClientPasswordLen > 0) {
    if (wifiPasswordStoredSpecified && !wifiPasswordStored) {
      copyBoundedString(wifiClientSsid, wifiClientSsidLen, g_wifiClientSsid);
      copyBoundedString(wifiClientPassword, wifiClientPasswordLen, g_wifiClientPassword);
      if (wifiConfiguredMode != nullptr) {
        *wifiConfiguredMode = g_wifiConfiguredMode;
      }
    } else if (hasEncryptedWifiPassword) {
      if (!decryptBackupWiFiPasswordFromJson(json, wifiClientPassword, wifiClientPasswordLen, errorMsg)) {
        if (warningMsg != nullptr && warningMsg->length() == 0) {
          *warningMsg = "WiFi/login settings skipped (could not decrypt Client WiFi password)";
        }
        copyBoundedString(wifiClientSsid, wifiClientSsidLen, g_wifiClientSsid);
        copyBoundedString(wifiClientPassword, wifiClientPasswordLen, g_wifiClientPassword);
        if (wifiConfiguredMode != nullptr) {
          *wifiConfiguredMode = g_wifiConfiguredMode;
        }
      } else {
        /* WiFi restore succeeded, keep parsed values. */
      }
    } else if (hasLegacyPlainWifiPassword &&
               parseJsonStr(json, "wifiStaPassword", wifiClientPassword, (int)wifiClientPasswordLen)) {
      wifiClientPassword[wifiClientPasswordLen - 1] = '\0';
    } else if (wifiPasswordStoredSpecified && wifiPasswordStored) {
      if (warningMsg != nullptr && warningMsg->length() == 0) {
        *warningMsg = "WiFi/login settings skipped (backup WiFi password data is incomplete)";
      }
      copyBoundedString(wifiClientSsid, wifiClientSsidLen, g_wifiClientSsid);
      copyBoundedString(wifiClientPassword, wifiClientPasswordLen, g_wifiClientPassword);
      if (wifiConfiguredMode != nullptr) {
        *wifiConfiguredMode = g_wifiConfiguredMode;
      }
    }
  }
  if (controllerLocalSettingsRestoreAllowed &&
      uiAuthUsername != nullptr && uiAuthUsernameLen > 0 &&
      parseJsonStr(json, "uiAuthUsername", uiAuthUsername, (int)uiAuthUsernameLen)) {
    uiAuthUsername[uiAuthUsernameLen - 1] = '\0';
    if (uiAuthUsername[0] == '\0') {
      getDefaultUiAuthUsername(uiAuthUsername, uiAuthUsernameLen);
    }
  }
  if (controllerLocalSettingsRestoreAllowed && parseJsonInt(json, "uiAuthPasswordStored", v)) {
    if (!inRange(v, 0, 1)) { *errorMsg = "Error: invalid uiAuthPasswordStored"; return false; }
    uiAuthPasswordStoredSpecified = true;
    uiAuthPasswordStored = (v != 0);
  }
  if (controllerLocalSettingsRestoreAllowed && uiAuthPassword != nullptr && uiAuthPasswordLen > 0) {
    if (uiAuthPasswordStoredSpecified && !uiAuthPasswordStored) {
      copyBoundedString(uiAuthUsername, uiAuthUsernameLen, g_uiAuthUsername);
      copyBoundedString(uiAuthPassword, uiAuthPasswordLen, g_uiAuthPassword);
    } else if (hasEncryptedUiAuthPassword) {
      if (!decryptBackupUiAuthPasswordFromJson(json, uiAuthPassword, uiAuthPasswordLen, errorMsg)) {
        if (warningMsg != nullptr && warningMsg->length() == 0) {
          *warningMsg = "WiFi/login settings skipped (could not decrypt controller login)";
        }
        copyBoundedString(uiAuthUsername, uiAuthUsernameLen, g_uiAuthUsername);
        copyBoundedString(uiAuthPassword, uiAuthPasswordLen, g_uiAuthPassword);
      }
    } else if (hasLegacyPlainUiAuthPassword &&
               parseJsonStr(json, "uiAuthPassword", uiAuthPassword, (int)uiAuthPasswordLen)) {
      uiAuthPassword[uiAuthPasswordLen - 1] = '\0';
      if (uiAuthPassword[0] == '\0') {
        getDefaultUiAuthPassword(uiAuthPassword, uiAuthPasswordLen);
      }
    } else if (uiAuthPasswordStoredSpecified && uiAuthPasswordStored) {
      if (warningMsg != nullptr && warningMsg->length() == 0) {
        *warningMsg = "WiFi/login settings skipped (backup login data is incomplete)";
      }
      copyBoundedString(uiAuthUsername, uiAuthUsernameLen, g_uiAuthUsername);
      copyBoundedString(uiAuthPassword, uiAuthPasswordLen, g_uiAuthPassword);
    }
  }

  if (controllerLocalSettingsRestoreAllowed &&
      wifiConfiguredMode != nullptr &&
      *wifiConfiguredMode == WIFI_CONFIG_HOME &&
      (wifiClientSsid == nullptr || wifiClientPassword == nullptr ||
       wifiClientSsid[0] == '\0' || wifiClientPassword[0] == '\0')) {
    *errorMsg = "Error: Client mode requires WiFi SSID and password";
    return false;
  }

  /* Parse car profiles */
  int carStart = json.indexOf("\"cars\"");
  if (carStart < 0) { *errorMsg = "Error: missing cars array"; return false; }

  int searchPos = json.indexOf('[', carStart);
  if (searchPos < 0) { *errorMsg = "Error: malformed cars array"; return false; }

  for (int i = 0; i < CAR_MAX_COUNT; i++) {
    /* Find next car object */
    int objStart = json.indexOf('{', searchPos);
    int objEnd = json.indexOf('}', objStart);
    if (objStart < 0 || objEnd < 0) {
      break;  /* fewer profiles in backup than CAR_MAX_COUNT — keep defaults for the rest */
    }
    String carJson = json.substring(objStart, objEnd + 1);
    searchPos = objEnd + 1;

    CarParam_type& c = sv->carParam[i];

    /* Name - parse into a wider buffer; normalizeStoredCarNames() below maps legacy 5-char
       defaults (CAR10..CAR19) and terminates anything longer at 4 chars */
    char tempName[16];
    if (!parseJsonStr(carJson, "name", tempName, sizeof(tempName))) {
      *errorMsg = "Error: missing name in car " + String(i); return false;
    }
    memset(c.carName, 0, CAR_NAME_MAX_SIZE);
    memcpy(c.carName, tempName, min(strlen(tempName), (size_t)CAR_NAME_MAX_SIZE));
    c.carNumber = i;

    /* Numeric fields */
    uint16_t minSpeedRaw = 0;
    if (!parseJsonTenthPercent(carJson, "minSpeed", MIN_SPEED_MAX_VALUE, &minSpeedRaw)) {
      *errorMsg = "Error: invalid minSpeed in car " + String(i); return false;
    }
    c.minSpeed = minSpeedRaw;

    uint16_t brakeRaw = 0;
    if (!parseJsonTenthPercent(carJson, "brake", BRAKE_MAX_VALUE, &brakeRaw)) {
      *errorMsg = "Error: invalid brake in car " + String(i); return false;
    }
    c.brake = brakeRaw;

    if (!parseJsonInt(carJson, "maxSpeed", v) || !inRange(v, 5, 100)) {
      *errorMsg = "Error: invalid maxSpeed in car " + String(i); return false;
    }
    c.maxSpeed = v;

    if (!parseJsonInt(carJson, "curveInput", v) || !inRange(v, 0, THROTTLE_NORMALIZED)) {
      *errorMsg = "Error: invalid curveInput in car " + String(i); return false;
    }
    c.throttleCurveVertex.inputThrottle = v;

    if (!parseJsonInt(carJson, "curveDiff", v) || !inRange(v, THROTTLE_CURVE_SPEED_DIFF_MIN_VALUE, THROTTLE_CURVE_SPEED_DIFF_MAX_VALUE)) {
      *errorMsg = "Error: invalid curveDiff in car " + String(i); return false;
    }
    c.throttleCurveVertex.curveSpeedDiff = v;

    if (parseJsonInt(carJson, "fade", v) && inRange(v, 0, FADE_MAX_VALUE))
      c.fade = v;

    if (!parseJsonInt(carJson, "antiSpin", v) || !inRange(v, 0, ANTISPIN_MAX_VALUE)) {
      *errorMsg = "Error: invalid antiSpin in car " + String(i); return false;
    }
    c.antiSpin = v;

    {
      uint16_t freqMaxRaw = pwmFreqMaxProfileToRaw(
          (pwmFreqMaxProfile != nullptr)
            ? *pwmFreqMaxProfile
            : getConfiguredPwmFreqMaxProfile());
      if (!parseJsonInt(carJson, "freqPWM", v) || !inRange(v, FREQ_MIN_VALUE / 100, freqMaxRaw)) {
        *errorMsg = "Error: invalid freqPWM in car " + String(i); return false;
      }
    }
    c.freqPWM = v;

    if (!parseJsonIntAlias(carJson, "altBrake", "brakeButton", v) || !inRange(v, 0, 100)) {
      *errorMsg = "Error: invalid altBrake in car " + String(i); return false;
    }
    c.brakeButtonReduction = v;

    /* Release-brake fields — optional for backwards compatibility with older backups */
    uint16_t releaseMode = c.quickBrakeEnabled;
    if (parseJsonReleaseModeAlias(carJson, "releaseMode", "quickBrakeEnabled", &releaseMode)) {
      c.quickBrakeEnabled = releaseMode;
    } else if (jsonHasKey(carJson, "releaseMode") || jsonHasKey(carJson, "quickBrakeEnabled")) {
      *errorMsg = "Error: invalid releaseMode in car " + String(i); return false;
    }

    if (parseJsonIntAlias(carJson, "releaseZone", "quickBrakeThreshold", v)) {
      if (!inRange(v, 0, QUICK_BRAKE_THRESHOLD_MAX)) {
        *errorMsg = "Error: invalid releaseZone in car " + String(i); return false;
      }
      c.quickBrakeThreshold = v;
    } else if (jsonHasKey(carJson, "releaseZone") || jsonHasKey(carJson, "quickBrakeThreshold")) {
      *errorMsg = "Error: invalid releaseZone in car " + String(i); return false;
    }

    if (parseJsonIntAlias(carJson, "releaseLevel", "quickBrakeStrength", v)) {
      if (!inRange(v, 0, QUICK_BRAKE_STRENGTH_MAX)) {
        *errorMsg = "Error: invalid releaseLevel in car " + String(i); return false;
      }
      c.quickBrakeStrength = v;
    } else if (jsonHasKey(carJson, "releaseLevel") || jsonHasKey(carJson, "quickBrakeStrength")) {
      *errorMsg = "Error: invalid releaseLevel in car " + String(i); return false;
    }
  }

  normalizeStoredCarNames(sv);
  clampStoredVarCarPwmFreqsToProfile(sv,
                                     (pwmFreqMaxProfile != nullptr)
                                       ? *pwmFreqMaxProfile
                                       : getConfiguredPwmFreqMaxProfile());
  return true;
}

void appendJsonEscaped(String& out, const char* in) {
  if (in == nullptr) return;
  while (*in) {
    char c = *in++;
    if (c == '\"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out += c;
    }
  }
}

static void appendTenthPercentField(String& out, const char* key, uint16_t raw, bool withTrailingComma) {
  char buf[48];
  snprintf(buf, sizeof(buf), "\"%s\":%u.%u%s",
           key,
           raw / 10U,
           raw % 10U,
           withTrailingComma ? "," : "");
  out += buf;
}

static void appendSchemaIntField(
  String& out, bool& first, const char* id, const char* label,
  int32_t minVal, int32_t maxVal, int32_t step, const char* unit = nullptr) {
  if (!first) out += ",";
  first = false;
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"id\":\"%s\",\"label\":\"%s\",\"type\":\"int\",\"min\":%ld,\"max\":%ld,\"step\":%ld",
           id, label, (long)minVal, (long)maxVal, (long)step);
  out += buf;
  if (unit != nullptr && unit[0] != '\0') {
    out += ",\"unit\":\"";
    out += unit;
    out += "\"";
  }
  out += "}";
}

static void appendSchemaNumberField(
  String& out, bool& first, const char* id, const char* label,
  const char* minVal, const char* maxVal, const char* step, const char* unit = nullptr) {
  if (!first) out += ",";
  first = false;
  out += "{\"id\":\"";
  out += id;
  out += "\",\"label\":\"";
  out += label;
  out += "\",\"type\":\"int\",\"min\":";
  out += minVal;
  out += ",\"max\":";
  out += maxVal;
  out += ",\"step\":";
  out += step;
  if (unit != nullptr && unit[0] != '\0') {
    out += ",\"unit\":\"";
    out += unit;
    out += "\"";
  }
  out += "}";
}

static void appendSchemaStringField(
  String& out, bool& first, const char* id, const char* label, int32_t maxLen, bool secret = false) {
  if (!first) out += ",";
  first = false;
  char buf[200];
  snprintf(buf, sizeof(buf),
           "{\"id\":\"%s\",\"label\":\"%s\",\"type\":\"string\",\"maxLen\":%ld",
           id, label, (long)maxLen);
  out += buf;
  if (secret) {
    out += ",\"secret\":true";
  }
  out += "}";
}

static void appendSchemaEnumField(
  String& out, bool& first, const char* id, const char* label, const char* optionsJson) {
  if (!first) out += ",";
  first = false;
  out += "{\"id\":\"";
  out += id;
  out += "\",\"label\":\"";
  out += label;
  out += "\",\"type\":\"enum\",\"options\":";
  out += optionsJson;
  out += "}";
}

String buildSchemaJson() {
  loadWiFiNetworkSettingsIfNeeded();
  String json;
  json.reserve(5500);

  char buf[64];
  json += "{";
  snprintf(buf, sizeof(buf), "\"fwVersion\":\"%d.%d\",", SW_MAJOR_VERSION, SW_MINOR_VERSION);
  json += buf;
  snprintf(buf, sizeof(buf), "\"carCount\":%d,", CAR_MAX_COUNT);
  json += buf;

  json += "\"global\":[";
  bool first = true;
  appendSchemaIntField(json, first, "selectedCarNumber", "Active Car", 0, CAR_MAX_COUNT - 1, 1);
  appendSchemaEnumField(json, first, "viewMode", "View Mode",
                        "[{\"value\":0,\"label\":\"LIST\"},{\"value\":1,\"label\":\"GRID\"}]");
  appendSchemaIntField(json, first, "screensaverTimeout", "Screensaver Timeout", 0, SCREENSAVER_TIMEOUT_MAX, 1, "s");
  appendSchemaIntField(json, first, "powerSaveTimeout", "Sleep Timeout", 0, POWER_SAVE_TIMEOUT_MAX, 1, "min");
  appendSchemaIntField(json, first, "deepSleepTimeout", "Deep Sleep Timeout", 0, DEEP_SLEEP_TIMEOUT_MAX, 1, "min");
  appendSchemaEnumField(json, first, "soundBoot", "Boot Sound",
                        "[{\"value\":0,\"label\":\"OFF\"},{\"value\":1,\"label\":\"ON\"}]");
  appendSchemaEnumField(json, first, "soundRace", "Race Sound",
                        "[{\"value\":0,\"label\":\"OFF\"},{\"value\":1,\"label\":\"ON\"}]");
  appendSchemaEnumField(json, first, "antiSpinDisplayMode", "ANTIS Display",
                        "[{\"value\":0,\"label\":\"ms\"},{\"value\":1,\"label\":\"%\"},{\"value\":2,\"label\":\"TEXT\"}]");
  appendSchemaIntField(json, first, "antiSpinStepMs", "ANTIS Step (ms)", ANTISPIN_STEP_MIN, ANTISPIN_STEP_MAX, 1, "ms");
  appendSchemaIntField(json, first, "antiSpinStepPct", "ANTIS Step (%)", ANTISPIN_STEP_PCT_MIN, ANTISPIN_STEP_PCT_MAX, 1, "%");
  appendSchemaNumberField(json, first, "brakeStep", "Brake Step", "0.1", "50.0", "0.1", "%");
  appendSchemaNumberField(json, first, "sensiStep", "Sensi Step", "0.1", "5.0", "0.1", "%");
  appendSchemaEnumField(json, first, "advancedMenu", "Advanced Menu",
                        "[{\"value\":0,\"label\":\"OFF\"},{\"value\":1,\"label\":\"ON\"}]");
  appendSchemaEnumField(json, first, "encoderInvert", "ENC INV",
                        "[{\"value\":0,\"label\":\"OFF\"},{\"value\":1,\"label\":\"ON\"}]");
  appendSchemaEnumField(json, first, "pwmFreqMax", "PWM_F Max",
                        "[{\"value\":5,\"label\":\"5 kHz\"},{\"value\":10,\"label\":\"10 kHz\"},{\"value\":20,\"label\":\"20 kHz\"},{\"value\":25,\"label\":\"25 kHz\"}]");
  appendSchemaIntField(json, first, "adcVoltageRangeMv", "VIN CAL ADC", ADC_VOLTAGE_RANGE_MIN_MVOLTS, ADC_VOLTAGE_RANGE_MAX_MVOLTS, 1, "mV");
  appendSchemaEnumField(json, first, "gridCarSelectEnabled", "Grid Car Select",
                        "[{\"value\":0,\"label\":\"OFF\"},{\"value\":1,\"label\":\"ON\"}]");
  appendSchemaEnumField(json, first, "raceViewMode", "Race Mode",
                        "[{\"value\":0,\"label\":\"OFF\"},{\"value\":1,\"label\":\"FULL\"},{\"value\":2,\"label\":\"SIMPLE\"}]");
  appendSchemaEnumField(json, first, "language", "Language",
                        "[{\"value\":0,\"label\":\"NOR\"},{\"value\":1,\"label\":\"ENG\"},{\"value\":2,\"label\":\"CS\"},{\"value\":3,\"label\":\"ACD\"},{\"value\":4,\"label\":\"ESP\"},{\"value\":5,\"label\":\"DEU\"},{\"value\":6,\"label\":\"ITA\"},{\"value\":7,\"label\":\"NLD\"},{\"value\":8,\"label\":\"POR\"},{\"value\":9,\"label\":\"SVE\"}]");
  appendSchemaEnumField(json, first, "textCase", "Text Case",
                        "[{\"value\":0,\"label\":\"UPPER\"},{\"value\":1,\"label\":\"Pascal\"}]");
  appendSchemaEnumField(json, first, "listFontSize", "Font Size",
                        "[{\"value\":0,\"label\":\"LARGE\"},{\"value\":1,\"label\":\"small\"}]");
  appendSchemaIntField(json, first, "startupDelay", "Startup Delay", STARTUP_DELAY_MIN, STARTUP_DELAY_MAX, 1, "x10ms");
  appendSchemaEnumField(json, first, "wifiMode", "WiFi Mode",
                        "[{\"value\":0,\"label\":\"AP\"},{\"value\":1,\"label\":\"CLIENT\"}]");
  appendSchemaStringField(json, first, "wifiStaSsid", "Client WiFi SSID", WIFI_STA_SSID_MAX_LEN);
  appendSchemaStringField(json, first, "wifiStaPassword", "Client WiFi Password", WIFI_STA_PASS_MAX_LEN, true);
  appendSchemaEnumField(json, first, "wifiStaPasswordClear", "Clear Saved Client WiFi Password",
                        "[{\"value\":0,\"label\":\"KEEP\"},{\"value\":1,\"label\":\"CLEAR\"}]");
  appendSchemaStringField(json, first, "uiAuthPassword", "Controller Login Password", UI_AUTH_PASS_MAX_LEN, true);
  appendSchemaEnumField(json, first, "uiAuthResetDefault", "Reset Controller Login To Default",
                        "[{\"value\":0,\"label\":\"KEEP\"},{\"value\":1,\"label\":\"RESET\"}]");
  appendSchemaEnumField(json, first, "statusSlot0", "Status Slot 1",
                        "[{\"value\":0,\"label\":\"BLANK\"},{\"value\":1,\"label\":\"OUTPUT\"},{\"value\":2,\"label\":\"THROTTLE\"},{\"value\":3,\"label\":\"CAR\"},{\"value\":4,\"label\":\"CURR\"},{\"value\":5,\"label\":\"VOLTAGE\"},{\"value\":8,\"label\":\"BATTERY\"},{\"value\":7,\"label\":\"ACTIVE BRAKE\"},{\"value\":9,\"label\":\"DEVICE ID\"}]");
  appendSchemaEnumField(json, first, "statusSlot1", "Status Slot 2",
                        "[{\"value\":0,\"label\":\"BLANK\"},{\"value\":1,\"label\":\"OUTPUT\"},{\"value\":2,\"label\":\"THROTTLE\"},{\"value\":3,\"label\":\"CAR\"},{\"value\":4,\"label\":\"CURR\"},{\"value\":5,\"label\":\"VOLTAGE\"},{\"value\":8,\"label\":\"BATTERY\"},{\"value\":7,\"label\":\"ACTIVE BRAKE\"},{\"value\":9,\"label\":\"DEVICE ID\"}]");
  appendSchemaEnumField(json, first, "statusSlot2", "Status Slot 3",
                        "[{\"value\":0,\"label\":\"BLANK\"},{\"value\":1,\"label\":\"OUTPUT\"},{\"value\":2,\"label\":\"THROTTLE\"},{\"value\":3,\"label\":\"CAR\"},{\"value\":4,\"label\":\"CURR\"},{\"value\":5,\"label\":\"VOLTAGE\"},{\"value\":8,\"label\":\"BATTERY\"},{\"value\":7,\"label\":\"ACTIVE BRAKE\"},{\"value\":9,\"label\":\"DEVICE ID\"}]");
  appendSchemaEnumField(json, first, "statusSlot3", "Status Slot 4",
                        "[{\"value\":0,\"label\":\"BLANK\"},{\"value\":1,\"label\":\"OUTPUT\"},{\"value\":2,\"label\":\"THROTTLE\"},{\"value\":3,\"label\":\"CAR\"},{\"value\":4,\"label\":\"CURR\"},{\"value\":5,\"label\":\"VOLTAGE\"},{\"value\":8,\"label\":\"BATTERY\"},{\"value\":7,\"label\":\"ACTIVE BRAKE\"},{\"value\":9,\"label\":\"DEVICE ID\"}]");
  appendSchemaStringField(json, first, "screensaverLine1", "Screensaver Line 1", SCREENSAVER_TEXT_MAX - 1);
  appendSchemaStringField(json, first, "screensaverLine2", "Screensaver Line 2", SCREENSAVER_TEXT_MAX - 1);
  json += "],";

  json += "\"car\":[";
  first = true;
  appendSchemaStringField(json, first, "carName", "Profile Name", CAR_NAME_MAX_SIZE - 1);
  appendSchemaNumberField(json, first, "minSpeed", "SENSI", "0.0", "90.0", "0.1", "%");
  appendSchemaNumberField(json, first, "brake", "BRAKE", "0.0", "100.0", "0.1", "%");
  appendSchemaIntField(json, first, "maxSpeed", "LIMIT", 5, 100, 1, "%");
  appendSchemaIntField(json, first, "curveDiff", "CURVE", THROTTLE_CURVE_SPEED_DIFF_MIN_VALUE, THROTTLE_CURVE_SPEED_DIFF_MAX_VALUE, 1, "%");
  appendSchemaIntField(json, first, "fade", "FADE", 0, FADE_MAX_VALUE, 1, "%");
  appendSchemaIntField(json, first, "antiSpin", "ANTIS", 0, ANTISPIN_MAX_VALUE, 1, "ms");
  appendSchemaIntField(json, first, "freqPWM", "PWM_F", FREQ_MIN_VALUE / 100, getConfiguredPwmFreqMaxRaw(), 1, "x0.1kHz");
  appendSchemaIntField(json, first, "brakeButton", "Alt.Brake", 0, 100, 1, "%");
  appendSchemaEnumField(json, first, "quickBrakeEnabled", "Rel.Brake",
                        "[{\"value\":0,\"label\":\"OFF\"},{\"value\":1,\"label\":\"QUICK\"},{\"value\":2,\"label\":\"DRAG\"}]");
  appendSchemaIntField(json, first, "quickBrakeThreshold", "Rel.Brake Zone", 0, QUICK_BRAKE_THRESHOLD_MAX, 1, "%");
  appendSchemaIntField(json, first, "quickBrakeStrength", "Rel.Brake Level", 0, QUICK_BRAKE_STRENGTH_MAX, 1, "%");
  json += "]";
  json += "}";
  return json;
}

uint8_t getRequestedCarIndex() {
  if (g_wifiServer != nullptr && g_wifiServer->hasArg("car")) {
    int32_t v = g_wifiServer->arg("car").toInt();
    if (v >= 0 && v < CAR_MAX_COUNT) return (uint8_t)v;
  }
  return (uint8_t)g_storedVar.selectedCarNumber;
}

String buildStateJson(uint8_t carIndex) {
  loadWiFiNetworkSettingsIfNeeded();
  if (carIndex >= CAR_MAX_COUNT) carIndex = (uint8_t)g_storedVar.selectedCarNumber;
  const CarParam_type& c = g_storedVar.carParam[carIndex];

  String json;
  json.reserve(2600);
  char buf[160];

  json += "{";
  snprintf(buf, sizeof(buf), "\"selectedCarNumber\":%u,", g_storedVar.selectedCarNumber);
  json += buf;
  snprintf(buf, sizeof(buf), "\"carIndex\":%u,", carIndex);
  json += buf;

  json += "\"global\":{";
  snprintf(buf, sizeof(buf), "\"selectedCarNumber\":%u,", g_storedVar.selectedCarNumber); json += buf;
  snprintf(buf, sizeof(buf), "\"viewMode\":%u,", g_storedVar.viewMode); json += buf;
  snprintf(buf, sizeof(buf), "\"screensaverTimeout\":%u,", g_storedVar.screensaverTimeout); json += buf;
  snprintf(buf, sizeof(buf), "\"powerSaveTimeout\":%u,", g_storedVar.powerSaveTimeout); json += buf;
  snprintf(buf, sizeof(buf), "\"deepSleepTimeout\":%u,", g_storedVar.deepSleepTimeout); json += buf;
  snprintf(buf, sizeof(buf), "\"soundBoot\":%u,", g_storedVar.soundBoot); json += buf;
  snprintf(buf, sizeof(buf), "\"soundRace\":%u,", g_storedVar.soundRace); json += buf;
  snprintf(buf, sizeof(buf), "\"antiSpinDisplayMode\":%u,", g_antiSpinDisplayMode); json += buf;
  snprintf(buf, sizeof(buf), "\"antiSpinStepMs\":%u,", g_antiSpinStepMs); json += buf;
  snprintf(buf, sizeof(buf), "\"antiSpinStepPct\":%u,", g_antiSpinStepPct); json += buf;
  snprintf(buf, sizeof(buf), "\"brakeStep\":%u.%u,", g_brakeStep / BRAKE_SCALE, brakeFracDigit(g_brakeStep)); json += buf;
  snprintf(buf, sizeof(buf), "\"sensiStep\":%u.%u,", g_sensiStep / SENSI_SCALE, sensiFracDigit(g_sensiStep)); json += buf;
  snprintf(buf, sizeof(buf), "\"advancedMenu\":%u,", g_advancedMenuEnabled ? 1 : 0); json += buf;
  snprintf(buf, sizeof(buf), "\"encoderInvert\":%u,", g_encoderInvertEnabled ? 1 : 0); json += buf;
  snprintf(buf, sizeof(buf), "\"pwmFreqMax\":%u,", getConfiguredPwmFreqMaxKHz()); json += buf;
  snprintf(buf, sizeof(buf), "\"adcVoltageRangeMv\":%u,", g_adcVoltageRange_mV); json += buf;
  snprintf(buf, sizeof(buf), "\"gridCarSelectEnabled\":%u,", g_storedVar.gridCarSelectEnabled); json += buf;
  snprintf(buf, sizeof(buf), "\"raceViewMode\":%u,", g_storedVar.raceViewMode); json += buf;
  snprintf(buf, sizeof(buf), "\"language\":%u,", g_storedVar.language); json += buf;
  snprintf(buf, sizeof(buf), "\"textCase\":%u,", g_storedVar.textCase); json += buf;
  snprintf(buf, sizeof(buf), "\"listFontSize\":%u,", g_storedVar.listFontSize); json += buf;
  snprintf(buf, sizeof(buf), "\"startupDelay\":%u,", g_storedVar.startupDelay); json += buf;
  snprintf(buf, sizeof(buf), "\"wifiMode\":%u,", g_wifiConfiguredMode); json += buf;
  snprintf(buf, sizeof(buf), "\"statusSlot0\":%u,", normalizeStatusSlotForUi(g_storedVar.statusSlot[0])); json += buf;
  snprintf(buf, sizeof(buf), "\"statusSlot1\":%u,", normalizeStatusSlotForUi(g_storedVar.statusSlot[1])); json += buf;
  snprintf(buf, sizeof(buf), "\"statusSlot2\":%u,", normalizeStatusSlotForUi(g_storedVar.statusSlot[2])); json += buf;
  snprintf(buf, sizeof(buf), "\"statusSlot3\":%u,", normalizeStatusSlotForUi(g_storedVar.statusSlot[3])); json += buf;
  json += "\"screensaverLine1\":\"";
  appendJsonEscaped(json, g_storedVar.screensaverLine1);
  json += "\",\"screensaverLine2\":\"";
  appendJsonEscaped(json, g_storedVar.screensaverLine2);
  json += "\",\"wifiStaSsid\":\"";
  appendJsonEscaped(json, g_wifiClientSsid);
  snprintf(buf, sizeof(buf),
           "\",\"wifiStaPasswordSet\":%u,\"wifiStaPasswordClear\":0,\"uiAuthPasswordSet\":%u,\"uiAuthResetDefault\":0,\"uiAuthDefault\":%u},",
           g_wifiClientPassword[0] != '\0' ? 1U : 0U,
           g_uiAuthPassword[0] != '\0' ? 1U : 0U,
           areCurrentUiAuthCredentialsDefault() ? 1U : 0U);
  json += buf;

  json += "\"car\":{";
  json += "\"carName\":\"";
  appendJsonEscaped(json, c.carName);
  json += "\",";
  appendTenthPercentField(json, "minSpeed", c.minSpeed, true);
  appendTenthPercentField(json, "brake", c.brake, true);
  snprintf(buf, sizeof(buf), "\"maxSpeed\":%u,", c.maxSpeed); json += buf;
  snprintf(buf, sizeof(buf), "\"curveDiff\":%u,", c.throttleCurveVertex.curveSpeedDiff); json += buf;
  snprintf(buf, sizeof(buf), "\"fade\":%u,", c.fade); json += buf;
  snprintf(buf, sizeof(buf), "\"antiSpin\":%u,", c.antiSpin); json += buf;
  snprintf(buf, sizeof(buf), "\"freqPWM\":%u,", c.freqPWM); json += buf;
  snprintf(buf, sizeof(buf), "\"brakeButton\":%u,", c.brakeButtonReduction); json += buf;
  snprintf(buf, sizeof(buf), "\"quickBrakeEnabled\":%u,", c.quickBrakeEnabled); json += buf;
  snprintf(buf, sizeof(buf), "\"quickBrakeThreshold\":%u,", c.quickBrakeThreshold); json += buf;
  snprintf(buf, sizeof(buf), "\"quickBrakeStrength\":%u", c.quickBrakeStrength); json += buf;
  json += "}";
  json += "}";
  return json;
}

bool parseAndApplyWebPatch(const String& json, String* errorMsg, uint8_t* appliedCarIndex) {
  StoredVar_type updated = g_storedVar;
  loadWiFiNetworkSettingsIfNeeded();
  uint16_t wifiConfiguredMode = g_wifiConfiguredMode;
  uint16_t antiSpinDisplayMode = g_antiSpinDisplayMode;
  uint16_t antiSpinStepMs = g_antiSpinStepMs;
  uint16_t antiSpinStepPct = g_antiSpinStepPct;
  uint16_t brakeStepRaw = g_brakeStep;
  uint16_t sensiStepRaw = g_sensiStep;
  uint16_t advancedMenuEnabled = g_advancedMenuEnabled ? 1 : 0;
  uint16_t encoderInvertEnabled = g_encoderInvertEnabled ? 1 : 0;
  uint16_t pwmFreqMaxProfile = getConfiguredPwmFreqMaxProfile();
  uint16_t adcVoltageRangeMv = g_adcVoltageRange_mV;
  char wifiClientSsid[WIFI_STA_SSID_MAX_LEN + 1];
  char wifiClientPassword[WIFI_STA_PASS_MAX_LEN + 1];
  char requestedWiFiPassword[WIFI_STA_PASS_MAX_LEN + 1];
  char uiAuthUsername[UI_AUTH_USER_MAX_LEN + 1];
  char uiAuthPassword[UI_AUTH_PASS_MAX_LEN + 1];
  char requestedUiAuthPassword[UI_AUTH_PASS_MAX_LEN + 1];
  copyBoundedString(wifiClientSsid, sizeof(wifiClientSsid), g_wifiClientSsid);
  copyBoundedString(wifiClientPassword, sizeof(wifiClientPassword), g_wifiClientPassword);
  copyBoundedString(uiAuthUsername, sizeof(uiAuthUsername), g_uiAuthUsername);
  copyBoundedString(uiAuthPassword, sizeof(uiAuthPassword), g_uiAuthPassword);
  requestedWiFiPassword[0] = '\0';
  requestedUiAuthPassword[0] = '\0';
  int32_t v;
  uint8_t carIndex = updated.selectedCarNumber;
  bool wifiPasswordProvided = false;
  bool wifiPasswordClearRequested = false;
  bool uiAuthPasswordProvided = false;
  bool uiAuthResetDefaultRequested = false;

  if (parseJsonInt(json, "carIndex", v)) {
    if (!inRange(v, 0, CAR_MAX_COUNT - 1)) {
      *errorMsg = "Error: invalid carIndex";
      return false;
    }
    carIndex = (uint8_t)v;
  }

  if (parseJsonInt(json, "selectedCarNumber", v)) {
    if (!inRange(v, 0, CAR_MAX_COUNT - 1)) { *errorMsg = "Error: invalid selectedCarNumber"; return false; }
    updated.selectedCarNumber = (uint16_t)v;
  }
  if (parseJsonInt(json, "viewMode", v)) {
    if (!inRange(v, VIEW_MODE_LIST, VIEW_MODE_GRID)) { *errorMsg = "Error: invalid viewMode"; return false; }
    updated.viewMode = (uint16_t)v;
  }
  if (parseJsonInt(json, "screensaverTimeout", v)) {
    if (!inRange(v, 0, SCREENSAVER_TIMEOUT_MAX)) { *errorMsg = "Error: invalid screensaverTimeout"; return false; }
    updated.screensaverTimeout = (uint16_t)v;
  }
  if (parseJsonInt(json, "powerSaveTimeout", v)) {
    if (!inRange(v, 0, POWER_SAVE_TIMEOUT_MAX)) { *errorMsg = "Error: invalid powerSaveTimeout"; return false; }
    updated.powerSaveTimeout = (uint16_t)v;
  }
  if (parseJsonInt(json, "deepSleepTimeout", v)) {
    if (!(v == 0 || inRange(v, DEEP_SLEEP_TIMEOUT_MIN, DEEP_SLEEP_TIMEOUT_MAX))) {
      *errorMsg = "Error: invalid deepSleepTimeout";
      return false;
    }
    updated.deepSleepTimeout = (uint16_t)v;
  }
  if (parseJsonInt(json, "soundBoot", v)) {
    if (!inRange(v, 0, 1)) { *errorMsg = "Error: invalid soundBoot"; return false; }
    updated.soundBoot = (uint16_t)v;
  }
  if (parseJsonInt(json, "soundRace", v)) {
    if (!inRange(v, 0, 1)) { *errorMsg = "Error: invalid soundRace"; return false; }
    updated.soundRace = (uint16_t)v;
  }
  if (parseJsonInt(json, "antiSpinDisplayMode", v)) {
    if (!inRange(v, ANTISPIN_UI_MODE_MS, ANTISPIN_UI_MODE_TEXT)) { *errorMsg = "Error: invalid antiSpinDisplayMode"; return false; }
    antiSpinDisplayMode = (uint16_t)v;
  }
  if (parseJsonInt(json, "antiSpinStep", v) || parseJsonInt(json, "antiSpinStepMs", v)) {
    if (!inRange(v, ANTISPIN_STEP_MIN, ANTISPIN_STEP_MAX)) { *errorMsg = "Error: invalid antiSpinStepMs"; return false; }
    antiSpinStepMs = (uint16_t)v;
  }
  if (parseJsonInt(json, "antiSpinStepPct", v)) {
    if (!inRange(v, ANTISPIN_STEP_PCT_MIN, ANTISPIN_STEP_PCT_MAX)) { *errorMsg = "Error: invalid antiSpinStepPct"; return false; }
    antiSpinStepPct = (uint16_t)v;
  }
  if (jsonHasKey(json, "brakeStep")) {
    uint16_t stepRaw = 0;
    if (!parseJsonTenthPercent(json, "brakeStep", BRAKE_STEP_MAX, &stepRaw) || stepRaw < BRAKE_STEP_MIN) {
      *errorMsg = "Error: invalid brakeStep"; return false;
    }
    brakeStepRaw = stepRaw;
  }
  if (jsonHasKey(json, "sensiStep")) {
    uint16_t stepRaw = 0;
    if (!parseJsonTenthPercent(json, "sensiStep", SENSI_STEP_MAX, &stepRaw) || stepRaw < SENSI_STEP_MIN) {
      *errorMsg = "Error: invalid sensiStep"; return false;
    }
    sensiStepRaw = stepRaw;
  }
  if (parseJsonInt(json, "advancedMenu", v) || parseJsonInt(json, "advancedMenuEnabled", v)) {
    if (!inRange(v, 0, 1)) { *errorMsg = "Error: invalid advancedMenu"; return false; }
    advancedMenuEnabled = (uint16_t)v;
  }
  if (parseJsonInt(json, "encoderInvert", v)) {
    if (!inRange(v, 0, 1)) { *errorMsg = "Error: invalid encoderInvert"; return false; }
    encoderInvertEnabled = (uint16_t)v;
  }
  if (parseJsonInt(json, "pwmFreqMax", v)) {
    if (!parsePwmFreqMaxProfileValue(v, &pwmFreqMaxProfile)) {
      *errorMsg = "Error: invalid pwmFreqMax";
      return false;
    }
  }
  if (parseJsonInt(json, "adcVoltageRangeMv", v)) {
    if (!inRange(v, ADC_VOLTAGE_RANGE_MIN_MVOLTS, ADC_VOLTAGE_RANGE_MAX_MVOLTS)) {
      *errorMsg = "Error: invalid adcVoltageRangeMv";
      return false;
    }
    adcVoltageRangeMv = (uint16_t)v;
  }
  if (parseJsonInt(json, "gridCarSelectEnabled", v)) {
    if (!inRange(v, 0, 1)) { *errorMsg = "Error: invalid gridCarSelectEnabled"; return false; }
    updated.gridCarSelectEnabled = (uint16_t)v;
  }
  if (parseJsonInt(json, "raceViewMode", v)) {
    if (!inRange(v, RACE_VIEW_OFF, RACE_VIEW_SIMPLE)) { *errorMsg = "Error: invalid raceViewMode"; return false; }
    updated.raceViewMode = (uint16_t)v;
  }
  if (parseJsonInt(json, "language", v)) {
    if (!inRange(v, LANG_NOR, LANG_MAX)) { *errorMsg = "Error: invalid language"; return false; }
    updated.language = (uint16_t)v;
  }
  if (parseJsonInt(json, "textCase", v)) {
    if (!inRange(v, TEXT_CASE_UPPER, TEXT_CASE_PASCAL)) { *errorMsg = "Error: invalid textCase"; return false; }
    updated.textCase = (uint16_t)v;
  }
  if (parseJsonInt(json, "listFontSize", v)) {
    if (!inRange(v, FONT_SIZE_LARGE, FONT_SIZE_SMALL)) { *errorMsg = "Error: invalid listFontSize"; return false; }
    updated.listFontSize = (uint16_t)v;
  }
  if (parseJsonInt(json, "startupDelay", v)) {
    if (!inRange(v, STARTUP_DELAY_MIN, STARTUP_DELAY_MAX)) { *errorMsg = "Error: invalid startupDelay"; return false; }
    updated.startupDelay = (uint16_t)v;
  }
  if (parseJsonInt(json, "wifiMode", v)) {
    if (!inRange(v, WIFI_CONFIG_AP, WIFI_CONFIG_HOME)) { *errorMsg = "Error: invalid wifiMode"; return false; }
    wifiConfiguredMode = (uint16_t)v;
  }
  if (parseJsonStr(json, "uiAuthUsername", uiAuthUsername, sizeof(uiAuthUsername))) {
    uiAuthUsername[sizeof(uiAuthUsername) - 1] = '\0';
    if (uiAuthUsername[0] == '\0') {
      getDefaultUiAuthUsername(uiAuthUsername, sizeof(uiAuthUsername));
    }
  }
  if (parseJsonInt(json, "statusSlot0", v)) {
    if (v == STATUS_CURRENT_MA) v = STATUS_CURRENT;
    if (!inRange(v, STATUS_BLANK, STATUS_DEVICE_ID)) { *errorMsg = "Error: invalid statusSlot0"; return false; }
    updated.statusSlot[0] = (uint16_t)v;
  }
  if (parseJsonInt(json, "statusSlot1", v)) {
    if (v == STATUS_CURRENT_MA) v = STATUS_CURRENT;
    if (!inRange(v, STATUS_BLANK, STATUS_DEVICE_ID)) { *errorMsg = "Error: invalid statusSlot1"; return false; }
    updated.statusSlot[1] = (uint16_t)v;
  }
  if (parseJsonInt(json, "statusSlot2", v)) {
    if (v == STATUS_CURRENT_MA) v = STATUS_CURRENT;
    if (!inRange(v, STATUS_BLANK, STATUS_DEVICE_ID)) { *errorMsg = "Error: invalid statusSlot2"; return false; }
    updated.statusSlot[2] = (uint16_t)v;
  }
  if (parseJsonInt(json, "statusSlot3", v)) {
    if (v == STATUS_CURRENT_MA) v = STATUS_CURRENT;
    if (!inRange(v, STATUS_BLANK, STATUS_DEVICE_ID)) { *errorMsg = "Error: invalid statusSlot3"; return false; }
    updated.statusSlot[3] = (uint16_t)v;
  }

  char tempStr[SCREENSAVER_TEXT_MAX];
  if (parseJsonStr(json, "screensaverLine1", tempStr, SCREENSAVER_TEXT_MAX)) {
    strncpy(updated.screensaverLine1, tempStr, SCREENSAVER_TEXT_MAX);
    updated.screensaverLine1[SCREENSAVER_TEXT_MAX - 1] = '\0';
  }
  if (parseJsonStr(json, "screensaverLine2", tempStr, SCREENSAVER_TEXT_MAX)) {
    strncpy(updated.screensaverLine2, tempStr, SCREENSAVER_TEXT_MAX);
    updated.screensaverLine2[SCREENSAVER_TEXT_MAX - 1] = '\0';
  }
  if (parseJsonStr(json, "wifiStaSsid", wifiClientSsid, sizeof(wifiClientSsid))) {
    wifiClientSsid[sizeof(wifiClientSsid) - 1] = '\0';
  }
  if (parseJsonStr(json, "wifiStaPassword", requestedWiFiPassword, sizeof(requestedWiFiPassword))) {
    requestedWiFiPassword[sizeof(requestedWiFiPassword) - 1] = '\0';
    wifiPasswordProvided = true;
  }
  if (parseJsonInt(json, "wifiStaPasswordClear", v)) {
    if (!inRange(v, 0, 1)) { *errorMsg = "Error: invalid wifiStaPasswordClear"; return false; }
    wifiPasswordClearRequested = (v != 0);
  }
  if (parseJsonStr(json, "uiAuthPassword", requestedUiAuthPassword, sizeof(requestedUiAuthPassword))) {
    requestedUiAuthPassword[sizeof(requestedUiAuthPassword) - 1] = '\0';
    uiAuthPasswordProvided = true;
  }
  if (parseJsonInt(json, "uiAuthResetDefault", v)) {
    if (!inRange(v, 0, 1)) { *errorMsg = "Error: invalid uiAuthResetDefault"; return false; }
    uiAuthResetDefaultRequested = (v != 0);
  }
  if (wifiPasswordClearRequested && wifiPasswordProvided && requestedWiFiPassword[0] != '\0') {
    *errorMsg = "Error: enter password or choose clear, not both";
    return false;
  }
  if (uiAuthResetDefaultRequested && uiAuthPasswordProvided && requestedUiAuthPassword[0] != '\0') {
    *errorMsg = "Error: enter controller login password or choose reset, not both";
    return false;
  }
  if (wifiPasswordClearRequested) {
    wifiClientPassword[0] = '\0';
  } else if (wifiPasswordProvided && requestedWiFiPassword[0] != '\0') {
    copyBoundedString(wifiClientPassword, sizeof(wifiClientPassword), requestedWiFiPassword);
  }
  if (uiAuthResetDefaultRequested) {
    getDefaultUiAuthUsername(uiAuthUsername, sizeof(uiAuthUsername));
    getDefaultUiAuthPassword(uiAuthPassword, sizeof(uiAuthPassword));
  } else if (uiAuthPasswordProvided && requestedUiAuthPassword[0] != '\0') {
    copyBoundedString(uiAuthPassword, sizeof(uiAuthPassword), requestedUiAuthPassword);
  }

  if (wifiConfiguredMode == WIFI_CONFIG_HOME &&
      (wifiClientSsid[0] == '\0' || wifiClientPassword[0] == '\0')) {
    *errorMsg = "Error: Client mode requires WiFi SSID and password";
    return false;
  }

  CarParam_type car = updated.carParam[carIndex];
  uint16_t minSpeedRaw = car.minSpeed;
  uint16_t maxSpeed = car.maxSpeed;

  if (json.indexOf("\"minSpeed\"") >= 0) {
    if (!parseJsonTenthPercent(json, "minSpeed", MIN_SPEED_MAX_VALUE, &minSpeedRaw)) { *errorMsg = "Error: invalid minSpeed"; return false; }
  }
  if (parseJsonInt(json, "maxSpeed", v)) {
    if (!inRange(v, 5, 100)) { *errorMsg = "Error: invalid maxSpeed"; return false; }
    maxSpeed = (uint16_t)v;
  }
  if (maxSpeed < (uint16_t)(sensiToWholePctCeil(minSpeedRaw) + 5)) {
    *errorMsg = "Error: maxSpeed must be at least minSpeed+5";
    return false;
  }
  car.minSpeed = minSpeedRaw;
  car.maxSpeed = maxSpeed;

  if (jsonHasKey(json, "brake")) {
    uint16_t brakeRaw = 0;
    if (!parseJsonTenthPercent(json, "brake", BRAKE_MAX_VALUE, &brakeRaw)) { *errorMsg = "Error: invalid brake"; return false; }
    car.brake = brakeRaw;
  }
  if (parseJsonInt(json, "curveDiff", v)) {
    if (!inRange(v, THROTTLE_CURVE_SPEED_DIFF_MIN_VALUE, THROTTLE_CURVE_SPEED_DIFF_MAX_VALUE)) {
      *errorMsg = "Error: invalid curveDiff"; return false;
    }
    car.throttleCurveVertex.curveSpeedDiff = (uint16_t)v;
  }
  if (parseJsonInt(json, "fade", v)) {
    if (!inRange(v, 0, FADE_MAX_VALUE)) { *errorMsg = "Error: invalid fade"; return false; }
    car.fade = (uint16_t)v;
  }
  if (parseJsonInt(json, "antiSpin", v)) {
    if (!inRange(v, 0, ANTISPIN_MAX_VALUE)) { *errorMsg = "Error: invalid antiSpin"; return false; }
    car.antiSpin = (uint16_t)v;
  }
  if (parseJsonInt(json, "freqPWM", v)) {
    if (!inRange(v, FREQ_MIN_VALUE / 100, pwmFreqMaxProfileToRaw(pwmFreqMaxProfile))) {
      *errorMsg = "Error: invalid freqPWM";
      return false;
    }
    car.freqPWM = (uint16_t)v;
  }
  if (jsonHasKey(json, "altBrake") || jsonHasKey(json, "brakeButton")) {
    if (!parseJsonIntAlias(json, "altBrake", "brakeButton", v) || !inRange(v, 0, 100)) {
      *errorMsg = "Error: invalid altBrake"; return false;
    }
    car.brakeButtonReduction = (uint16_t)v;
  }
  if (jsonHasKey(json, "releaseMode") || jsonHasKey(json, "quickBrakeEnabled")) {
    uint16_t releaseMode = car.quickBrakeEnabled;
    if (!parseJsonReleaseModeAlias(json, "releaseMode", "quickBrakeEnabled", &releaseMode)) {
      *errorMsg = "Error: invalid releaseMode"; return false;
    }
    car.quickBrakeEnabled = releaseMode;
  }
  if (jsonHasKey(json, "releaseZone") || jsonHasKey(json, "quickBrakeThreshold")) {
    if (!parseJsonIntAlias(json, "releaseZone", "quickBrakeThreshold", v) || !inRange(v, 0, QUICK_BRAKE_THRESHOLD_MAX)) {
      *errorMsg = "Error: invalid releaseZone"; return false;
    }
    car.quickBrakeThreshold = (uint16_t)v;
  }
  if (jsonHasKey(json, "releaseLevel") || jsonHasKey(json, "quickBrakeStrength")) {
    if (!parseJsonIntAlias(json, "releaseLevel", "quickBrakeStrength", v) || !inRange(v, 0, QUICK_BRAKE_STRENGTH_MAX)) {
      *errorMsg = "Error: invalid releaseLevel"; return false;
    }
    car.quickBrakeStrength = (uint16_t)v;
  }

  char tempName[CAR_NAME_MAX_SIZE];
  if (parseJsonStr(json, "carName", tempName, CAR_NAME_MAX_SIZE)) {
    memset(car.carName, 0, CAR_NAME_MAX_SIZE);
    strncpy(car.carName, tempName, CAR_NAME_MAX_SIZE - 1);
    car.carName[CAR_NAME_MAX_SIZE - 1] = '\0';
  }

  updated.carParam[carIndex] = car;
  clampStoredVarCarPwmFreqsToProfile(&updated, pwmFreqMaxProfile);
  g_storedVar = updated;
  g_antiSpinDisplayMode = antiSpinDisplayMode;
  g_antiSpinStepMs = antiSpinStepMs;
  g_antiSpinStepPct = antiSpinStepPct;
  g_brakeStep = brakeStepRaw;
  g_sensiStep = sensiStepRaw;
  if ((g_advancedMenuEnabled ? 1U : 0U) != (advancedMenuEnabled ? 1U : 0U)) {
    applyAdvancedMenuSetting(advancedMenuEnabled);
  } else {
    g_advancedMenuEnabled = advancedMenuEnabled ? 1U : 0U;
  }
  applyEncoderInvertSetting(encoderInvertEnabled);
  applyConfiguredPwmFreqMaxProfile(pwmFreqMaxProfile);
  applyAdcVoltageRangeMilliVolts(adcVoltageRangeMv);
  g_wifiConfiguredMode = wifiConfiguredMode;
  copyBoundedString(g_wifiClientSsid, sizeof(g_wifiClientSsid), wifiClientSsid);
  copyBoundedString(g_wifiClientPassword, sizeof(g_wifiClientPassword), wifiClientPassword);
  setCurrentUiAuthCredentials(uiAuthUsername, uiAuthPassword);
  initMenuItems();

  if (appliedCarIndex != nullptr) *appliedCarIndex = carIndex;
  return true;
}
