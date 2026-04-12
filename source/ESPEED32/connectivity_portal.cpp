#include "connectivity_portal.h"
#include "slot_ESC.h"
#include "HAL.h"
#include "telemetry_logging.h"
#include <FS.h>
#include <SPIFFS.h>
#include <Update.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>
#include <ctype.h>

/* External references from main .ino */
extern StoredVar_type g_storedVar;
extern ESC_type g_escVar;
extern uint16_t g_antiSpinStepMs;
extern uint16_t g_antiSpinStepPct;
extern uint16_t g_antiSpinDisplayMode;
extern uint16_t g_brakeStep;
extern uint16_t g_sensiStep;
extern uint16_t g_advancedMenuEnabled;
extern uint16_t g_encoderInvertEnabled;
extern uint16_t g_carSel;
extern OBDISP g_obd;
extern AiEsp32RotaryEncoder g_rotaryEncoder;
extern void saveEEPROM(StoredVar_type toSave);
extern void applyAdvancedMenuSetting(uint16_t enabled);
extern void initMenuItems();

/* Web server instance (heap-allocated only when active) */
static WebServer* g_wifiServer = nullptr;
static String g_uploadBuffer;
static char g_wifiSuffix[5] = "";  /* MAC-based suffix, e.g. "A3B4" */
static char g_wifiHostName[18] = "";
static bool g_spiffsMounted = false;
static String g_serialCmdLine;
static bool g_mdnsActive = false;
static bool g_wifiConfigLoaded = false;
static bool g_backupSecretLoaded = false;
static bool g_backupSecretAvailable = false;
static bool g_wifiRestartPending = false;
static uint32_t g_wifiRestartAtMs = 0;
static uint16_t g_wifiConfiguredMode = WIFI_CONFIG_AP;
static uint8_t g_wifiActiveMode = WIFI_PORTAL_OFF;
static bool g_wifiApFallbackActive = false;
static wl_status_t g_wifiLastConnectStatus = WL_IDLE_STATUS;
static char g_wifiClientSsid[WIFI_STA_SSID_MAX_LEN + 1] = "";
static char g_wifiClientPassword[WIFI_STA_PASS_MAX_LEN + 1] = "";
static const size_t UI_AUTH_USER_MAX_LEN = 31;
static const size_t UI_AUTH_PASS_MAX_LEN = 63;
static char g_uiAuthUsername[UI_AUTH_USER_MAX_LEN + 1] = "espeed32";
static char g_uiAuthPassword[UI_AUTH_PASS_MAX_LEN + 1] = "";
static char g_wifiConnectedSsid[WIFI_STA_SSID_MAX_LEN + 1] = "";
static uint8_t g_backupSecret[32] = {0};

static const char* PREF_KEY_WIFI_MODE = "wifi_mode_v1";
static const char* PREF_KEY_WIFI_STA_SSID = "wifi_sta_ssid";
static const char* PREF_KEY_WIFI_STA_PASS = "wifi_sta_pass";
static const char* PREF_KEY_UI_AUTH_USER = "ui_auth_user";
static const char* PREF_KEY_UI_AUTH_PASS = "ui_auth_pass";
static const char* PREF_KEY_WIFI_BACKUP_SECRET = "wifi_bkp_sec";
static const char* PREF_KEY_OTA_WIFI_BOOT = "ota_wifi_boot";
static const uint32_t WIFI_STA_CONNECT_TIMEOUT_MS = 8000UL;
static const size_t WIFI_BACKUP_SECRET_LEN = 32;
static const size_t WIFI_BACKUP_SECRET_HEX_LEN = WIFI_BACKUP_SECRET_LEN * 2;
static const size_t WIFI_BACKUP_NONCE_LEN = 12;
static const size_t WIFI_BACKUP_TAG_LEN = 16;
static const size_t WIFI_BACKUP_NONCE_B64_LEN = 16;
static const size_t WIFI_BACKUP_TAG_B64_LEN = 24;
static const size_t WIFI_BACKUP_PASS_B64_MAX_LEN = (((WIFI_STA_PASS_MAX_LEN + 2) / 3) * 4);

static bool readMacAddress(esp_mac_type_t type, uint8_t out[6]) {
  if (out == nullptr) return false;
  return esp_read_mac(out, type) == ESP_OK;
}

static void serviceUsbSerialCommands();
static void appendJsonEscaped(String& out, const char* in);
static String buildTelemetryStatusPayload(const char* message);
static String buildTelemetryLivePayload(uint32_t afterSeq, size_t limit);
static String buildTelemetryConfigSnapshotJson();
static String buildInfoJson();
static void sendSerialLengthPrefixedPayload(const String& payload);
static const char* getBackupReleaseModeName(uint16_t mode);
static bool parseJsonStr(const String& json, const char* key, char* outStr, int maxLen);
static void copyBoundedString(char* out, size_t outLen, const char* in);
static bool otaRequestWantsRestart();
static void clearOtaDeferredRestartState();
static bool isOtaDeferredRestartActive();
static bool readSpiffsRelease(char* out, size_t outLen);

static void scheduleWiFiPortalRestartIfNeeded(uint16_t previousConfiguredMode,
                                              const char* previousWiFiSsid,
                                              const char* previousWiFiPassword) {
  if (!isWiFiPortalActive()) {
    return;
  }
  bool modeChanged = previousConfiguredMode != g_wifiConfiguredMode;
  bool clientCredentialsChanged =
    g_wifiConfiguredMode == WIFI_CONFIG_HOME &&
    ((previousWiFiSsid != nullptr && strcmp(previousWiFiSsid, g_wifiClientSsid) != 0) ||
     (previousWiFiPassword != nullptr && strcmp(previousWiFiPassword, g_wifiClientPassword) != 0));
  if (!modeChanged && !clientCredentialsChanged) {
    return;
  }
  g_wifiRestartPending = true;
  g_wifiRestartAtMs = millis() + 250UL;
}

static uint16_t normalizeStatusSlotForUi(uint16_t slotValue) {
  return normalizeStatusSlotValue(slotValue);
}

static void buildWifiSuffixIfNeeded() {
  if (g_wifiSuffix[0] != '\0') {
    return;
  }
  uint8_t wifiMac[6] = {0};
  if (readMacAddress(ESP_MAC_WIFI_STA, wifiMac)) {
    sprintf(g_wifiSuffix, "%02X%02X", wifiMac[4], wifiMac[5]);
  } else {
    uint64_t mac = ESP.getEfuseMac();
    sprintf(g_wifiSuffix, "%02X%02X", (uint8_t)(mac >> 8), (uint8_t)(mac));
  }
}

static void buildWifiHostNameIfNeeded() {
  if (g_wifiHostName[0] != '\0') {
    return;
  }
  buildWifiSuffixIfNeeded();
  char suffixLower[5];
  for (uint8_t i = 0; i < 4; i++) {
    suffixLower[i] = (char)tolower((unsigned char)g_wifiSuffix[i]);
  }
  suffixLower[4] = '\0';
  snprintf(g_wifiHostName, sizeof(g_wifiHostName), "espeed32-%s", suffixLower);
}

static bool readSpiffsRelease(char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return false;
  }
  out[0] = '\0';

  bool mountedHere = false;
  if (!g_spiffsMounted) {
    if (!SPIFFS.begin(false)) {
      return false;
    }
    mountedHere = true;
  }

  File manifest = SPIFFS.open("/ui/release.json", "r");
  if (!manifest) {
    if (mountedHere) {
      SPIFFS.end();
    }
    return false;
  }

  String json = manifest.readString();
  manifest.close();

  if (mountedHere) {
    SPIFFS.end();
  }

  char release[16];
  if (parseJsonStr(json, "spiffsRelease", release, sizeof(release)) ||
      parseJsonStr(json, "release", release, sizeof(release))) {
    copyBoundedString(out, outLen, release);
    return release[0] != '\0';
  }

  return false;
}

bool getSpiffsReleaseString(char* out, size_t outLen) {
  return readSpiffsRelease(out, outLen);
}

static void getBackupControllerId(char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  uint64_t mac = ESP.getEfuseMac();
  snprintf(out, outLen, "%012llX", (unsigned long long)(mac & 0xFFFFFFFFFFFFULL));
}

static void getDefaultUiAuthUsername(char* out, size_t outLen) {
  copyBoundedString(out, outLen, "espeed32");
}

static void getDefaultUiAuthPassword(char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  copyBoundedString(out, outLen, WIFI_PASS);
}

static void getLegacyUiAuthPassword4(char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  uint8_t wifiMac[6] = {0};
  if (readMacAddress(ESP_MAC_WIFI_STA, wifiMac)) {
    snprintf(out, outLen, "%02x%02x", wifiMac[4], wifiMac[5]);
  } else {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(out, outLen, "%04llx", (unsigned long long)(mac & 0xFFFFULL));
  }
}

static void getLegacyUiAuthPassword8(char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  uint8_t wifiMac[6] = {0};
  if (readMacAddress(ESP_MAC_WIFI_STA, wifiMac)) {
    snprintf(out, outLen, "%02x%02x%02x%02x", wifiMac[2], wifiMac[3], wifiMac[4], wifiMac[5]);
  } else {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(out, outLen, "%08llx", (unsigned long long)(mac & 0xFFFFFFFFULL));
  }
}

static void applyDefaultUiAuthCredentials() {
  getDefaultUiAuthUsername(g_uiAuthUsername, sizeof(g_uiAuthUsername));
  getDefaultUiAuthPassword(g_uiAuthPassword, sizeof(g_uiAuthPassword));
}

static void loadWiFiNetworkSettingsIfNeeded() {
  if (g_wifiConfigLoaded) {
    return;
  }

  Preferences pref;
  if (pref.begin("stored_var", true)) {
    char legacyDefault4[UI_AUTH_PASS_MAX_LEN + 1];
    char legacyDefault8[UI_AUTH_PASS_MAX_LEN + 1];
    uint32_t mode = pref.getUChar(PREF_KEY_WIFI_MODE, WIFI_CONFIG_AP);
    g_wifiConfiguredMode = (mode == WIFI_CONFIG_HOME) ? WIFI_CONFIG_HOME : WIFI_CONFIG_AP;

    String ssid = pref.getString(PREF_KEY_WIFI_STA_SSID, "");
    String pass = pref.getString(PREF_KEY_WIFI_STA_PASS, "");
    String authPass = pref.getString(PREF_KEY_UI_AUTH_PASS, "");
    ssid.toCharArray(g_wifiClientSsid, sizeof(g_wifiClientSsid));
    pass.toCharArray(g_wifiClientPassword, sizeof(g_wifiClientPassword));
    getDefaultUiAuthUsername(g_uiAuthUsername, sizeof(g_uiAuthUsername));
    authPass.toCharArray(g_uiAuthPassword, sizeof(g_uiAuthPassword));
    getLegacyUiAuthPassword4(legacyDefault4, sizeof(legacyDefault4));
    getLegacyUiAuthPassword8(legacyDefault8, sizeof(legacyDefault8));
    if (g_uiAuthPassword[0] == '\0' ||
        strcmp(g_uiAuthPassword, legacyDefault4) == 0 ||
        strcmp(g_uiAuthPassword, legacyDefault8) == 0) {
      getDefaultUiAuthPassword(g_uiAuthPassword, sizeof(g_uiAuthPassword));
    }
    pref.end();
  } else {
    g_wifiConfiguredMode = WIFI_CONFIG_AP;
    g_wifiClientSsid[0] = '\0';
    g_wifiClientPassword[0] = '\0';
    applyDefaultUiAuthCredentials();
  }

  g_wifiConfigLoaded = true;
}

static void writeWiFiNetworkSettingsToPrefs() {
  loadWiFiNetworkSettingsIfNeeded();

  Preferences pref;
  if (!pref.begin("stored_var", false)) {
    return;
  }
  pref.putUChar(PREF_KEY_WIFI_MODE, (uint8_t)((g_wifiConfiguredMode == WIFI_CONFIG_HOME) ? WIFI_CONFIG_HOME : WIFI_CONFIG_AP));
  pref.putString(PREF_KEY_WIFI_STA_SSID, g_wifiClientSsid);
  pref.putString(PREF_KEY_WIFI_STA_PASS, g_wifiClientPassword);
  pref.remove(PREF_KEY_UI_AUTH_USER);
  pref.putString(PREF_KEY_UI_AUTH_PASS, g_uiAuthPassword);
  pref.end();
}

void requestWiFiAutoStartOnNextBoot() {
  Preferences pref;
  if (!pref.begin("stored_var", false)) {
    return;
  }
  pref.putBool(PREF_KEY_OTA_WIFI_BOOT, true);
  pref.end();
}

bool consumeWiFiAutoStartOnNextBootRequest() {
  Preferences pref;
  if (!pref.begin("stored_var", false)) {
    return false;
  }
  bool enabled = pref.getBool(PREF_KEY_OTA_WIFI_BOOT, false);
  if (enabled) {
    pref.putBool(PREF_KEY_OTA_WIFI_BOOT, false);
  }
  pref.end();
  return enabled;
}

static void copyBoundedString(char* out, size_t outLen, const char* in) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  if (in == nullptr) {
    out[0] = '\0';
    return;
  }
  strncpy(out, in, outLen - 1);
  out[outLen - 1] = '\0';
}

static void getCurrentUiAuthUsername(char* out, size_t outLen) {
  getDefaultUiAuthUsername(out, outLen);
}

static void getCurrentUiAuthPassword(char* out, size_t outLen) {
  loadWiFiNetworkSettingsIfNeeded();
  copyBoundedString(out, outLen, g_uiAuthPassword);
}

static void setCurrentUiAuthCredentials(const char* username, const char* password) {
  (void)username;
  loadWiFiNetworkSettingsIfNeeded();
  getDefaultUiAuthUsername(g_uiAuthUsername, sizeof(g_uiAuthUsername));
  if (password != nullptr && password[0] != '\0') {
    copyBoundedString(g_uiAuthPassword, sizeof(g_uiAuthPassword), password);
  } else {
    getDefaultUiAuthPassword(g_uiAuthPassword, sizeof(g_uiAuthPassword));
  }
}

static bool stringsEqualIgnoreCase(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) {
    return a == b;
  }
  while (*a != '\0' && *b != '\0') {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
      return false;
    }
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

static bool areCurrentUiAuthCredentialsDefault() {
  loadWiFiNetworkSettingsIfNeeded();
  char defaultUser[UI_AUTH_USER_MAX_LEN + 1];
  char defaultPass[UI_AUTH_PASS_MAX_LEN + 1];
  getDefaultUiAuthUsername(defaultUser, sizeof(defaultUser));
  getDefaultUiAuthPassword(defaultPass, sizeof(defaultPass));
  return stringsEqualIgnoreCase(g_uiAuthUsername, defaultUser) &&
         strcmp(g_uiAuthPassword, defaultPass) == 0;
}

static bool hexCharToNibble(char c, uint8_t* out) {
  if (out == nullptr) return false;
  if (c >= '0' && c <= '9') {
    *out = (uint8_t)(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    *out = (uint8_t)(10 + (c - 'a'));
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    *out = (uint8_t)(10 + (c - 'A'));
    return true;
  }
  return false;
}

static void bytesToHex(const uint8_t* in, size_t inLen, char* out, size_t outLen) {
  if (in == nullptr || out == nullptr || outLen < (inLen * 2U + 1U)) {
    return;
  }
  static const char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < inLen; i++) {
    out[i * 2U] = kHex[(in[i] >> 4) & 0x0F];
    out[i * 2U + 1U] = kHex[in[i] & 0x0F];
  }
  out[inLen * 2U] = '\0';
}

static bool hexToBytes(const char* in, uint8_t* out, size_t outLen) {
  if (in == nullptr || out == nullptr) return false;
  size_t hexLen = strlen(in);
  if (hexLen != outLen * 2U) return false;
  for (size_t i = 0; i < outLen; i++) {
    uint8_t hi = 0;
    uint8_t lo = 0;
    if (!hexCharToNibble(in[i * 2U], &hi) || !hexCharToNibble(in[i * 2U + 1U], &lo)) {
      return false;
    }
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

static bool encodeBase64(const uint8_t* in, size_t inLen, char* out, size_t outLen) {
  if (in == nullptr || out == nullptr || outLen == 0) return false;
  size_t actualLen = 0;
  int rc = mbedtls_base64_encode((unsigned char*)out, outLen - 1U, &actualLen, in, inLen);
  if (rc != 0 || actualLen >= outLen) {
    out[0] = '\0';
    return false;
  }
  out[actualLen] = '\0';
  return true;
}

static bool decodeBase64(const char* in, uint8_t* out, size_t outLen, size_t* actualLen) {
  if (in == nullptr || out == nullptr) return false;
  size_t decodedLen = 0;
  int rc = mbedtls_base64_decode(out, outLen, &decodedLen, (const unsigned char*)in, strlen(in));
  if (rc != 0) return false;
  if (actualLen != nullptr) {
    *actualLen = decodedLen;
  }
  return true;
}

static bool ensureBackupSecretAvailable(bool createIfMissing) {
  if (g_backupSecretLoaded) {
    if (g_backupSecretAvailable || !createIfMissing) {
      return g_backupSecretAvailable;
    }
  }

  Preferences pref;
  if (!pref.begin("stored_var", createIfMissing ? false : true)) {
    g_backupSecretLoaded = true;
    g_backupSecretAvailable = false;
    return false;
  }

  String secretHex = pref.getString(PREF_KEY_WIFI_BACKUP_SECRET, "");
  bool ok = secretHex.length() == WIFI_BACKUP_SECRET_HEX_LEN &&
            hexToBytes(secretHex.c_str(), g_backupSecret, sizeof(g_backupSecret));

  if (!ok && createIfMissing) {
    esp_fill_random(g_backupSecret, sizeof(g_backupSecret));
    char secretHexBuf[WIFI_BACKUP_SECRET_HEX_LEN + 1];
    bytesToHex(g_backupSecret, sizeof(g_backupSecret), secretHexBuf, sizeof(secretHexBuf));
    ok = pref.putString(PREF_KEY_WIFI_BACKUP_SECRET, secretHexBuf) == WIFI_BACKUP_SECRET_HEX_LEN;
  }

  pref.end();
  g_backupSecretLoaded = true;
  g_backupSecretAvailable = ok;
  if (!ok) {
    memset(g_backupSecret, 0, sizeof(g_backupSecret));
  }
  return ok;
}

static bool encryptBackupWiFiPassword(const char* password, const char* backupControllerId,
                                      char* nonceB64, size_t nonceB64Len,
                                      char* tagB64, size_t tagB64Len,
                                      char* cipherB64, size_t cipherB64Len) {
  if (password == nullptr || backupControllerId == nullptr ||
      nonceB64 == nullptr || tagB64 == nullptr || cipherB64 == nullptr) {
    return false;
  }
  size_t passwordLen = strlen(password);
  if (passwordLen == 0 || passwordLen > WIFI_STA_PASS_MAX_LEN) {
    return false;
  }
  if (!ensureBackupSecretAvailable(true)) {
    return false;
  }

  uint8_t nonce[WIFI_BACKUP_NONCE_LEN];
  uint8_t tag[WIFI_BACKUP_TAG_LEN];
  uint8_t cipher[WIFI_STA_PASS_MAX_LEN];
  esp_fill_random(nonce, sizeof(nonce));

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES,
                              g_backupSecret, (unsigned int)(sizeof(g_backupSecret) * 8U));
  if (rc == 0) {
    rc = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, passwordLen,
                                   nonce, sizeof(nonce),
                                   (const unsigned char*)backupControllerId, strlen(backupControllerId),
                                   (const unsigned char*)password, cipher,
                                   sizeof(tag), tag);
  }
  mbedtls_gcm_free(&ctx);
  if (rc != 0) {
    return false;
  }

  return encodeBase64(nonce, sizeof(nonce), nonceB64, nonceB64Len) &&
         encodeBase64(tag, sizeof(tag), tagB64, tagB64Len) &&
         encodeBase64(cipher, passwordLen, cipherB64, cipherB64Len);
}

static bool decryptBackupWiFiPasswordFromJson(const String& json, char* outPassword,
                                              size_t outPasswordLen, String* errorMsg) {
  if (outPassword == nullptr || outPasswordLen == 0 || errorMsg == nullptr) {
    return false;
  }

  char backupControllerId[13];
  char currentControllerId[13];
  char nonceB64[WIFI_BACKUP_NONCE_B64_LEN + 1];
  char tagB64[WIFI_BACKUP_TAG_B64_LEN + 1];
  char cipherB64[WIFI_BACKUP_PASS_B64_MAX_LEN + 1];
  getBackupControllerId(currentControllerId, sizeof(currentControllerId));

  if (!parseJsonStr(json, "backupControllerId", backupControllerId, sizeof(backupControllerId)) ||
      backupControllerId[0] == '\0') {
    *errorMsg = "Error: encrypted Client WiFi password is missing backupControllerId";
    return false;
  }
  if (strcmp(backupControllerId, currentControllerId) != 0) {
    *errorMsg = "Error: encrypted Client WiFi password belongs to another controller";
    return false;
  }
  if (!parseJsonStr(json, "wifiStaPasswordNonce", nonceB64, sizeof(nonceB64)) ||
      !parseJsonStr(json, "wifiStaPasswordTag", tagB64, sizeof(tagB64)) ||
      !parseJsonStr(json, "wifiStaPasswordEnc", cipherB64, sizeof(cipherB64))) {
    *errorMsg = "Error: encrypted Client WiFi password is missing crypto fields";
    return false;
  }
  if (!ensureBackupSecretAvailable(false)) {
    *errorMsg = "Error: this controller is missing the backup key for encrypted Client WiFi password";
    return false;
  }

  uint8_t nonce[WIFI_BACKUP_NONCE_LEN];
  uint8_t tag[WIFI_BACKUP_TAG_LEN];
  uint8_t cipher[WIFI_STA_PASS_MAX_LEN];
  uint8_t plain[WIFI_STA_PASS_MAX_LEN];
  size_t nonceLen = 0;
  size_t tagLen = 0;
  size_t cipherLen = 0;

  if (!decodeBase64(nonceB64, nonce, sizeof(nonce), &nonceLen) ||
      !decodeBase64(tagB64, tag, sizeof(tag), &tagLen) ||
      !decodeBase64(cipherB64, cipher, sizeof(cipher), &cipherLen)) {
    *errorMsg = "Error: encrypted Client WiFi password has invalid base64 data";
    return false;
  }
  if (nonceLen != sizeof(nonce) || tagLen != sizeof(tag) || cipherLen >= outPasswordLen) {
    *errorMsg = "Error: encrypted Client WiFi password has invalid lengths";
    return false;
  }

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES,
                              g_backupSecret, (unsigned int)(sizeof(g_backupSecret) * 8U));
  if (rc == 0) {
    rc = mbedtls_gcm_auth_decrypt(&ctx, cipherLen,
                                  nonce, sizeof(nonce),
                                  (const unsigned char*)backupControllerId, strlen(backupControllerId),
                                  tag, sizeof(tag),
                                  cipher, plain);
  }
  mbedtls_gcm_free(&ctx);
  if (rc != 0) {
    *errorMsg = "Error: failed to decrypt Client WiFi password on this controller";
    return false;
  }

  memcpy(outPassword, plain, cipherLen);
  outPassword[cipherLen] = '\0';
  return true;
}

static bool decryptBackupUiAuthPasswordFromJson(const String& json, char* outPassword,
                                                size_t outPasswordLen, String* errorMsg) {
  if (outPassword == nullptr || outPasswordLen == 0 || errorMsg == nullptr) {
    return false;
  }

  char backupControllerId[13];
  char currentControllerId[13];
  char nonceB64[WIFI_BACKUP_NONCE_B64_LEN + 1];
  char tagB64[WIFI_BACKUP_TAG_B64_LEN + 1];
  char cipherB64[WIFI_BACKUP_PASS_B64_MAX_LEN + 1];
  getBackupControllerId(currentControllerId, sizeof(currentControllerId));

  if (!parseJsonStr(json, "backupControllerId", backupControllerId, sizeof(backupControllerId)) ||
      backupControllerId[0] == '\0') {
    *errorMsg = "Error: encrypted UI auth password is missing backupControllerId";
    return false;
  }
  if (strcmp(backupControllerId, currentControllerId) != 0) {
    *errorMsg = "Error: encrypted UI auth password belongs to another controller";
    return false;
  }
  if (!parseJsonStr(json, "uiAuthPasswordNonce", nonceB64, sizeof(nonceB64)) ||
      !parseJsonStr(json, "uiAuthPasswordTag", tagB64, sizeof(tagB64)) ||
      !parseJsonStr(json, "uiAuthPasswordEnc", cipherB64, sizeof(cipherB64))) {
    *errorMsg = "Error: encrypted UI auth password is missing crypto fields";
    return false;
  }
  if (!ensureBackupSecretAvailable(false)) {
    *errorMsg = "Error: this controller is missing the backup key for encrypted UI auth password";
    return false;
  }

  uint8_t nonce[WIFI_BACKUP_NONCE_LEN];
  uint8_t tag[WIFI_BACKUP_TAG_LEN];
  uint8_t cipher[UI_AUTH_PASS_MAX_LEN];
  uint8_t plain[UI_AUTH_PASS_MAX_LEN];
  size_t nonceLen = 0;
  size_t tagLen = 0;
  size_t cipherLen = 0;

  if (!decodeBase64(nonceB64, nonce, sizeof(nonce), &nonceLen) ||
      !decodeBase64(tagB64, tag, sizeof(tag), &tagLen) ||
      !decodeBase64(cipherB64, cipher, sizeof(cipher), &cipherLen)) {
    *errorMsg = "Error: encrypted UI auth password has invalid base64 data";
    return false;
  }
  if (nonceLen != sizeof(nonce) || tagLen != sizeof(tag) || cipherLen >= outPasswordLen) {
    *errorMsg = "Error: encrypted UI auth password has invalid lengths";
    return false;
  }

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES,
                              g_backupSecret, (unsigned int)(sizeof(g_backupSecret) * 8U));
  if (rc == 0) {
    rc = mbedtls_gcm_auth_decrypt(&ctx, cipherLen,
                                  nonce, sizeof(nonce),
                                  (const unsigned char*)backupControllerId, strlen(backupControllerId),
                                  tag, sizeof(tag),
                                  cipher, plain);
  }
  mbedtls_gcm_free(&ctx);
  if (rc != 0) {
    *errorMsg = "Error: failed to decrypt UI auth password on this controller";
    return false;
  }

  memcpy(outPassword, plain, cipherLen);
  outPassword[cipherLen] = '\0';
  return true;
}

static bool hasWiFiClientCredentials() {
  loadWiFiNetworkSettingsIfNeeded();
  return g_wifiClientSsid[0] != '\0' && g_wifiClientPassword[0] != '\0';
}

/* Minimal fallback page if /ui/index.html is missing on SPIFFS */
static const char SPIFFS_MISMATCH_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESPEED32 – Update Required</title>
<style>
body{font-family:Arial,sans-serif;max-width:560px;margin:20px auto;padding:0 15px;background:#1a1a2e;color:#eee}
h1{color:#e94560;margin-bottom:8px}
code{background:#111;padding:2px 6px;border-radius:4px}
li{margin:8px 0}
a{color:#7ed6ff}
.ver{margin:10px 0;padding:10px 12px;background:#111;border-radius:6px;font-size:14px}
</style>
</head>
<body>
<h1>Filesystem Update Required</h1>
<p>The controller is running firmware <code>%FW_VER%</code> but the web UI filesystem is <code>%SPIFFS_VER%</code>.
These must match. The controller is working normally — only the web UI is affected.</p>
<div class="ver">Firmware: <code>%FW_VER%</code> &nbsp;|&nbsp; Filesystem/UI: <code>%SPIFFS_VER%</code></div>
<p><b>Fix: upload the SPIFFS image for firmware %FW_VER%</b></p>
<form method="POST" action="/ota-spiffs" enctype="multipart/form-data">
  <input type="file" name="file" accept=".bin" required>
  <button type="submit" style="margin-left:8px">Upload Filesystem (.bin)</button>
</form>
<p>Or use the <a href="https://espeed32.com/ui/index.html?mode=hosted">hosted controller page</a>
to run an automatic paired update.</p>
<p><a href="/backup">Download config backup (.json)</a> before updating if you want to be safe.</p>
</body>
</html>
)rawliteral";

static const char UI_FALLBACK_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESPEED32 Recovery</title>
</head>
<body style="font-family:Arial,sans-serif;max-width:560px;margin:20px auto;padding:0 12px;">
<h1>ESPEED32 Recovery Page</h1>
<p>Missing SPIFFS file: <code>/ui/index.html</code></p>
<p>This page is intentionally minimal.</p>
<p><a href="/backup">Download Config Backup (.json)</a></p>
<form method="POST" action="/ota-fw" enctype="multipart/form-data">
  <p><b>Firmware Recovery (.bin)</b></p>
  <input type="file" name="file" accept=".bin" required>
  <button type="submit">Upload Firmware</button>
</form>
<form method="POST" action="/ota-spiffs" enctype="multipart/form-data">
  <p><b>Filesystem Recovery (SPIFFS .bin)</b></p>
  <input type="file" name="file" accept=".bin" required>
  <button type="submit">Upload Filesystem</button>
</form>
<p><b>Restore full web UI:</b></p>
<ol>
  <li>Ensure <code>source/ESPEED32/data/ui/index.html</code> exists.</li>
  <li>Upload SPIFFS image on this page or from IDE.</li>
  <li>Reload <code>/</code>.</li>
</ol>
</body>
</html>
)rawliteral";

static const char PUBLIC_UI_FALLBACK_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESPEED32</title>
</head>
<body style="font-family:Arial,sans-serif;max-width:560px;margin:20px auto;padding:0 12px;line-height:1.45;">
<h1>ESPEED32</h1>
<p>Open the protected controller panel for tools, or the onboard guide for docs.</p>
<p><a href="/controller">Open Controller Panel</a></p>
<p><a href="/docs">Open User Guide</a></p>
<p>Controller login credentials are shown on the controller WiFi info page.</p>
</body>
</html>
)rawliteral";

/* Fallback page shown if docs are missing from SPIFFS */
static const char DOCS_MISSING_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESPEED32 Docs Not Found</title>
<style>
body{font-family:Arial,sans-serif;max-width:560px;margin:20px auto;padding:0 15px;background:#1a1a2e;color:#eee}
h1{color:#e94560;margin-bottom:8px}
code{background:#111;padding:2px 6px;border-radius:4px}
li{margin:8px 0}
a{color:#7ed6ff}
</style>
</head>
<body>
<h1>Documentation Not Found</h1>
<p>Could not open <code>%PATH%</code> from SPIFFS.</p>
<p>To update docs on the controller:</p>
<ol>
<li>Edit files in <code>source/ESPEED32/data/docs/</code> (Git source of truth).</li>
<li>Upload the filesystem image from Web UI or IDE (SPIFFS upload).</li>
<li>Refresh this page.</li>
</ol>
<form method="POST" action="/ota-spiffs" enctype="multipart/form-data">
  <p><b>Upload Filesystem (SPIFFS .bin)</b></p>
  <input type="file" name="file" accept=".bin" required>
  <button type="submit">Upload SPIFFS (.bin)</button>
</form>
<p><a href="/">Back to backup/restore page</a></p>
</body>
</html>
)rawliteral";


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
  sprintf(buf, "  \"version\": %d,\n", STORED_VAR_VERSION);              json += buf;
  sprintf(buf, "  \"selectedCarNumber\": %u,\n", storedVar.selectedCarNumber); json += buf;
  sprintf(buf, "  \"minTrigger_raw\": %d,\n", storedVar.minTrigger_raw); json += buf;
  sprintf(buf, "  \"maxTrigger_raw\": %d,\n", storedVar.maxTrigger_raw); json += buf;
  sprintf(buf, "  \"viewMode\": %u,\n", storedVar.viewMode);           json += buf;
  sprintf(buf, "  \"screensaverTimeout\": %u,\n", storedVar.screensaverTimeout); json += buf;
  sprintf(buf, "  \"soundBoot\": %u,\n", storedVar.soundBoot);         json += buf;
  sprintf(buf, "  \"soundRace\": %u,\n", storedVar.soundRace);         json += buf;
  sprintf(buf, "  \"antiSpinStep\": %u,\n", antiSpinStepMs);           json += buf; /* legacy alias for ms-step */
  sprintf(buf, "  \"antiSpinStepMs\": %u,\n", antiSpinStepMs);         json += buf;
  sprintf(buf, "  \"antiSpinStepPct\": %u,\n", antiSpinStepPct);       json += buf;
  sprintf(buf, "  \"antiSpinDisplayMode\": %u,\n", antiSpinDisplayMode); json += buf;
  sprintf(buf, "  \"brakeStep\": %u.%u,\n", brakeStepRaw / BRAKE_SCALE, brakeFracDigit(brakeStepRaw)); json += buf;
  sprintf(buf, "  \"sensiStep\": %u.%u,\n", sensiStepRaw / SENSI_SCALE, sensiFracDigit(sensiStepRaw)); json += buf;
  sprintf(buf, "  \"advancedMenu\": %u,\n", advancedMenuEnabled ? 1 : 0); json += buf;
  sprintf(buf, "  \"encoderInvert\": %u,\n", encoderInvertEnabled ? 1 : 0); json += buf;
  sprintf(buf, "  \"pwmFreqMax\": %u,\n", pwmFreqMaxProfileToWholeKHz(pwmFreqMaxProfile)); json += buf;
  sprintf(buf, "  \"adcVoltageRangeMv\": %u,\n", adcVoltageRange_mV);  json += buf;
  sprintf(buf, "  \"gridCarSelectEnabled\": %u,\n", storedVar.gridCarSelectEnabled); json += buf;
  sprintf(buf, "  \"raceViewMode\": %u,\n", storedVar.raceViewMode);   json += buf;
  sprintf(buf, "  \"language\": %u,\n", storedVar.language);           json += buf;
  sprintf(buf, "  \"textCase\": %u,\n", storedVar.textCase);           json += buf;
  sprintf(buf, "  \"listFontSize\": %u,\n", storedVar.listFontSize);   json += buf;
  sprintf(buf, "  \"startupDelay\": %u,\n", storedVar.startupDelay);   json += buf;
  json += "  \"backupControllerId\": \"";
  appendJsonEscaped(json, backupControllerId);
  json += "\",\n";
  json += "  \"screensaverLine1\": \"";
  appendJsonEscaped(json, storedVar.screensaverLine1);
  json += "\",\n";
  json += "  \"screensaverLine2\": \"";
  appendJsonEscaped(json, storedVar.screensaverLine2);
  json += "\",\n";
  sprintf(buf, "  \"wifiMode\": %u,\n", wifiConfiguredMode); json += buf;
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
    sprintf(buf, "      \"minSpeed\": %u.%u,\n", c.minSpeed / SENSI_SCALE, sensiFracDigit(c.minSpeed)); json += buf;
    sprintf(buf, "      \"brake\": %u.%u,\n", c.brake / BRAKE_SCALE, brakeFracDigit(c.brake)); json += buf;
    sprintf(buf, "      \"maxSpeed\": %u,\n", c.maxSpeed);               json += buf;
    sprintf(buf, "      \"curveInput\": %u,\n", c.throttleCurveVertex.inputThrottle); json += buf;
    sprintf(buf, "      \"curveDiff\": %u,\n", c.throttleCurveVertex.curveSpeedDiff); json += buf;
    sprintf(buf, "      \"fade\": %u,\n", c.fade);                       json += buf;
    sprintf(buf, "      \"antiSpin\": %u,\n", c.antiSpin);               json += buf;
    sprintf(buf, "      \"freqPWM\": %u,\n", c.freqPWM);                 json += buf;
    sprintf(buf, "      \"altBrake\": %u,\n", c.brakeButtonReduction);  json += buf;
    json += "      \"releaseMode\": \"";
    json += getBackupReleaseModeName(c.quickBrakeEnabled);
    json += "\",\n";
    sprintf(buf, "      \"releaseZone\": %u,\n", c.quickBrakeThreshold); json += buf;
    sprintf(buf, "      \"releaseLevel\": %u\n", c.quickBrakeStrength);  json += buf;
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
static String buildJsonBackup() {
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
static bool parseJsonStr(const String& json, const char* key, char* outStr, int maxLen) {
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

static const char* getBackupReleaseModeName(uint16_t mode) {
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
    default:
      return false;
  }
}


/**
 * @brief Parse and validate uploaded JSON, populate temporary StoredVar
 * @return true if valid, false with error message
 */
static bool parseAndValidateJson(const String& json, StoredVar_type* sv, uint16_t* antiSpinStepMs,
                                 uint16_t* antiSpinStepPct, uint16_t* antiSpinDisplayMode,
                                 uint16_t* brakeStepRaw, uint16_t* sensiStepRaw,
                                 uint16_t* advancedMenuEnabled, uint16_t* encoderInvertEnabled, uint16_t* pwmFreqMaxProfile,
                                 uint16_t* adcVoltageRangeMv,
                                 uint16_t* wifiConfiguredMode, char* wifiClientSsid,
                                 size_t wifiClientSsidLen, char* wifiClientPassword,
                                 size_t wifiClientPasswordLen, char* uiAuthUsername,
                                 size_t uiAuthUsernameLen, char* uiAuthPassword,
                                 size_t uiAuthPasswordLen, String* errorMsg,
                                 String* warningMsg = nullptr) {
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

    /* Name - use temp buffer to preserve names up to CAR_NAME_MAX_SIZE chars */
    char tempName[16];
    if (!parseJsonStr(carJson, "name", tempName, sizeof(tempName))) {
      *errorMsg = "Error: missing name in car " + String(i); return false;
    }
    memset(c.carName, 0, CAR_NAME_MAX_SIZE);
    strncpy(c.carName, tempName, CAR_NAME_MAX_SIZE);
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

  clampStoredVarCarPwmFreqsToProfile(sv,
                                     (pwmFreqMaxProfile != nullptr)
                                       ? *pwmFreqMaxProfile
                                       : getConfiguredPwmFreqMaxProfile());
  return true;
}

static void appendJsonEscaped(String& out, const char* in) {
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

static String buildSchemaJson() {
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
  appendSchemaEnumField(json, first, "pwmFreqMax", "PWM Max",
                        "[{\"value\":5,\"label\":\"5 kHz\"},{\"value\":10,\"label\":\"10 kHz\"},{\"value\":20,\"label\":\"20 kHz\"}]");
  appendSchemaIntField(json, first, "adcVoltageRangeMv", "VIN CAL ADC", ADC_VOLTAGE_RANGE_MIN_MVOLTS, ADC_VOLTAGE_RANGE_MAX_MVOLTS, 1, "mV");
  appendSchemaEnumField(json, first, "gridCarSelectEnabled", "Grid Car Select",
                        "[{\"value\":0,\"label\":\"OFF\"},{\"value\":1,\"label\":\"ON\"}]");
  appendSchemaEnumField(json, first, "raceViewMode", "Race Mode",
                        "[{\"value\":0,\"label\":\"OFF\"},{\"value\":1,\"label\":\"FULL\"},{\"value\":2,\"label\":\"SIMPLE\"}]");
  appendSchemaEnumField(json, first, "language", "Language",
                        "[{\"value\":0,\"label\":\"NOR\"},{\"value\":1,\"label\":\"ENG\"},{\"value\":2,\"label\":\"CS\"},{\"value\":3,\"label\":\"ACD\"},{\"value\":4,\"label\":\"ESP\"},{\"value\":5,\"label\":\"DEU\"},{\"value\":6,\"label\":\"ITA\"},{\"value\":7,\"label\":\"NLD\"},{\"value\":8,\"label\":\"POR\"}]");
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
                        "[{\"value\":0,\"label\":\"BLANK\"},{\"value\":1,\"label\":\"OUTPUT\"},{\"value\":2,\"label\":\"THROTTLE\"},{\"value\":3,\"label\":\"CAR\"},{\"value\":4,\"label\":\"CURR\"},{\"value\":5,\"label\":\"VOLTAGE\"},{\"value\":8,\"label\":\"BATTERY\"},{\"value\":7,\"label\":\"ACTIVE BRAKE\"}]");
  appendSchemaEnumField(json, first, "statusSlot1", "Status Slot 2",
                        "[{\"value\":0,\"label\":\"BLANK\"},{\"value\":1,\"label\":\"OUTPUT\"},{\"value\":2,\"label\":\"THROTTLE\"},{\"value\":3,\"label\":\"CAR\"},{\"value\":4,\"label\":\"CURR\"},{\"value\":5,\"label\":\"VOLTAGE\"},{\"value\":8,\"label\":\"BATTERY\"},{\"value\":7,\"label\":\"ACTIVE BRAKE\"}]");
  appendSchemaEnumField(json, first, "statusSlot2", "Status Slot 3",
                        "[{\"value\":0,\"label\":\"BLANK\"},{\"value\":1,\"label\":\"OUTPUT\"},{\"value\":2,\"label\":\"THROTTLE\"},{\"value\":3,\"label\":\"CAR\"},{\"value\":4,\"label\":\"CURR\"},{\"value\":5,\"label\":\"VOLTAGE\"},{\"value\":8,\"label\":\"BATTERY\"},{\"value\":7,\"label\":\"ACTIVE BRAKE\"}]");
  appendSchemaEnumField(json, first, "statusSlot3", "Status Slot 4",
                        "[{\"value\":0,\"label\":\"BLANK\"},{\"value\":1,\"label\":\"OUTPUT\"},{\"value\":2,\"label\":\"THROTTLE\"},{\"value\":3,\"label\":\"CAR\"},{\"value\":4,\"label\":\"CURR\"},{\"value\":5,\"label\":\"VOLTAGE\"},{\"value\":8,\"label\":\"BATTERY\"},{\"value\":7,\"label\":\"ACTIVE BRAKE\"}]");
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

static uint8_t getRequestedCarIndex() {
  if (g_wifiServer != nullptr && g_wifiServer->hasArg("car")) {
    int32_t v = g_wifiServer->arg("car").toInt();
    if (v >= 0 && v < CAR_MAX_COUNT) return (uint8_t)v;
  }
  return (uint8_t)g_storedVar.selectedCarNumber;
}

static String buildStateJson(uint8_t carIndex) {
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

static bool parseAndApplyWebPatch(const String& json, String* errorMsg, uint8_t* appliedCarIndex) {
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
    if (!inRange(v, STATUS_BLANK, STATUS_BATTERY)) { *errorMsg = "Error: invalid statusSlot0"; return false; }
    updated.statusSlot[0] = (uint16_t)v;
  }
  if (parseJsonInt(json, "statusSlot1", v)) {
    if (v == STATUS_CURRENT_MA) v = STATUS_CURRENT;
    if (!inRange(v, STATUS_BLANK, STATUS_BATTERY)) { *errorMsg = "Error: invalid statusSlot1"; return false; }
    updated.statusSlot[1] = (uint16_t)v;
  }
  if (parseJsonInt(json, "statusSlot2", v)) {
    if (v == STATUS_CURRENT_MA) v = STATUS_CURRENT;
    if (!inRange(v, STATUS_BLANK, STATUS_BATTERY)) { *errorMsg = "Error: invalid statusSlot2"; return false; }
    updated.statusSlot[2] = (uint16_t)v;
  }
  if (parseJsonInt(json, "statusSlot3", v)) {
    if (v == STATUS_CURRENT_MA) v = STATUS_CURRENT;
    if (!inRange(v, STATUS_BLANK, STATUS_BATTERY)) { *errorMsg = "Error: invalid statusSlot3"; return false; }
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


/* OTA progress tracking */
static size_t g_otaTotal = 0;
static size_t g_otaWritten = 0;
static bool g_otaInProgress = false;
static bool g_otaTargetSpiffs = false;
static bool g_otaSessionOk = false;
static bool g_otaRestartRequested = true;
static String g_otaActiveToken = "";
static String g_otaRejectedToken = "";
static int8_t g_otaPrevTxPower = 0;
static bool g_otaRestartDeferredPending = false;
static uint32_t g_otaRestartDeferredUntilMs = 0;
static uint32_t g_otaLastUiUpdateMs = 0;
static size_t g_otaLastUiShownKb = 0;
static const uint32_t OTA_DEFERRED_RESTART_GRACE_MS = 180000UL;

static void clearOtaDeferredRestartState() {
  g_otaRestartDeferredPending = false;
  g_otaRestartDeferredUntilMs = 0;
}

static bool isOtaDeferredRestartActive() {
  if (!g_otaRestartDeferredPending) {
    return false;
  }
  if ((int32_t)(millis() - g_otaRestartDeferredUntilMs) >= 0) {
    clearOtaDeferredRestartState();
    return false;
  }
  return true;
}

/* Documentation helpers (served from SPIFFS) */
static const char* getPublicUiPath() {
  return "/ui/public.html";
}

static const char* getUiPath() {
  return "/ui/index.html";
}

static const char* getDocsPathForLanguage(uint16_t lang) {
  switch (lang) {
    case LANG_NOR: return "/docs/no/index.html";
    case LANG_ESP: return "/docs/es/index.html";
    case LANG_DEU: return "/docs/de/index.html";
    case LANG_ITA: return "/docs/it/index.html";
    case LANG_NLD: return "/docs/nl/index.html";
    case LANG_POR: return "/docs/pt/index.html";
    default:       return "/docs/en/index.html";
  }
}

static const char* getDefaultDocsPath() {
  return getDocsPathForLanguage(g_storedVar.language);
}

static bool spiffsMatchesFirmware(char* spiffsVerOut, size_t outLen) {
  char fwVer[16];
  snprintf(fwVer, sizeof(fwVer), "%d.%d", SW_MAJOR_VERSION, SW_MINOR_VERSION);

  char release[16];
  if (!readSpiffsRelease(release, sizeof(release))) {
    copyBoundedString(spiffsVerOut, outLen, "unknown");
    return false;
  }
  copyBoundedString(spiffsVerOut, outLen, release);

  const char* r = (release[0] == 'v' || release[0] == 'V') ? release + 1 : release;
  return strcmp(r, fwVer) == 0;
}

static void sendSpiffsMismatch(const char* spiffsVer) {
  char fwVer[16];
  snprintf(fwVer, sizeof(fwVer), "%d.%d", SW_MAJOR_VERSION, SW_MINOR_VERSION);

  String page = FPSTR(SPIFFS_MISMATCH_HTML);
  page.replace("%FW_VER%", fwVer);
  page.replace("%SPIFFS_VER%", spiffsVer);
  g_wifiServer->send(200, "text/html; charset=utf-8", page);
}

static bool streamFileFromSpiffs(const char* path, const char* contentType) {
  if (!g_spiffsMounted) {
    return false;
  }

  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    String gzPath = String(path) + ".gz";
    file = SPIFFS.open(gzPath, FILE_READ);
    if (!file) {
      return false;
    }
    g_wifiServer->sendHeader("Content-Encoding", "gzip");
  }

  g_wifiServer->streamFile(file, contentType);
  file.close();
  return true;
}

static bool streamHtmlFromSpiffs(const char* path) {
  /* Prevent browsers from caching HTML pages so stale UI is never served
   * after a SPIFFS update. Static assets (images, fonts) keep default caching. */
  g_wifiServer->sendHeader("Cache-Control", "no-store");
  return streamFileFromSpiffs(path, "text/html; charset=utf-8");
}

static void sendPublicUiFallback() {
  g_wifiServer->send(200, "text/html; charset=utf-8", FPSTR(PUBLIC_UI_FALLBACK_HTML));
}

static bool requireControllerAuth() {
  loadWiFiNetworkSettingsIfNeeded();
  if (g_wifiServer == nullptr) {
    return false;
  }
  if (g_wifiServer->authenticate([](HTTPAuthMethod mode, String enteredUsernameOrReq, String params[]) -> String* {
        (void)params;
        if (mode != BASIC_AUTH) {
          return nullptr;
        }
        if (!stringsEqualIgnoreCase(enteredUsernameOrReq.c_str(), g_uiAuthUsername)) {
          return nullptr;
        }
        return new String(g_uiAuthPassword);
      })) {
    return true;
  }
  g_wifiServer->requestAuthentication(BASIC_AUTH, "ESPEED32 Controller");
  return false;
}

int centerX8x8(const char* text) {
  int widthPx = strlen(text) * WIDTH8x8;
  int x = (OLED_WIDTH - widthPx) / 2;
  return (x < 0) ? 0 : x;
}

static void sendUiFallback() {
  g_wifiServer->send(200, "text/html; charset=utf-8", FPSTR(UI_FALLBACK_HTML));
}

static void sendDocsMissing(const char* requestedPath) {
  String page = FPSTR(DOCS_MISSING_HTML);
  page.replace("%PATH%", requestedPath);
  g_wifiServer->send(404, "text/html; charset=utf-8", page);
}

static void handleSerialCommand(const String& cmd);

/**
 * @brief Non-blocking USB serial command pump.
 * @details Reads one newline-terminated command and dispatches it. Called frequently
 *          from normal UI loops so WebSerial works outside the USB info screen too.
 */
static void serviceUsbSerialCommands() {
  while (Serial.available() > 0) {
    char ch = (char)Serial.read();

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      if (g_serialCmdLine.length() > 0) {
        String cmd = g_serialCmdLine;
        g_serialCmdLine = "";
        cmd.trim();
        if (cmd.length() > 0) {
          handleSerialCommand(cmd);
          return;  /* handle one command per call */
        }
      }
      continue;
    }

    if (g_serialCmdLine.length() < 64) {
      g_serialCmdLine += ch;
    } else {
      /* Corrupted/too-long command line: drop until next newline. */
      g_serialCmdLine = "";
    }
  }
}

/**
 * @brief Handle a single USB serial backup/restore command.
 * Protocol:
 *   "VERSION"        → "<id>,v<major>.<minor>\n"  e.g. "F0A4,v4.4"
 *   "INFO"           → "<bytecount>\n<json>"
 *   "BACKUP"         → "<bytecount>\n<json>"
 *   "RESTORE\n<len>" → read len bytes, parse, save; reply "OK..." or "ERR:..."
 *   "SCHEMA"         → "<bytecount>\n<json>"
 *   "STATE [car]"    → "<bytecount>\n<json>"
 *   "APPLY\n<len>"   → read len bytes patch JSON; reply "OK\n<bytecount>\n<stateJson>" or "ERR:..."
 *   "SAVE"           → "OK - Saved to flash"
 *   "TSTATUS"        → "<bytecount>\n<json>"
 *   "TLIVE a l"      → "<bytecount>\n<json>" (afterSeq=a, limit=l)
 *   "TSTART"         → "<bytecount>\n<json>"
 *   "TSTOP"          → "<bytecount>\n<json>"
 *   "TCLEAR"         → "<bytecount>\n<json>"
 *   "TCONFIG"        → "<bytecount>\n<json>"
 * Shared by showUSBPortalScreen(); called when Serial.available() triggers.
 */
static void handleSerialCommand(const String& cmd) {
  if (cmd == "VERSION") {
    uint8_t idHi = 0;
    uint8_t idLo = 0;
    uint8_t wifiMac[6] = {0};
    if (readMacAddress(ESP_MAC_WIFI_STA, wifiMac)) {
      idHi = wifiMac[4];
      idLo = wifiMac[5];
    } else {
      uint64_t mac = ESP.getEfuseMac();
      idHi = (uint8_t)(mac >> 8);
      idLo = (uint8_t)(mac);
    }
    char resp[16];
    sprintf(resp, "%02X%02X,v%d.%d", idHi, idLo,
            SW_MAJOR_VERSION, SW_MINOR_VERSION);
    Serial.println(resp);
    Serial.flush();

  } else if (cmd == "INFO") {
    sendSerialLengthPrefixedPayload(buildInfoJson());

  } else if (cmd == "BACKUP") {
    String json = buildJsonBackup();
    Serial.print(json.length());
    Serial.print('\n');
    Serial.print(json);
    Serial.flush();

  } else if (cmd == "SCHEMA") {
    String json = buildSchemaJson();
    sendSerialLengthPrefixedPayload(json);

  } else if (cmd.startsWith("STATE")) {
    uint8_t carIndex = (uint8_t)g_storedVar.selectedCarNumber;
    int32_t sp = cmd.indexOf(' ');
    if (sp > 0 && sp < (int32_t)cmd.length() - 1) {
      int32_t v = cmd.substring(sp + 1).toInt();
      if (v < 0 || v >= CAR_MAX_COUNT) {
        Serial.println("ERR:invalid carIndex");
        return;
      }
      carIndex = (uint8_t)v;
    }
    String json = buildStateJson(carIndex);
    sendSerialLengthPrefixedPayload(json);

  } else if (cmd == "TSTATUS") {
    sendSerialLengthPrefixedPayload(buildTelemetryStatusPayload(""));

  } else if (cmd.startsWith("TLIVE")) {
    uint32_t afterSeq = 0U;
    size_t limit = 256U;
    int32_t firstSpace = cmd.indexOf(' ');
    if (firstSpace > 0 && firstSpace < (int32_t)cmd.length() - 1) {
      int32_t secondSpace = cmd.indexOf(' ', firstSpace + 1);
      String afterStr;
      String limitStr;
      if (secondSpace > firstSpace) {
        afterStr = cmd.substring(firstSpace + 1, secondSpace);
        limitStr = cmd.substring(secondSpace + 1);
      } else {
        afterStr = cmd.substring(firstSpace + 1);
      }
      afterStr.trim();
      limitStr.trim();
      if (afterStr.length() > 0) {
        afterSeq = (uint32_t)afterStr.toInt();
      }
      if (limitStr.length() > 0) {
        int32_t parsedLimit = limitStr.toInt();
        if (parsedLimit > 0) {
          limit = (size_t)parsedLimit;
        }
      }
    }
    if (limit < 1U) limit = 1U;
    if (limit > 256U) limit = 256U;
    sendSerialLengthPrefixedPayload(buildTelemetryLivePayload(afterSeq, limit));

  } else if (cmd == "TSTART") {
    if (!telemetryStartLogging(&g_storedVar,
                               g_antiSpinStepMs,
                               g_encoderInvertEnabled,
                               g_adcVoltageRange_mV,
                               (uint8_t)g_carSel)) {
      Serial.println("ERR:Telemetry start failed");
      return;
    }
    sendSerialLengthPrefixedPayload(buildTelemetryStatusPayload("Telemetry logging started"));

  } else if (cmd == "TSTOP") {
    telemetryStopLogging();
    sendSerialLengthPrefixedPayload(buildTelemetryStatusPayload("Telemetry logging stopped"));

  } else if (cmd == "TCLEAR") {
    telemetryStopLogging();
    telemetryClear();
    sendSerialLengthPrefixedPayload(buildTelemetryStatusPayload("Telemetry buffer cleared"));

  } else if (cmd == "TCONFIG") {
    sendSerialLengthPrefixedPayload(buildTelemetryConfigSnapshotJson());

  } else if (cmd == "APPLY") {
    uint16_t previousWiFiMode = g_wifiConfiguredMode;
    char previousWiFiSsid[WIFI_STA_SSID_MAX_LEN + 1];
    char previousWiFiPassword[WIFI_STA_PASS_MAX_LEN + 1];
    copyBoundedString(previousWiFiSsid, sizeof(previousWiFiSsid), g_wifiClientSsid);
    copyBoundedString(previousWiFiPassword, sizeof(previousWiFiPassword), g_wifiClientPassword);
    String lenStr = Serial.readStringUntil('\n');
    int32_t len = lenStr.toInt();
    if (len <= 0 || len > 8192) {
      Serial.println("ERR:invalid length");
      return;
    }
    /* Static buffer avoids heap fragmentation for large payloads */
    static char jsonBuf[8193];
    Serial.setTimeout(15000);
    int32_t got = (int32_t)Serial.readBytes(jsonBuf, (size_t)len);
    Serial.setTimeout(1000);
    if (got != len) { Serial.println("ERR:timeout"); return; }
    jsonBuf[len] = '\0';
    String json(jsonBuf);

    String errorMsg;
    uint8_t carIndex = (uint8_t)g_storedVar.selectedCarNumber;
    if (!parseAndApplyWebPatch(json, &errorMsg, &carIndex)) {
      Serial.println("ERR:" + errorMsg);
      return;
    }
    scheduleWiFiPortalRestartIfNeeded(previousWiFiMode, previousWiFiSsid, previousWiFiPassword);

    String outState = buildStateJson(carIndex);
    Serial.println("OK");
    Serial.print(outState.length());
    Serial.print('\n');
    Serial.print(outState);
    Serial.flush();

  } else if (cmd == "SAVE") {
    saveEEPROM(g_storedVar);
    Serial.println("OK - Saved to flash");
    Serial.flush();

  } else if (cmd == "RESTORE") {
    String lenStr = Serial.readStringUntil('\n');
    int32_t len = lenStr.toInt();
    if (len <= 0 || len > 8192) {
      Serial.println("ERR:invalid length");
      return;
    }
    /* Static buffer avoids heap fragmentation for large payloads */
    static char jsonBuf[8193];
    Serial.setTimeout(15000);
    int32_t got = (int32_t)Serial.readBytes(jsonBuf, (size_t)len);
    Serial.setTimeout(1000);
    if (got != len) { Serial.println("ERR:timeout"); return; }
    jsonBuf[len] = '\0';
    String json(jsonBuf);
    StoredVar_type tempVar;
    String errorMsg;
    String warningMsg;
    uint16_t tempAntiSpinStep = g_antiSpinStepMs;
    uint16_t tempAntiSpinStepPct = g_antiSpinStepPct;
    uint16_t tempAntiSpinDisplayMode = g_antiSpinDisplayMode;
    uint16_t tempBrakeStep = g_brakeStep;
    uint16_t tempSensiStep = g_sensiStep;
    uint16_t tempAdvancedMenu = g_advancedMenuEnabled ? 1 : 0;
    uint16_t tempEncoderInvert = g_encoderInvertEnabled ? 1 : 0;
    uint16_t tempPwmFreqMaxProfile = getConfiguredPwmFreqMaxProfile();
    uint16_t tempAdcVoltageRange = g_adcVoltageRange_mV;
    uint16_t tempWiFiMode = getConfiguredWiFiMode();
    char tempWiFiSsid[WIFI_STA_SSID_MAX_LEN + 1];
    char tempWiFiPassword[WIFI_STA_PASS_MAX_LEN + 1];
    char tempUiAuthUsername[UI_AUTH_USER_MAX_LEN + 1];
    char tempUiAuthPassword[UI_AUTH_PASS_MAX_LEN + 1];
    getConfiguredWiFiClientSsid(tempWiFiSsid, sizeof(tempWiFiSsid));
    getConfiguredWiFiClientPassword(tempWiFiPassword, sizeof(tempWiFiPassword));
    getCurrentUiAuthUsername(tempUiAuthUsername, sizeof(tempUiAuthUsername));
    getCurrentUiAuthPassword(tempUiAuthPassword, sizeof(tempUiAuthPassword));
    if (parseAndValidateJson(json, &tempVar, &tempAntiSpinStep, &tempAntiSpinStepPct, &tempAntiSpinDisplayMode,
                             &tempBrakeStep, &tempSensiStep, &tempAdvancedMenu, &tempEncoderInvert,
                             &tempPwmFreqMaxProfile, &tempAdcVoltageRange,
                             &tempWiFiMode, tempWiFiSsid, sizeof(tempWiFiSsid),
                             tempWiFiPassword, sizeof(tempWiFiPassword),
                             tempUiAuthUsername, sizeof(tempUiAuthUsername),
                             tempUiAuthPassword, sizeof(tempUiAuthPassword),
                             &errorMsg, &warningMsg)) {
      g_storedVar = tempVar;
      g_antiSpinStepMs = tempAntiSpinStep;
      g_antiSpinStepPct = tempAntiSpinStepPct;
      g_antiSpinDisplayMode = tempAntiSpinDisplayMode;
      g_brakeStep = tempBrakeStep;
      g_sensiStep = tempSensiStep;
      if ((g_advancedMenuEnabled ? 1U : 0U) != (tempAdvancedMenu ? 1U : 0U)) {
        applyAdvancedMenuSetting(tempAdvancedMenu);
      } else {
        g_advancedMenuEnabled = tempAdvancedMenu ? 1U : 0U;
      }
      applyEncoderInvertSetting(tempEncoderInvert);
      applyConfiguredPwmFreqMaxProfile(tempPwmFreqMaxProfile);
      applyAdcVoltageRangeMilliVolts(tempAdcVoltageRange);
      setConfiguredWiFiMode(tempWiFiMode);
      setConfiguredWiFiClientCredentials(tempWiFiSsid, tempWiFiPassword);
      setCurrentUiAuthCredentials(tempUiAuthUsername, tempUiAuthPassword);
      initMenuItems();
      saveEEPROM(g_storedVar);
      if (warningMsg.length() > 0) {
        Serial.println("OK - Settings restored (" + warningMsg + ")");
      } else {
        Serial.println("OK - Settings restored");
      }
      Serial.flush();
      delay(500);
      ESP.restart();
    } else {
      Serial.println("ERR:" + errorMsg);
    }
  }
}

/* HTTP route handlers */

static String buildInfoJson() {
  loadWiFiNetworkSettingsIfNeeded();
  buildWifiSuffixIfNeeded();
  char ver[8];
  char dataVer[8];
  char host[24];
  char infoSsid[WIFI_STA_SSID_MAX_LEN + 10];
  char ipText[24];
  char spiffsRelease[16];
  char sensorInfo[40];
  char chipInfo[48];
  char wifiMacStr[24];
  char btMacStr[24];
  char buildInfo[32];
  uint8_t wifiMac[6] = {0};
  uint8_t btMac[6] = {0};
  bool wifiOk = readMacAddress(ESP_MAC_WIFI_STA, wifiMac);
  bool btOk = readMacAddress(ESP_MAC_BT, btMac);
  infoSsid[0] = '\0';
  ipText[0] = '\0';
  spiffsRelease[0] = '\0';

  sprintf(ver, "%d.%d", SW_MAJOR_VERSION, SW_MINOR_VERSION);
  sprintf(dataVer, "v%d", STORED_VAR_VERSION);
  getWiFiPortalHostName(host, sizeof(host));
  HAL_GetTriggerSensorInfo(sensorInfo, sizeof(sensorInfo));

  if (g_wifiActiveMode == WIFI_PORTAL_HOME) {
    copyBoundedString(infoSsid, sizeof(infoSsid),
                      g_wifiConnectedSsid[0] != '\0' ? g_wifiConnectedSsid : g_wifiClientSsid);
    copyBoundedString(ipText, sizeof(ipText), WiFi.localIP().toString().c_str());
  } else if (g_wifiActiveMode == WIFI_PORTAL_AP) {
    getWiFiPortalSsid(infoSsid, sizeof(infoSsid));
    copyBoundedString(ipText, sizeof(ipText), WiFi.softAPIP().toString().c_str());
  } else if (g_wifiConfiguredMode == WIFI_CONFIG_HOME) {
    copyBoundedString(infoSsid, sizeof(infoSsid), g_wifiClientSsid);
  } else {
    getWiFiPortalSsid(infoSsid, sizeof(infoSsid));
  }

  String chipModel = String(ESP.getChipModel());
  snprintf(chipInfo, sizeof(chipInfo), "%s r%d c%d %uMB",
           chipModel.c_str(),
           ESP.getChipRevision(),
           ESP.getChipCores(),
           (unsigned int)(ESP.getFlashChipSize() / (1024UL * 1024UL)));
  if (wifiOk) {
    snprintf(wifiMacStr, sizeof(wifiMacStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             wifiMac[0], wifiMac[1], wifiMac[2], wifiMac[3], wifiMac[4], wifiMac[5]);
  } else {
    copyBoundedString(wifiMacStr, sizeof(wifiMacStr), "unavailable");
  }
  if (btOk) {
    snprintf(btMacStr, sizeof(btMacStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             btMac[0], btMac[1], btMac[2], btMac[3], btMac[4], btMac[5]);
  } else {
    copyBoundedString(btMacStr, sizeof(btMacStr), "unavailable");
  }
  snprintf(buildInfo, sizeof(buildInfo), "%s %s", __DATE__, __TIME__);
  readSpiffsRelease(spiffsRelease, sizeof(spiffsRelease));

  String json;
  json.reserve(700);
  json += "{\"deviceId\":\"";
  json += g_wifiSuffix;
  json += "\",\"version\":\"";
  json += ver;
  json += "\",\"dataVersion\":\"";
  appendJsonEscaped(json, dataVer);
  json += "\",\"spiffsRelease\":\"";
  appendJsonEscaped(json, spiffsRelease);
  json += "\",\"halSensor\":\"";
  appendJsonEscaped(json, sensorInfo);
  json += "\",\"chip\":\"";
  appendJsonEscaped(json, chipInfo);
  json += "\",\"wifiMac\":\"";
  appendJsonEscaped(json, wifiMacStr);
  json += "\",\"btMac\":\"";
  appendJsonEscaped(json, btMacStr);
  json += "\",\"build\":\"";
  appendJsonEscaped(json, buildInfo);
  json += "\",\"docsPath\":\"";
  json += getDefaultDocsPath();
  json += "\",\"wifiMode\":";
  json += String(g_wifiConfiguredMode);
  json += ",\"portalMode\":";
  json += String(getActiveWiFiPortalMode());
  json += ",\"wifiFallback\":";
  json += String(isWiFiPortalApFallbackActive() ? 1 : 0);
  json += ",\"hostName\":\"";
  appendJsonEscaped(json, host);
  json += "\",\"ssid\":\"";
  appendJsonEscaped(json, infoSsid);
  json += "\",\"ipAddress\":\"";
  appendJsonEscaped(json, ipText);
  json += "\",\"screensaverLine1\":\"";
  appendJsonEscaped(json, g_storedVar.screensaverLine1);
  json += "\",\"screensaverLine2\":\"";
  appendJsonEscaped(json, g_storedVar.screensaverLine2);
  json += "\",\"authDefault\":";
  json += String(areCurrentUiAuthCredentialsDefault() ? 1 : 0);
  json += "}";

  return json;
}

static void handleInfo() {
  g_wifiServer->send(200, "application/json", buildInfoJson());
}

static void handleSchema() {
  g_wifiServer->send(200, "application/json", buildSchemaJson());
}

static void handleState() {
  uint8_t carIndex = getRequestedCarIndex();
  g_wifiServer->send(200, "application/json", buildStateJson(carIndex));
}

static void handleApply() {
  String payload = g_wifiServer->arg("plain");
  if (payload.length() == 0) {
    g_wifiServer->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
    return;
  }

  String saveArg = g_wifiServer->arg("save");
  bool saveAfter = saveArg == "1" || saveArg == "true" || saveArg == "on";

  String errorMsg;
  uint16_t previousWiFiMode = g_wifiConfiguredMode;
  char previousWiFiSsid[WIFI_STA_SSID_MAX_LEN + 1];
  char previousWiFiPassword[WIFI_STA_PASS_MAX_LEN + 1];
  copyBoundedString(previousWiFiSsid, sizeof(previousWiFiSsid), g_wifiClientSsid);
  copyBoundedString(previousWiFiPassword, sizeof(previousWiFiPassword), g_wifiClientPassword);
  uint8_t carIndex = g_storedVar.selectedCarNumber;
  if (!parseAndApplyWebPatch(payload, &errorMsg, &carIndex)) {
    String out = "{\"ok\":false,\"error\":\"";
    appendJsonEscaped(out, errorMsg.c_str());
    out += "\"}";
    g_wifiServer->send(400, "application/json", out);
    return;
  }

  if (saveAfter) {
    saveEEPROM(g_storedVar);
  }

  String out = "{\"ok\":true,\"saved\":";
  out += String(saveAfter ? 1 : 0);
  out += ",\"state\":";
  out += buildStateJson(carIndex);
  out += "}";
  g_wifiServer->send(200, "application/json", out);
  scheduleWiFiPortalRestartIfNeeded(previousWiFiMode, previousWiFiSsid, previousWiFiPassword);
}

static void handleSave() {
  saveEEPROM(g_storedVar);
  g_wifiServer->send(200, "application/json", "{\"ok\":true,\"message\":\"Saved to flash\"}");
}

static uint32_t getTelemetryArgU32(const char* name, uint32_t defaultValue) {
  if (g_wifiServer == nullptr || name == nullptr || !g_wifiServer->hasArg(name)) {
    return defaultValue;
  }

  String raw = g_wifiServer->arg(name);
  raw.trim();
  if (raw.length() == 0) {
    return defaultValue;
  }

  bool valid = true;
  for (uint16_t i = 0; i < raw.length(); i++) {
    if (!isDigit(raw[i])) {
      valid = false;
      break;
    }
  }
  if (!valid) {
    return defaultValue;
  }

  return (uint32_t)raw.toInt();
}

static size_t getTelemetryLiveLimit() {
  uint32_t limit = getTelemetryArgU32("limit", 120U);
  if (limit == 0U) {
    limit = 120U;
  }
  if (limit > 256U) {
    limit = 256U;
  }
  return (size_t)limit;
}

static void formatTenthPercentValue(uint16_t raw, char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  snprintf(out, outLen, "%u.%u", raw / 10U, raw % 10U);
}

static void appendPercentX10JsonValue(String& out, uint16_t raw) {
  out += String((unsigned int)(raw / 10U));
  out += ".";
  out += String((unsigned int)(raw % 10U));
}

static const char* getTelemetryReleaseModeLabel(uint8_t mode) {
  switch (mode) {
    case RELEASE_BRAKE_QUICK: return "QUICK";
    case RELEASE_BRAKE_DRAG:  return "DRAG";
    default:                  return "OFF";
  }
}

static void appendTelemetryCarParamJson(String& json, const CarParam_type& car) {
  char buf[256];
  json += "{";
  json += "\"name\":\"";
  appendJsonEscaped(json, car.carName);
  json += "\"";
  snprintf(buf, sizeof(buf),
           ",\"carNumber\":%u,\"minSpeedHalfPct\":%u,\"brake\":%u.%u,\"maxSpeed\":%u,"
           "\"curveInputPct\":%u,\"curveDiffPct\":%u,\"fade\":%u,\"antiSpin\":%u,"
           "\"freqPwm100Hz\":%u,\"brakeButton\":%u,\"releaseMode\":%u,\"releaseZone\":%u,\"releaseLevel\":%u",
           (unsigned int)car.carNumber,
           (unsigned int)car.minSpeed,
           (unsigned int)(car.brake / BRAKE_SCALE),
           (unsigned int)brakeFracDigit(car.brake),
           (unsigned int)car.maxSpeed,
           (unsigned int)car.throttleCurveVertex.inputThrottle,
           (unsigned int)car.throttleCurveVertex.curveSpeedDiff,
           (unsigned int)car.fade,
           (unsigned int)car.antiSpin,
           (unsigned int)car.freqPWM,
           (unsigned int)car.brakeButtonReduction,
           (unsigned int)car.quickBrakeEnabled,
           (unsigned int)car.quickBrakeThreshold,
           (unsigned int)car.quickBrakeStrength);
  json += buf;
  json += "}";
}

static void appendTelemetryCarNamesJson(String& json, const StoredVar_type& storedVar) {
  json += "[";
  for (uint8_t i = 0; i < CAR_MAX_COUNT; i++) {
    if (i > 0) {
      json += ",";
    }
    json += "\"";
    appendJsonEscaped(json, storedVar.carParam[i].carName);
    json += "\"";
  }
  json += "]";
}

static String buildTelemetryConfigSummaryJsonFromSnapshot(const TelemetryConfigSnapshot& snapshot,
                                                          uint8_t activeCarIndex) {
  if (activeCarIndex >= CAR_MAX_COUNT) {
    activeCarIndex = 0;
  }

  String json;
  json.reserve(960);
  json += "{";
  json += "\"selectedCarNumber\":";
  json += String(activeCarIndex);
  json += ",\"antiSpinStepMs\":";
  json += String(snapshot.antiSpinStepMs);
  json += ",\"encoderInvert\":";
  json += String(snapshot.encoderInvertEnabled ? 1U : 0U);
  json += ",\"adcVoltageRangeMv\":";
  json += String(snapshot.adcVoltageRange_mV);
  json += ",\"statusSlots\":[";
  for (uint8_t i = 0; i < STATUS_SLOTS; i++) {
    if (i > 0) {
      json += ",";
    }
    json += String(normalizeStatusSlotForUi(snapshot.storedVar.statusSlot[i]));
  }
  json += "],\"carNames\":";
  appendTelemetryCarNamesJson(json, snapshot.storedVar);
  json += ",\"activeCar\":";
  appendTelemetryCarParamJson(json, snapshot.storedVar.carParam[activeCarIndex]);
  json += "}";
  return json;
}

static void appendTelemetryEventJson(String& json, const TelemetryEvent& event) {
  json += "{";
  json += "\"id\":";
  json += String((unsigned long)event.id);
  json += ",\"tMs\":";
  json += String((unsigned long)event.t_ms);
  json += ",\"sampleSeq\":";
  json += String((unsigned long)event.sampleSeq);
  json += ",\"type\":\"";
  json += (event.type == TELEMETRY_EVENT_CAR_SELECT) ? "car_select" : "car_params";
  json += "\",\"carIndex\":";
  json += String((unsigned int)event.carIndex);
  json += ",\"previousCarIndex\":";
  json += String((unsigned int)event.previousCarIndex);
  json += ",\"changedMask\":";
  json += String((unsigned int)event.changedMask);
  json += ",\"carParams\":";
  appendTelemetryCarParamJson(json, event.carParam);
  json += "}";
}

static void appendTelemetryStatusFields(String& json, const TelemetryStatus& status) {
  char buf[320];
  uint8_t sessionCarIndex = (status.sessionStartCarIndex < CAR_MAX_COUNT) ? status.sessionStartCarIndex : 0;
  uint8_t currentCarIndex = (g_carSel < CAR_MAX_COUNT) ? (uint8_t)g_carSel : (uint8_t)g_storedVar.selectedCarNumber;
  uint8_t triggerPct = (uint8_t)(((uint32_t)g_escVar.trigger_norm * 100U) / THROTTLE_NORMALIZED);
  uint8_t outputPct = (uint8_t)constrain((int)g_escVar.outputSpeed_pct, 0, 100);
  uint16_t brakePct = constrain(g_escVar.effectiveBrake_raw, 0, BRAKE_MAX_VALUE);
  if (digitalRead(BUTT_PIN) == BUTTON_PRESSED && g_escVar.trigger_norm == 0) {
    brakePct = constrain(g_storedVar.carParam[currentCarIndex].brakeButtonReduction * BRAKE_SCALE, 0, BRAKE_MAX_VALUE);
  }

  snprintf(buf, sizeof(buf),
           ",\"loggingActive\":%u,\"hasData\":%u,\"wrapped\":%u,\"sampleRateMs\":%u,\"capacity\":%u,"
           "\"storedCount\":%u,\"eventCount\":%u,\"eventCapacity\":%u,"
           "\"sessionId\":%lu,\"sessionStartMs\":%lu,\"oldestSeq\":%lu,\"latestSeq\":%lu,"
           "\"oldestEventId\":%lu,\"latestEventId\":%lu,"
           "\"wifiActive\":%u,\"sessionStartCarIndex\":%u,\"currentCarIndex\":%u",
           status.loggingActive ? 1U : 0U,
           status.hasData ? 1U : 0U,
           status.wrapped ? 1U : 0U,
           status.sampleRateMs,
           status.capacity,
           status.storedCount,
           status.eventCount,
           status.eventCapacity,
           (unsigned long)status.sessionId,
           (unsigned long)status.sessionStartMs,
           (unsigned long)status.oldestSeq,
           (unsigned long)status.latestSeq,
           (unsigned long)status.oldestEventId,
           (unsigned long)status.latestEventId,
           isWiFiPortalActive() ? 1U : 0U,
           sessionCarIndex,
           currentCarIndex);
  json += buf;

  json += ",\"sessionStartCarName\":\"";
  appendJsonEscaped(json, g_storedVar.carParam[sessionCarIndex].carName);
  json += "\",\"currentCarName\":\"";
  appendJsonEscaped(json, g_storedVar.carParam[currentCarIndex].carName);
  json += "\",\"current\":{";

  snprintf(buf, sizeof(buf),
           "\"triggerPct\":%u,\"outputPct\":%u,\"brakePct\":%u.%u,\"sensiPct\":%u,\"sensiHalfPct\":%u,"
           "\"vinMv\":%u,\"currentMa\":%u,\"releaseMode\":%u,\"currentSense\":%u",
           triggerPct,
           outputPct,
           (unsigned int)(brakePct / BRAKE_SCALE),
           (unsigned int)brakeFracDigit(brakePct),
           g_escVar.effectiveSensi_raw,
           g_escVar.effectiveSensi_raw,
           g_escVar.Vin_mV,
           g_escVar.motorCurrent_mA,
           g_storedVar.carParam[currentCarIndex].quickBrakeEnabled,
           HAL_HasMotorCurrentSense() ? 1U : 0U);
  json += buf;

  json += ",\"statusSlots\":[";
  for (uint8_t i = 0; i < STATUS_SLOTS; i++) {
    if (i > 0) json += ",";
    json += String(normalizeStatusSlotForUi(g_storedVar.statusSlot[i]));
  }
  json += "]}";
}

static String buildTelemetryStatusPayload(const char* message) {
  TelemetryStatus status;
  telemetryGetStatus(&status);

  String json;
  json.reserve(768);
  json += "{\"ok\":true";
  if (message != nullptr && message[0] != '\0') {
    json += ",\"message\":\"";
    appendJsonEscaped(json, message);
    json += "\"";
  }
  appendTelemetryStatusFields(json, status);
  json += "}";
  return json;
}

static String buildTelemetryLivePayload(uint32_t afterSeq, size_t limit) {
  static TelemetrySample samples[256];
  static TelemetryEvent events[TELEMETRY_EVENT_BUFFER_CAPACITY];
  TelemetryStatus status;
  bool truncated = false;
  bool hasMore = false;
  bool eventsTruncated = false;
  size_t copied = telemetryCopySamplesAfter(afterSeq, samples, limit, &truncated, &hasMore, &status);
  size_t eventCopied = telemetryCopyEvents(events, TELEMETRY_EVENT_BUFFER_CAPACITY, &eventsTruncated, nullptr);

  String json;
  json.reserve(1600 + (copied * 92U) + (eventCopied * 180U));
  json += "{\"ok\":true";
  appendTelemetryStatusFields(json, status);
  json += ",\"truncated\":";
  json += truncated ? "true" : "false";
  json += ",\"hasMore\":";
  json += hasMore ? "true" : "false";
  json += ",\"returned\":";
  json += String((unsigned long)copied);
  json += ",\"eventTruncated\":";
  json += eventsTruncated ? "true" : "false";
  json += ",\"events\":[";
  for (size_t i = 0; i < eventCopied; i++) {
    if (i > 0) {
      json += ",";
    }
    appendTelemetryEventJson(json, events[i]);
  }
  json += "],\"samples\":[";

  for (size_t i = 0; i < copied; i++) {
    const TelemetrySample& s = samples[i];
    if (i > 0) {
      json += ",";
    }
    json += "[";
    json += String((unsigned long)s.seq);
    json += ",";
    json += String((unsigned long)s.t_ms);
    json += ",";
    json += String((unsigned int)s.trigger_pct);
    json += ",";
    json += String((unsigned int)s.output_pct);
    json += ",";
    json += String((unsigned int)s.vin_mV);
    json += ",";
    json += String((unsigned int)s.current_mA);
    json += ",";
    appendPercentX10JsonValue(json, s.brake_pct);
    json += ",";
    json += String((unsigned int)s.sensi_halfPct);
    json += ",";
    json += String((unsigned int)s.carIndex);
    json += ",";
    json += String((unsigned int)s.releaseMode);
    json += ",";
    json += String((unsigned int)s.flags);
    json += "]";
  }

  json += "]}";
  return json;
}

static String buildTelemetryConfigSnapshotJson() {
  TelemetryConfigSnapshot snapshot;
  TelemetryStatus status;
  bool hasSnapshot = telemetryGetConfigSnapshot(&snapshot);
  telemetryGetStatus(&status);
  if (!hasSnapshot) {
    snapshot.storedVar = g_storedVar;
    snapshot.antiSpinStepMs = g_antiSpinStepMs;
    snapshot.encoderInvertEnabled = g_encoderInvertEnabled;
    snapshot.adcVoltageRange_mV = g_adcVoltageRange_mV;
  }
  uint8_t activeCarIndex = status.hasData ? status.sessionStartCarIndex : (uint8_t)g_carSel;
  if (activeCarIndex >= CAR_MAX_COUNT) {
    activeCarIndex = (uint8_t)g_storedVar.selectedCarNumber;
  }
  return buildTelemetryConfigSummaryJsonFromSnapshot(snapshot, activeCarIndex);
}

static void sendSerialLengthPrefixedPayload(const String& payload) {
  Serial.print(payload.length());
  Serial.print('\n');
  Serial.print(payload);
  Serial.flush();
}

static void handleTelemetryStatus() {
  g_wifiServer->send(200, "application/json", buildTelemetryStatusPayload(""));
}

static void handleTelemetryStart() {
  if (!telemetryStartLogging(&g_storedVar,
                             g_antiSpinStepMs,
                             g_encoderInvertEnabled,
                             g_adcVoltageRange_mV,
                             (uint8_t)g_carSel)) {
    g_wifiServer->send(500, "application/json", "{\"ok\":false,\"error\":\"Telemetry start failed\"}");
    return;
  }

  g_wifiServer->send(200, "application/json", buildTelemetryStatusPayload("Telemetry logging started"));
}

static void handleTelemetryStop() {
  telemetryStopLogging();
  g_wifiServer->send(200, "application/json", buildTelemetryStatusPayload("Telemetry logging stopped"));
}

static void handleTelemetryClear() {
  telemetryStopLogging();
  telemetryClear();
  g_wifiServer->send(200, "application/json", buildTelemetryStatusPayload("Telemetry buffer cleared"));
}

static void handleTelemetryLive() {
  uint32_t afterSeq = getTelemetryArgU32("after", 0U);
  size_t limit = getTelemetryLiveLimit();
  g_wifiServer->send(200, "application/json", buildTelemetryLivePayload(afterSeq, limit));
}

static void handleTelemetryExportCsv() {
  TelemetryStatus status;
  telemetryGetStatus(&status);
  if (!status.hasData) {
    g_wifiServer->send(404, "application/json", "{\"ok\":false,\"error\":\"No telemetry data available\"}");
    return;
  }

  TelemetryConfigSnapshot snapshot;
  bool hasSnapshot = telemetryGetConfigSnapshot(&snapshot);
  if (!hasSnapshot) {
    snapshot.storedVar = g_storedVar;
    snapshot.antiSpinStepMs = g_antiSpinStepMs;
    snapshot.encoderInvertEnabled = g_encoderInvertEnabled;
    snapshot.adcVoltageRange_mV = g_adcVoltageRange_mV;
  }

  g_wifiServer->setContentLength(CONTENT_LENGTH_UNKNOWN);
  g_wifiServer->sendHeader("Content-Disposition", "attachment; filename=\"espeed32-telemetry.csv\"");
  g_wifiServer->send(200, "text/csv; charset=utf-8", "");

  g_wifiServer->sendContent("seq,t_ms,car_index,car_name,trigger_pct,output_pct,brake_pct,sensi_pct,vin_mV,current_mA,release_mode,brake_button,trigger_releasing,in_release_zone,release_active,current_sense\n");

  static TelemetrySample batch[96];
  uint32_t afterSeq = 0;

  while (true) {
    TelemetryStatus batchStatus;
    bool truncated = false;
    bool hasMore = false;
    size_t copied = telemetryCopySamplesAfter(afterSeq, batch, 96U, &truncated, &hasMore, &batchStatus);
    if (copied == 0) {
      break;
    }

    String chunk;
    chunk.reserve(copied * 96U);
    for (size_t i = 0; i < copied; i++) {
      const TelemetrySample& s = batch[i];
      char sensiBuf[12];
      char row[192];
      char brakeBuf[12];
      formatTenthPercentValue(s.sensi_halfPct, sensiBuf, sizeof(sensiBuf));
      snprintf(brakeBuf, sizeof(brakeBuf), "%u.%u", (unsigned int)(s.brake_pct / BRAKE_SCALE), (unsigned int)brakeFracDigit(s.brake_pct));
      snprintf(row, sizeof(row),
               "%lu,%lu,%u,%s,%u,%u,%s,%s,%u,%u,%s,%u,%u,%u,%u,%u\n",
               (unsigned long)s.seq,
               (unsigned long)s.t_ms,
               (unsigned int)s.carIndex,
               snapshot.storedVar.carParam[s.carIndex].carName,
               (unsigned int)s.trigger_pct,
               (unsigned int)s.output_pct,
               brakeBuf,
               sensiBuf,
               (unsigned int)s.vin_mV,
               (unsigned int)s.current_mA,
               getTelemetryReleaseModeLabel(s.releaseMode),
               (s.flags & TELEMETRY_FLAG_BRAKE_BUTTON) ? 1U : 0U,
               (s.flags & TELEMETRY_FLAG_TRIGGER_RELEASING) ? 1U : 0U,
               (s.flags & TELEMETRY_FLAG_IN_RELEASE_ZONE) ? 1U : 0U,
               (s.flags & TELEMETRY_FLAG_RELEASE_ACTIVE) ? 1U : 0U,
               (s.flags & TELEMETRY_FLAG_CURRENT_SENSE) ? 1U : 0U);
      chunk += row;
      afterSeq = s.seq;
    }

    g_wifiServer->sendContent(chunk);
    if (!hasMore) {
      break;
    }
  }

  g_wifiServer->sendContent("");
}

static void handleTelemetryExportJson() {
  TelemetryStatus status;
  telemetryGetStatus(&status);
  if (!status.hasData) {
    g_wifiServer->send(404, "application/json", "{\"ok\":false,\"error\":\"No telemetry data available\"}");
    return;
  }

  TelemetryConfigSnapshot snapshot;
  bool hasSnapshot = telemetryGetConfigSnapshot(&snapshot);
  if (!hasSnapshot) {
    snapshot.storedVar = g_storedVar;
    snapshot.antiSpinStepMs = g_antiSpinStepMs;
    snapshot.encoderInvertEnabled = g_encoderInvertEnabled;
    snapshot.adcVoltageRange_mV = g_adcVoltageRange_mV;
  }

  char versionBuf[8];
  snprintf(versionBuf, sizeof(versionBuf), "%d.%d", SW_MAJOR_VERSION, SW_MINOR_VERSION);

  g_wifiServer->setContentLength(CONTENT_LENGTH_UNKNOWN);
  g_wifiServer->sendHeader("Content-Disposition", "attachment; filename=\"espeed32-telemetry.json\"");
  g_wifiServer->send(200, "application/json; charset=utf-8", "");

  static TelemetryEvent events[TELEMETRY_EVENT_BUFFER_CAPACITY];
  bool eventsTruncated = false;
  size_t eventCount = telemetryCopyEvents(events, TELEMETRY_EVENT_BUFFER_CAPACITY, &eventsTruncated, nullptr);

  String head;
  head.reserve(1800 + (eventCount * 180U));
  head += "{";
  head += "\"ok\":true,\"format\":\"espeed32-telemetry-v1\",\"deviceId\":\"";
  appendJsonEscaped(head, g_wifiSuffix);
  head += "\",\"firmware\":\"";
  appendJsonEscaped(head, versionBuf);
  head += "\",\"loggingActive\":";
  head += status.loggingActive ? "true" : "false";
  head += ",\"sampleRateMs\":";
  head += String(status.sampleRateMs);
  head += ",\"capacity\":";
  head += String(status.capacity);
  head += ",\"storedCount\":";
  head += String(status.storedCount);
  head += ",\"sessionId\":";
  head += String((unsigned long)status.sessionId);
  head += ",\"sessionStartMs\":";
  head += String((unsigned long)status.sessionStartMs);
  head += ",\"sessionStartCarIndex\":";
  head += String(status.sessionStartCarIndex);
  head += ",\"configAtStart\":";
  head += buildTelemetryConfigSummaryJsonFromSnapshot(snapshot, status.sessionStartCarIndex);
  head += ",\"eventsTruncated\":";
  head += eventsTruncated ? "true" : "false";
  head += ",\"events\":[";
  for (size_t i = 0; i < eventCount; i++) {
    if (i > 0) {
      head += ",";
    }
    appendTelemetryEventJson(head, events[i]);
  }
  head += "],\"samples\":[";
  g_wifiServer->sendContent(head);

  static TelemetrySample batch[64];
  uint32_t afterSeq = 0;
  bool first = true;

  while (true) {
    TelemetryStatus batchStatus;
    bool truncated = false;
    bool hasMore = false;
    size_t copied = telemetryCopySamplesAfter(afterSeq, batch, 64U, &truncated, &hasMore, &batchStatus);
    if (copied == 0) {
      break;
    }

    String chunk;
    chunk.reserve(copied * 128U);
    for (size_t i = 0; i < copied; i++) {
      const TelemetrySample& s = batch[i];
      if (!first) {
        chunk += ",";
      }
      first = false;
      chunk += "{\"seq\":";
      chunk += String((unsigned long)s.seq);
      chunk += ",\"tMs\":";
      chunk += String((unsigned long)s.t_ms);
      chunk += ",\"triggerPct\":";
      chunk += String((unsigned int)s.trigger_pct);
      chunk += ",\"outputPct\":";
      chunk += String((unsigned int)s.output_pct);
      chunk += ",\"vinMv\":";
      chunk += String((unsigned int)s.vin_mV);
      chunk += ",\"currentMa\":";
      chunk += String((unsigned int)s.current_mA);
      chunk += ",\"brakePct\":";
      appendPercentX10JsonValue(chunk, s.brake_pct);
      chunk += ",\"sensiPct\":";
      chunk += String((unsigned int)s.sensi_halfPct);
      chunk += ",\"sensiHalfPct\":";
      chunk += String((unsigned int)s.sensi_halfPct);
      chunk += ",\"carIndex\":";
      chunk += String((unsigned int)s.carIndex);
      chunk += ",\"releaseMode\":";
      chunk += String((unsigned int)s.releaseMode);
      chunk += ",\"flags\":";
      chunk += String((unsigned int)s.flags);
      chunk += "}";
      afterSeq = s.seq;
    }

    g_wifiServer->sendContent(chunk);
    if (!hasMore) {
      break;
    }
  }

  g_wifiServer->sendContent("]}");
}

static void handleRoot() {
  char spiffsVer[16];
  if (!spiffsMatchesFirmware(spiffsVer, sizeof(spiffsVer))) {
    sendSpiffsMismatch(spiffsVer);
    return;
  }
  if (!streamHtmlFromSpiffs(getPublicUiPath())) {
    sendPublicUiFallback();
  }
}

static void handleUi() {
  if (!requireControllerAuth()) {
    return;
  }
  char spiffsVer[16];
  if (!spiffsMatchesFirmware(spiffsVer, sizeof(spiffsVer))) {
    sendSpiffsMismatch(spiffsVer);
    return;
  }
  if (!streamHtmlFromSpiffs(getUiPath())) {
    sendUiFallback();
  }
}

static void handleBackup() {
  String json = buildJsonBackup();
  g_wifiServer->send(200, "application/json", json);
}

static void handleDocsDefault() {
  const char* defaultPath = getDefaultDocsPath();
  if (!streamHtmlFromSpiffs(defaultPath) &&
      !streamHtmlFromSpiffs("/docs/en/index.html") &&
      !streamHtmlFromSpiffs("/docs/no/index.html") &&
      !streamHtmlFromSpiffs("/docs/nl/index.html") &&
      !streamHtmlFromSpiffs("/docs/pt/index.html") &&
      !streamHtmlFromSpiffs("/docs/es/index.html") &&
      !streamHtmlFromSpiffs("/docs/de/index.html") &&
      !streamHtmlFromSpiffs("/docs/it/index.html")) {
    sendDocsMissing(defaultPath);
  }
}

static void handleDocsEn() {
  if (!streamHtmlFromSpiffs("/docs/en/index.html")) {
    sendDocsMissing("/docs/en/index.html");
  }
}

static void handleDocsNo() {
  if (!streamHtmlFromSpiffs("/docs/no/index.html")) {
    sendDocsMissing("/docs/no/index.html");
  }
}

static void handleDocsNl() {
  if (!streamHtmlFromSpiffs("/docs/nl/index.html")) {
    sendDocsMissing("/docs/nl/index.html");
  }
}

static void handleDocsPt() {
  if (!streamHtmlFromSpiffs("/docs/pt/index.html")) {
    sendDocsMissing("/docs/pt/index.html");
  }
}

static void handleDocsEs() {
  if (!streamHtmlFromSpiffs("/docs/es/index.html")) {
    sendDocsMissing("/docs/es/index.html");
  }
}

static void handleDocsDe() {
  if (!streamHtmlFromSpiffs("/docs/de/index.html")) {
    sendDocsMissing("/docs/de/index.html");
  }
}

static void handleDocsIt() {
  if (!streamHtmlFromSpiffs("/docs/it/index.html")) {
    sendDocsMissing("/docs/it/index.html");
  }
}

static void handleDocsCurveAsset() {
  if (!streamFileFromSpiffs("/docs/assets/curve_examples.png", "image/png")) {
    g_wifiServer->send(404, "text/plain", "Missing asset: /docs/assets/curve_examples.png");
  }
}

static void handleDocsTriggerAsset() {
  if (!streamFileFromSpiffs("/docs/assets/trig_cal.png", "image/png")) {
    g_wifiServer->send(404, "text/plain", "Missing asset: /docs/assets/trig_cal.png");
  }
}

static void handleFaviconSvg() {
  if (!streamFileFromSpiffs("/favicon.svg", "image/svg+xml")) {
    g_wifiServer->send(404, "text/plain", "Missing asset: /favicon.svg");
  }
}

static void handleRestoreUpload() {
  HTTPUpload& upload = g_wifiServer->upload();
  if (upload.status == UPLOAD_FILE_START) {
    g_uploadBuffer = "";
    g_uploadBuffer.reserve(6144);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    g_uploadBuffer += String((char*)upload.buf, upload.currentSize);
  }
}

static void handleRestore() {
  if (g_uploadBuffer.length() == 0) {
    g_wifiServer->send(400, "text/plain", "Error: no file uploaded");
    return;
  }

  StoredVar_type tempVar;
  String errorMsg;
  String warningMsg;

  uint16_t tempAntiSpinStep = g_antiSpinStepMs;
  uint16_t tempAntiSpinStepPct = g_antiSpinStepPct;
  uint16_t tempAntiSpinDisplayMode = g_antiSpinDisplayMode;
  uint16_t tempBrakeStep = g_brakeStep;
  uint16_t tempSensiStep = g_sensiStep;
  uint16_t tempAdvancedMenu = g_advancedMenuEnabled ? 1 : 0;
  uint16_t tempEncoderInvert = g_encoderInvertEnabled ? 1 : 0;
  uint16_t tempPwmFreqMaxProfile = getConfiguredPwmFreqMaxProfile();
  uint16_t tempAdcVoltageRange = g_adcVoltageRange_mV;
  uint16_t tempWiFiMode = getConfiguredWiFiMode();
  char tempWiFiSsid[WIFI_STA_SSID_MAX_LEN + 1];
  char tempWiFiPassword[WIFI_STA_PASS_MAX_LEN + 1];
  char tempUiAuthUsername[UI_AUTH_USER_MAX_LEN + 1];
  char tempUiAuthPassword[UI_AUTH_PASS_MAX_LEN + 1];
  getConfiguredWiFiClientSsid(tempWiFiSsid, sizeof(tempWiFiSsid));
  getConfiguredWiFiClientPassword(tempWiFiPassword, sizeof(tempWiFiPassword));
  getCurrentUiAuthUsername(tempUiAuthUsername, sizeof(tempUiAuthUsername));
  getCurrentUiAuthPassword(tempUiAuthPassword, sizeof(tempUiAuthPassword));
  if (parseAndValidateJson(g_uploadBuffer, &tempVar, &tempAntiSpinStep, &tempAntiSpinStepPct, &tempAntiSpinDisplayMode,
                           &tempBrakeStep, &tempSensiStep, &tempAdvancedMenu, &tempEncoderInvert,
                           &tempPwmFreqMaxProfile, &tempAdcVoltageRange,
                           &tempWiFiMode, tempWiFiSsid, sizeof(tempWiFiSsid),
                           tempWiFiPassword, sizeof(tempWiFiPassword),
                           tempUiAuthUsername, sizeof(tempUiAuthUsername),
                           tempUiAuthPassword, sizeof(tempUiAuthPassword),
                           &errorMsg, &warningMsg)) {
    g_storedVar = tempVar;
    g_antiSpinStepMs = tempAntiSpinStep;
    g_antiSpinStepPct = tempAntiSpinStepPct;
    g_antiSpinDisplayMode = tempAntiSpinDisplayMode;
    g_brakeStep = tempBrakeStep;
    g_sensiStep = tempSensiStep;
    if ((g_advancedMenuEnabled ? 1U : 0U) != (tempAdvancedMenu ? 1U : 0U)) {
      applyAdvancedMenuSetting(tempAdvancedMenu);
    } else {
      g_advancedMenuEnabled = tempAdvancedMenu ? 1U : 0U;
    }
    applyEncoderInvertSetting(tempEncoderInvert);
    applyConfiguredPwmFreqMaxProfile(tempPwmFreqMaxProfile);
    applyAdcVoltageRangeMilliVolts(tempAdcVoltageRange);
    setConfiguredWiFiMode(tempWiFiMode);
    setConfiguredWiFiClientCredentials(tempWiFiSsid, tempWiFiPassword);
    setCurrentUiAuthCredentials(tempUiAuthUsername, tempUiAuthPassword);
    initMenuItems();
    saveEEPROM(g_storedVar);
    String response = "OK - Settings restored!";
    if (warningMsg.length() > 0) {
      response += " ";
      response += warningMsg;
      response += ".";
    }
    response += " Restarting...";
    g_wifiServer->send(200, "text/plain", response);
    g_uploadBuffer = "";
    delay(1000);
    ESP.restart();
  } else {
    g_wifiServer->send(400, "text/plain", errorMsg);
  }
  g_uploadBuffer = "";
}


/**
 * @brief Handle OTA upload - streams firmware (.bin) or SPIFFS image (.bin) directly to flash
 */
static void handleOtaUpload() {
  HTTPUpload& upload = g_wifiServer->upload();
  char msgStr[32];
  String requestToken = g_wifiServer->header("X-Ota-Token");

  if (upload.status == UPLOAD_FILE_START) {
    /* Only reject while an upload is actively streaming. Paired OTA uses
     * deferred restart after firmware stage 1, and the follow-up SPIFFS
     * upload must still be allowed during that grace window. */
    if (!requestToken.isEmpty() && g_otaInProgress) {
      g_otaRejectedToken = requestToken;
      return;
    }

    if (!requestToken.isEmpty()) {
      g_otaActiveToken = requestToken;
      g_otaRejectedToken = "";
    }

    String uri = g_wifiServer->uri();
    g_otaTargetSpiffs = (uri == "/ota-spiffs");
    g_otaTotal = upload.totalSize;  /* May be 0 if browser doesn't send content-length */
    g_otaWritten = 0;
    g_otaLastUiUpdateMs = 0;
    g_otaLastUiShownKb = 0;
    g_otaInProgress = true;
    g_otaSessionOk = true;
    g_otaRestartRequested = otaRequestWantsRestart();

    /* Show update in progress on OLED */
    obdFill(&g_obd, OBD_WHITE, 1);
    if (g_otaTargetSpiffs) {
      obdWriteString(&g_obd, 0, 8, 0, (char*)"SPIFFS Update", FONT_8x8, OBD_BLACK, 1);
    } else {
      obdWriteString(&g_obd, 0, 16, 0, (char*)"OTA Update", FONT_8x8, OBD_BLACK, 1);
    }
    obdWriteString(&g_obd, 0, 0, 3 * HEIGHT8x8, (char*)"Updating...", FONT_8x8, OBD_BLACK, 1);
    obdWriteString(&g_obd, 0, 0, 6 * HEIGHT8x8, (char*)"Do not power off!", FONT_6x8, OBD_BLACK, 1);

    if (g_otaTargetSpiffs && g_spiffsMounted) {
      SPIFFS.end();
      g_spiffsMounted = false;
    }

    /* Boost WiFi TX power for the duration of the upload. In STA (home WiFi)
     * mode this helps maintain a stable connection during the large transfer.
     * The previous value is saved and restored at END/ABORTED. */
    esp_wifi_get_max_tx_power(&g_otaPrevTxPower);
    esp_wifi_set_max_tx_power(84); /* 84 * 0.25 dBm = 21 dBm (maximum) */

#if defined(U_SPIFFS)
    int updateCommand = g_otaTargetSpiffs ? U_SPIFFS : U_FLASH;
#else
    int updateCommand = U_FLASH;
    if (g_otaTargetSpiffs) {
      g_otaSessionOk = false;
      g_otaInProgress = false;
      return;
    }
#endif

    /* Use the binary size sent by the UI if available so Update.begin()
     * allocates the exact partition space instead of UPDATE_SIZE_UNKNOWN.
     * Falls back to UPDATE_SIZE_UNKNOWN if the header is absent, non-positive,
     * or otherwise invalid. */
    long binarySizeSigned = g_wifiServer->header("X-Binary-Size").toInt();
    size_t binarySize = (binarySizeSigned > 0) ? static_cast<size_t>(binarySizeSigned) : 0;
    if (!Update.begin(binarySize > 0 ? binarySize : UPDATE_SIZE_UNKNOWN, updateCommand)) {
      g_otaSessionOk = false;
      g_otaInProgress = false;
    }

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!requestToken.isEmpty() && requestToken != g_otaActiveToken) {
      return;
    }
    if (!g_otaSessionOk) {
      return;
    }
    if (Update.write(upload.buf, upload.currentSize) == upload.currentSize) {
      g_otaWritten += upload.currentSize;
      /* Throttle OLED writes during OTA to avoid wasting time on I2C updates. */
      uint32_t now = millis();
      size_t shownKb = g_otaWritten / 1024U;
      if (g_otaLastUiUpdateMs == 0 ||
          (uint32_t)(now - g_otaLastUiUpdateMs) >= 250U ||
          shownKb >= (g_otaLastUiShownKb + 16U)) {
        sprintf(msgStr, "%u KB", (unsigned int)shownKb);
        obdWriteString(&g_obd, 0, 0, 4 * HEIGHT8x8, msgStr, FONT_8x8, OBD_BLACK, 1);
        g_otaLastUiUpdateMs = now;
        g_otaLastUiShownKb = shownKb;
      }
    } else {
      g_otaSessionOk = false;
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    if (!requestToken.isEmpty() && requestToken != g_otaActiveToken) {
      return;
    }
    esp_wifi_set_max_tx_power(g_otaPrevTxPower);
    bool otaOk = g_otaSessionOk && Update.end(true);
    if (otaOk) {
      obdFill(&g_obd, OBD_WHITE, 1);
      if (g_otaTargetSpiffs) {
        obdWriteString(&g_obd, 0, 0, 24, (char*)"SPIFFS OK!", FONT_12x16, OBD_BLACK, 1);
      } else {
        obdWriteString(&g_obd, 0, 8, 24, (char*)"OTA OK!", FONT_12x16, OBD_BLACK, 1);
      }
    } else {
      obdFill(&g_obd, OBD_WHITE, 1);
      if (g_otaTargetSpiffs) {
        obdWriteString(&g_obd, 0, 0, 24, (char*)"SPIFFS FAIL!", FONT_12x16, OBD_BLACK, 1);
      } else {
        obdWriteString(&g_obd, 0, 0, 24, (char*)"OTA FAIL!", FONT_12x16, OBD_BLACK, 1);
      }
    }
    g_otaInProgress = false;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (!requestToken.isEmpty() && requestToken != g_otaActiveToken) {
      return;
    }
    esp_wifi_set_max_tx_power(g_otaPrevTxPower);
    Update.abort();
    g_otaSessionOk = false;
    g_otaInProgress = false;
    if (g_otaTargetSpiffs) {
      if (!g_spiffsMounted) {
        g_spiffsMounted = SPIFFS.begin(false);
      }
      clearOtaDeferredRestartState();
    }
    obdFill(&g_obd, OBD_WHITE, 1);
    obdWriteString(&g_obd, 0, 0, 24, (char*)"OTA ABORT!", FONT_12x16, OBD_BLACK, 1);
  }
}

/**
 * @brief Handle OTA completion response - called after upload finishes
 */
static void handleOta() {
  String requestToken = g_wifiServer->header("X-Ota-Token");
  if (!requestToken.isEmpty() && requestToken == g_otaRejectedToken) {
    g_otaRejectedToken = "";
    g_wifiServer->send(409, "text/plain", "Another update is already running. Wait for completion.");
    return;
  }

  bool otaOk = g_otaSessionOk && !Update.hasError();
  if (!otaOk) {
    clearOtaDeferredRestartState();
    if (g_otaTargetSpiffs && !g_spiffsMounted) {
      g_spiffsMounted = SPIFFS.begin(false);
    }
    if (g_otaTargetSpiffs) {
      g_wifiServer->send(400, "text/plain", "Error: SPIFFS update failed");
    } else {
      g_wifiServer->send(400, "text/plain", "Error: firmware update failed");
    }
  } else {
    if (!g_otaRestartRequested) {
      if (g_otaTargetSpiffs) {
        if (!g_spiffsMounted) {
          g_spiffsMounted = SPIFFS.begin(false);
        }
        g_wifiServer->send(200, "text/plain", "OK - SPIFFS updated! Restart deferred.");
      } else {
        g_otaRestartDeferredPending = true;
        g_otaRestartDeferredUntilMs = millis() + OTA_DEFERRED_RESTART_GRACE_MS;
        g_wifiServer->send(200, "text/plain", "OK - Firmware updated! Restart deferred.");
      }
    } else {
      clearOtaDeferredRestartState();
      requestWiFiAutoStartOnNextBoot();
      if (g_otaTargetSpiffs) {
        g_wifiServer->send(200, "text/plain", "OK - SPIFFS updated! Restarting...");
      } else {
        g_wifiServer->send(200, "text/plain", "OK - Firmware updated! Restarting...");
      }
      delay(1000);
      ESP.restart();
    }
  }
  if (!requestToken.isEmpty() && requestToken == g_otaActiveToken) {
    g_otaActiveToken = "";
  }
  g_otaRestartRequested = true;
}

static const uint8_t WIFI_QR_VERSION = 3;
static const uint8_t WIFI_QR_SIZE = 29;
static const uint8_t WIFI_QR_DATA_CODEWORDS = 55;
static const uint8_t WIFI_QR_ECC_CODEWORDS = 15;
static const uint8_t WIFI_QR_SCALE = 2;
static const uint8_t WIFI_QR_QUIET_ZONE = 1;

static inline bool qrGetBit(uint16_t value, uint8_t bit) {
  return ((value >> bit) & 1U) != 0;
}

static void qrSetFunctionModule(uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                                uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                                int row, int col, bool isBlack) {
  if (row < 0 || row >= WIFI_QR_SIZE || col < 0 || col >= WIFI_QR_SIZE) {
    return;
  }
  modules[row][col] = isBlack ? 1 : 0;
  functionModules[row][col] = 1;
}

static void qrReserveModule(uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                            int row, int col) {
  if (row < 0 || row >= WIFI_QR_SIZE || col < 0 || col >= WIFI_QR_SIZE) {
    return;
  }
  functionModules[row][col] = 1;
}

static void qrDrawFinderPattern(uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                                uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                                int top, int left) {
  for (int dy = -1; dy <= 7; dy++) {
    for (int dx = -1; dx <= 7; dx++) {
      int row = top + dy;
      int col = left + dx;
      if (row < 0 || row >= WIFI_QR_SIZE || col < 0 || col >= WIFI_QR_SIZE) {
        continue;
      }
      bool isBlack = (dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6 &&
                      (dx == 0 || dx == 6 || dy == 0 || dy == 6 ||
                       (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4)));
      qrSetFunctionModule(modules, functionModules, row, col, isBlack);
    }
  }
}

static void qrDrawAlignmentPattern(uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                                   uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                                   int centerRow, int centerCol) {
  for (int dy = -2; dy <= 2; dy++) {
    for (int dx = -2; dx <= 2; dx++) {
      bool isBlack = max(abs(dx), abs(dy)) != 1;
      qrSetFunctionModule(modules, functionModules, centerRow + dy, centerCol + dx, isBlack);
    }
  }
}

static void qrDrawFunctionPatterns(uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                                   uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  memset(modules, 0, WIFI_QR_SIZE * WIFI_QR_SIZE);
  memset(functionModules, 0, WIFI_QR_SIZE * WIFI_QR_SIZE);

  qrDrawFinderPattern(modules, functionModules, 0, 0);
  qrDrawFinderPattern(modules, functionModules, 0, WIFI_QR_SIZE - 7);
  qrDrawFinderPattern(modules, functionModules, WIFI_QR_SIZE - 7, 0);
  qrDrawAlignmentPattern(modules, functionModules, 22, 22);

  for (int i = 8; i < WIFI_QR_SIZE - 8; i++) {
    bool isBlack = (i % 2) == 0;
    qrSetFunctionModule(modules, functionModules, 6, i, isBlack);
    qrSetFunctionModule(modules, functionModules, i, 6, isBlack);
  }

  for (int i = 0; i < 9; i++) {
    qrReserveModule(functionModules, 8, i);
    qrReserveModule(functionModules, i, 8);
  }
  for (int i = 0; i < 8; i++) {
    qrReserveModule(functionModules, 8, WIFI_QR_SIZE - 1 - i);
  }
  for (int i = 0; i < 7; i++) {
    qrReserveModule(functionModules, WIFI_QR_SIZE - 1 - i, 8);
  }

  qrSetFunctionModule(modules, functionModules, WIFI_QR_SIZE - 8, 8, true);
}

static bool qrAppendBits(uint8_t out[WIFI_QR_DATA_CODEWORDS], int* bitLen,
                         uint32_t value, uint8_t bitCount) {
  if (bitLen == nullptr) {
    return false;
  }
  if ((*bitLen + bitCount) > (WIFI_QR_DATA_CODEWORDS * 8)) {
    return false;
  }
  for (int i = bitCount - 1; i >= 0; i--) {
    uint8_t bit = (uint8_t)((value >> i) & 1U);
    out[*bitLen >> 3] |= bit << (7 - (*bitLen & 7));
    (*bitLen)++;
  }
  return true;
}

static bool otaRequestWantsRestart() {
  if (g_wifiServer == nullptr || !g_wifiServer->hasArg("restart")) {
    return true;
  }
  String raw = g_wifiServer->arg("restart");
  raw.trim();
  raw.toLowerCase();
  return !(raw == "0" || raw == "false" || raw == "no" || raw == "off");
}

static uint8_t qrGfMultiply(uint8_t x, uint8_t y) {
  uint16_t a = x;
  uint16_t b = y;
  uint16_t z = 0;
  while (b != 0) {
    if (b & 1U) {
      z ^= a;
    }
    b >>= 1;
    a <<= 1;
    if (a & 0x100U) {
      a ^= 0x11DU;
    }
  }
  return (uint8_t)z;
}

static void qrBuildGenerator(uint8_t out[WIFI_QR_ECC_CODEWORDS]) {
  memset(out, 0, WIFI_QR_ECC_CODEWORDS);
  out[WIFI_QR_ECC_CODEWORDS - 1] = 1;
  uint8_t root = 1;
  for (uint8_t i = 0; i < WIFI_QR_ECC_CODEWORDS; i++) {
    for (uint8_t j = 0; j < WIFI_QR_ECC_CODEWORDS; j++) {
      out[j] = qrGfMultiply(out[j], root);
      if (j + 1 < WIFI_QR_ECC_CODEWORDS) {
        out[j] ^= out[j + 1];
      }
    }
    root = qrGfMultiply(root, 0x02);
  }
}

static void qrComputeRemainder(const uint8_t data[WIFI_QR_DATA_CODEWORDS],
                               const uint8_t generator[WIFI_QR_ECC_CODEWORDS],
                               uint8_t remainder[WIFI_QR_ECC_CODEWORDS]) {
  memset(remainder, 0, WIFI_QR_ECC_CODEWORDS);
  for (uint8_t i = 0; i < WIFI_QR_DATA_CODEWORDS; i++) {
    uint8_t factor = data[i] ^ remainder[0];
    for (uint8_t j = 0; j < WIFI_QR_ECC_CODEWORDS - 1; j++) {
      remainder[j] = remainder[j + 1] ^ qrGfMultiply(generator[j], factor);
    }
    remainder[WIFI_QR_ECC_CODEWORDS - 1] =
      qrGfMultiply(generator[WIFI_QR_ECC_CODEWORDS - 1], factor);
  }
}

static void qrPlaceCodewords(uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                             const uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                             const uint8_t codewords[WIFI_QR_DATA_CODEWORDS + WIFI_QR_ECC_CODEWORDS]) {
  int bitIndex = 0;
  bool upwards = true;

  for (int right = WIFI_QR_SIZE - 1; right >= 1; right -= 2) {
    if (right == 6) {
      right--;
    }
    for (int i = 0; i < WIFI_QR_SIZE; i++) {
      int row = upwards ? (WIFI_QR_SIZE - 1 - i) : i;
      for (int j = 0; j < 2; j++) {
        int col = right - j;
        if (functionModules[row][col]) {
          continue;
        }
        bool bit = false;
        if (bitIndex < ((WIFI_QR_DATA_CODEWORDS + WIFI_QR_ECC_CODEWORDS) * 8)) {
          bit = ((codewords[bitIndex >> 3] >> (7 - (bitIndex & 7))) & 1U) != 0;
        }
        modules[row][col] = bit ? 1 : 0;
        bitIndex++;
      }
    }
    upwards = !upwards;
  }
}

static bool qrMaskApplies(uint8_t mask, int row, int col) {
  switch (mask) {
    case 0: return ((row + col) % 2) == 0;
    case 1: return (row % 2) == 0;
    case 2: return (col % 3) == 0;
    case 3: return ((row + col) % 3) == 0;
    case 4: return (((row / 2) + (col / 3)) % 2) == 0;
    case 5: return (((row * col) % 2) + ((row * col) % 3)) == 0;
    case 6: return ((((row * col) % 2) + ((row * col) % 3)) % 2) == 0;
    case 7: return ((((row + col) % 2) + ((row * col) % 3)) % 2) == 0;
    default: return false;
  }
}

static void qrApplyMask(uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                        const uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                        uint8_t mask) {
  for (int row = 0; row < WIFI_QR_SIZE; row++) {
    for (int col = 0; col < WIFI_QR_SIZE; col++) {
      if (!functionModules[row][col] && qrMaskApplies(mask, row, col)) {
        modules[row][col] ^= 1;
      }
    }
  }
}

static uint16_t qrGetFormatBits(uint8_t mask) {
  uint16_t data = (1U << 3) | mask;  /* ECC level L = 01 */
  uint16_t rem = data << 10;
  for (int bit = 14; bit >= 10; bit--) {
    if (((rem >> bit) & 1U) != 0) {
      rem ^= (uint16_t)(0x537U << (bit - 10));
    }
  }
  return (uint16_t)(((data << 10) | rem) ^ 0x5412U);
}

static void qrDrawFormatBits(uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE], uint8_t mask) {
  uint16_t formatBits = qrGetFormatBits(mask);

  for (uint8_t i = 0; i <= 5; i++) {
    modules[i][8] = qrGetBit(formatBits, i) ? 1 : 0;
  }
  modules[7][8] = qrGetBit(formatBits, 6) ? 1 : 0;
  modules[8][8] = qrGetBit(formatBits, 7) ? 1 : 0;
  modules[8][7] = qrGetBit(formatBits, 8) ? 1 : 0;
  for (uint8_t i = 9; i < 15; i++) {
    modules[8][14 - i] = qrGetBit(formatBits, i) ? 1 : 0;
  }

  for (uint8_t i = 0; i < 8; i++) {
    modules[8][WIFI_QR_SIZE - 1 - i] = qrGetBit(formatBits, i) ? 1 : 0;
  }
  for (uint8_t i = 8; i < 15; i++) {
    modules[WIFI_QR_SIZE - 15 + i][8] = qrGetBit(formatBits, i) ? 1 : 0;
  }
  modules[WIFI_QR_SIZE - 8][8] = 1;
}

static int qrPenaltyRule1(const uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  int penalty = 0;

  for (int row = 0; row < WIFI_QR_SIZE; row++) {
    int runLength = 1;
    uint8_t runColor = modules[row][0];
    for (int col = 1; col < WIFI_QR_SIZE; col++) {
      if (modules[row][col] == runColor) {
        runLength++;
      } else {
        if (runLength >= 5) {
          penalty += runLength - 2;
        }
        runColor = modules[row][col];
        runLength = 1;
      }
    }
    if (runLength >= 5) {
      penalty += runLength - 2;
    }
  }

  for (int col = 0; col < WIFI_QR_SIZE; col++) {
    int runLength = 1;
    uint8_t runColor = modules[0][col];
    for (int row = 1; row < WIFI_QR_SIZE; row++) {
      if (modules[row][col] == runColor) {
        runLength++;
      } else {
        if (runLength >= 5) {
          penalty += runLength - 2;
        }
        runColor = modules[row][col];
        runLength = 1;
      }
    }
    if (runLength >= 5) {
      penalty += runLength - 2;
    }
  }

  return penalty;
}

static int qrPenaltyRule2(const uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  int penalty = 0;
  for (int row = 0; row < WIFI_QR_SIZE - 1; row++) {
    for (int col = 0; col < WIFI_QR_SIZE - 1; col++) {
      uint8_t color = modules[row][col];
      if (color == modules[row][col + 1] &&
          color == modules[row + 1][col] &&
          color == modules[row + 1][col + 1]) {
        penalty += 3;
      }
    }
  }
  return penalty;
}

static bool qrFinderPenaltyPattern(const uint8_t line[11]) {
  static const uint8_t PATTERN_A[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
  static const uint8_t PATTERN_B[11] = {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1};
  bool matchA = true;
  bool matchB = true;
  for (uint8_t i = 0; i < 11; i++) {
    if (line[i] != PATTERN_A[i]) {
      matchA = false;
    }
    if (line[i] != PATTERN_B[i]) {
      matchB = false;
    }
  }
  return matchA || matchB;
}

static int qrPenaltyRule3(const uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  int penalty = 0;
  uint8_t line[11];

  for (int row = 0; row < WIFI_QR_SIZE; row++) {
    for (int col = 0; col <= WIFI_QR_SIZE - 11; col++) {
      for (uint8_t i = 0; i < 11; i++) {
        line[i] = modules[row][col + i];
      }
      if (qrFinderPenaltyPattern(line)) {
        penalty += 40;
      }
    }
  }

  for (int col = 0; col < WIFI_QR_SIZE; col++) {
    for (int row = 0; row <= WIFI_QR_SIZE - 11; row++) {
      for (uint8_t i = 0; i < 11; i++) {
        line[i] = modules[row + i][col];
      }
      if (qrFinderPenaltyPattern(line)) {
        penalty += 40;
      }
    }
  }

  return penalty;
}

static int qrPenaltyRule4(const uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  int darkCount = 0;
  for (int row = 0; row < WIFI_QR_SIZE; row++) {
    for (int col = 0; col < WIFI_QR_SIZE; col++) {
      darkCount += modules[row][col] ? 1 : 0;
    }
  }

  const int totalModules = WIFI_QR_SIZE * WIFI_QR_SIZE;
  int k = abs(darkCount * 20 - totalModules * 10) / totalModules;
  return k * 10;
}

static int qrGetPenaltyScore(const uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  return qrPenaltyRule1(modules) +
         qrPenaltyRule2(modules) +
         qrPenaltyRule3(modules) +
         qrPenaltyRule4(modules);
}

static bool buildWiFiQrMatrix(const char* payload,
                              uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  if (payload == nullptr) {
    return false;
  }

  size_t payloadLen = strlen(payload);
  if (payloadLen == 0 || payloadLen > 53) {
    return false;
  }

  uint8_t dataCodewords[WIFI_QR_DATA_CODEWORDS];
  uint8_t generator[WIFI_QR_ECC_CODEWORDS];
  uint8_t remainder[WIFI_QR_ECC_CODEWORDS];
  uint8_t allCodewords[WIFI_QR_DATA_CODEWORDS + WIFI_QR_ECC_CODEWORDS];
  uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE];

  memset(dataCodewords, 0, sizeof(dataCodewords));
  int bitLen = 0;
  if (!qrAppendBits(dataCodewords, &bitLen, 0x4, 4)) {
    return false;
  }
  if (!qrAppendBits(dataCodewords, &bitLen, (uint32_t)payloadLen, 8)) {
    return false;
  }
  for (size_t i = 0; i < payloadLen; i++) {
    if (!qrAppendBits(dataCodewords, &bitLen, (uint8_t)payload[i], 8)) {
      return false;
    }
  }

  int capacityBits = WIFI_QR_DATA_CODEWORDS * 8;
  int terminatorBits = min(4, capacityBits - bitLen);
  if (terminatorBits > 0 && !qrAppendBits(dataCodewords, &bitLen, 0, (uint8_t)terminatorBits)) {
    return false;
  }

  int zeroPadBits = (8 - (bitLen & 7)) & 7;
  if (zeroPadBits > 0 && !qrAppendBits(dataCodewords, &bitLen, 0, (uint8_t)zeroPadBits)) {
    return false;
  }

  bool useAltPad = false;
  while (bitLen < capacityBits) {
    dataCodewords[bitLen >> 3] = useAltPad ? 0x11 : 0xEC;
    useAltPad = !useAltPad;
    bitLen += 8;
  }

  qrBuildGenerator(generator);
  qrComputeRemainder(dataCodewords, generator, remainder);

  memcpy(allCodewords, dataCodewords, WIFI_QR_DATA_CODEWORDS);
  memcpy(allCodewords + WIFI_QR_DATA_CODEWORDS, remainder, WIFI_QR_ECC_CODEWORDS);

  qrDrawFunctionPatterns(modules, functionModules);
  qrPlaceCodewords(modules, functionModules, allCodewords);

  uint8_t bestMask = 0;
  int bestPenalty = 0x7FFFFFFF;
  uint8_t trial[WIFI_QR_SIZE][WIFI_QR_SIZE];
  for (uint8_t mask = 0; mask < 8; mask++) {
    memcpy(trial, modules, sizeof(trial));
    qrApplyMask(trial, functionModules, mask);
    qrDrawFormatBits(trial, mask);
    int penalty = qrGetPenaltyScore(trial);
    if (penalty < bestPenalty) {
      bestPenalty = penalty;
      bestMask = mask;
    }
  }

  qrApplyMask(modules, functionModules, bestMask);
  qrDrawFormatBits(modules, bestMask);
  return true;
}

static bool buildWiFiQrPayload(const char* ssid, const char* password,
                               char out[64]) {
  if (ssid == nullptr || password == nullptr || out == nullptr) {
    return false;
  }
  int written = snprintf(out, 64, "WIFI:T:WPA;S:%s;P:%s;;", ssid, password);
  return written > 0 && written < 64;
}

static void drawWiFiQrMatrix(const uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  const int totalPixels = (WIFI_QR_SIZE + (2 * WIFI_QR_QUIET_ZONE)) * WIFI_QR_SCALE;
  const int originX = (OLED_WIDTH - totalPixels) / 2;
  const int originY = (OLED_HEIGHT - totalPixels) / 2;

  obdFill(&g_obd, OBD_WHITE, 1);
  for (int row = 0; row < WIFI_QR_SIZE; row++) {
    for (int col = 0; col < WIFI_QR_SIZE; col++) {
      if (!modules[row][col]) {
        continue;
      }
      int x0 = originX + (WIFI_QR_QUIET_ZONE + col) * WIFI_QR_SCALE;
      int y0 = originY + (WIFI_QR_QUIET_ZONE + row) * WIFI_QR_SCALE;
      for (uint8_t dy = 0; dy < WIFI_QR_SCALE; dy++) {
        obdDrawLine(&g_obd, x0, y0 + dy, x0 + WIFI_QR_SCALE - 1, y0 + dy, OBD_BLACK, 1);
      }
    }
  }
}

void getWiFiPortalSsid(char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  buildWifiSuffixIfNeeded();
  snprintf(out, outLen, "%s_%s", WIFI_SSID, g_wifiSuffix);
}

void getWiFiPortalHostName(char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  buildWifiHostNameIfNeeded();
  copyBoundedString(out, outLen, g_wifiHostName);
}

uint16_t getConfiguredWiFiMode() {
  loadWiFiNetworkSettingsIfNeeded();
  return g_wifiConfiguredMode;
}

void setConfiguredWiFiMode(uint16_t mode) {
  loadWiFiNetworkSettingsIfNeeded();
  g_wifiConfiguredMode = (mode == WIFI_CONFIG_HOME) ? WIFI_CONFIG_HOME : WIFI_CONFIG_AP;
}

uint16_t getActiveWiFiPortalMode() {
  return g_wifiActiveMode;
}

bool isWiFiPortalApFallbackActive() {
  return g_wifiApFallbackActive;
}

bool hasConfiguredWiFiClientCredentials() {
  return hasWiFiClientCredentials();
}

void getConfiguredWiFiClientSsid(char* out, size_t outLen) {
  loadWiFiNetworkSettingsIfNeeded();
  copyBoundedString(out, outLen, g_wifiClientSsid);
}

void getConfiguredWiFiClientPassword(char* out, size_t outLen) {
  loadWiFiNetworkSettingsIfNeeded();
  copyBoundedString(out, outLen, g_wifiClientPassword);
}

void setConfiguredWiFiClientCredentials(const char* ssid, const char* password) {
  loadWiFiNetworkSettingsIfNeeded();
  copyBoundedString(g_wifiClientSsid, sizeof(g_wifiClientSsid), ssid);
  copyBoundedString(g_wifiClientPassword, sizeof(g_wifiClientPassword), password);
}

void saveWiFiNetworkSettings() {
  writeWiFiNetworkSettingsToPrefs();
}

void resetWiFiNetworkSettings() {
  stopWiFiPortal();
  loadWiFiNetworkSettingsIfNeeded();
  g_wifiConfiguredMode = WIFI_CONFIG_AP;
  g_wifiClientSsid[0] = '\0';
  g_wifiClientPassword[0] = '\0';
  applyDefaultUiAuthCredentials();
  writeWiFiNetworkSettingsToPrefs();
}

IPAddress getWiFiPortalIP() {
  if (g_wifiActiveMode == WIFI_PORTAL_HOME) {
    return WiFi.localIP();
  }
  return WiFi.softAPIP();
}

static void registerWebRoutes() {
  if (g_wifiServer == nullptr) {
    return;
  }

  g_wifiServer->on("/", handleRoot);
  g_wifiServer->on("/index.html", HTTP_GET, handleRoot);
  g_wifiServer->on("/ui/public.html", HTTP_GET, handleRoot);
  g_wifiServer->on("/ui", HTTP_GET, handleUi);
  g_wifiServer->on("/ui/", HTTP_GET, handleUi);
  g_wifiServer->on("/ui/index.html", HTTP_GET, handleUi);
  g_wifiServer->on("/controller", HTTP_GET, handleUi);
  g_wifiServer->on("/controller/", HTTP_GET, handleUi);
  g_wifiServer->on("/controller/index.html", HTTP_GET, handleUi);
  g_wifiServer->on("/api/info", HTTP_GET, []() { if (!requireControllerAuth()) return; handleInfo(); });
  g_wifiServer->on("/api/schema", HTTP_GET, []() { if (!requireControllerAuth()) return; handleSchema(); });
  g_wifiServer->on("/api/state", HTTP_GET, []() { if (!requireControllerAuth()) return; handleState(); });
  g_wifiServer->on("/api/apply", HTTP_POST, []() { if (!requireControllerAuth()) return; handleApply(); });
  g_wifiServer->on("/api/save", HTTP_POST, []() { if (!requireControllerAuth()) return; handleSave(); });
  g_wifiServer->on("/api/telemetry/status", HTTP_GET, []() { if (!requireControllerAuth()) return; handleTelemetryStatus(); });
  g_wifiServer->on("/api/telemetry/live", HTTP_GET, []() { if (!requireControllerAuth()) return; handleTelemetryLive(); });
  g_wifiServer->on("/api/telemetry/start", HTTP_POST, []() { if (!requireControllerAuth()) return; handleTelemetryStart(); });
  g_wifiServer->on("/api/telemetry/stop", HTTP_POST, []() { if (!requireControllerAuth()) return; handleTelemetryStop(); });
  g_wifiServer->on("/api/telemetry/clear", HTTP_POST, []() { if (!requireControllerAuth()) return; handleTelemetryClear(); });
  g_wifiServer->on("/api/telemetry/export.csv", HTTP_GET, []() { if (!requireControllerAuth()) return; handleTelemetryExportCsv(); });
  g_wifiServer->on("/api/telemetry/export.json", HTTP_GET, []() { if (!requireControllerAuth()) return; handleTelemetryExportJson(); });
  g_wifiServer->on("/backup", HTTP_GET, []() { if (!requireControllerAuth()) return; handleBackup(); });
  g_wifiServer->on("/docs", HTTP_GET, handleDocsDefault);
  g_wifiServer->on("/docs/", HTTP_GET, handleDocsDefault);
  g_wifiServer->on("/docs/index.html", HTTP_GET, handleDocsDefault);
  g_wifiServer->on("/docs/en", HTTP_GET, handleDocsEn);
  g_wifiServer->on("/docs/en/", HTTP_GET, handleDocsEn);
  g_wifiServer->on("/docs/en/index.html", HTTP_GET, handleDocsEn);
  g_wifiServer->on("/docs/no", HTTP_GET, handleDocsNo);
  g_wifiServer->on("/docs/no/", HTTP_GET, handleDocsNo);
  g_wifiServer->on("/docs/no/index.html", HTTP_GET, handleDocsNo);
  g_wifiServer->on("/docs/nl", HTTP_GET, handleDocsNl);
  g_wifiServer->on("/docs/nl/", HTTP_GET, handleDocsNl);
  g_wifiServer->on("/docs/nl/index.html", HTTP_GET, handleDocsNl);
  g_wifiServer->on("/docs/pt", HTTP_GET, handleDocsPt);
  g_wifiServer->on("/docs/pt/", HTTP_GET, handleDocsPt);
  g_wifiServer->on("/docs/pt/index.html", HTTP_GET, handleDocsPt);
  g_wifiServer->on("/docs/es", HTTP_GET, handleDocsEs);
  g_wifiServer->on("/docs/es/", HTTP_GET, handleDocsEs);
  g_wifiServer->on("/docs/es/index.html", HTTP_GET, handleDocsEs);
  g_wifiServer->on("/docs/de", HTTP_GET, handleDocsDe);
  g_wifiServer->on("/docs/de/", HTTP_GET, handleDocsDe);
  g_wifiServer->on("/docs/de/index.html", HTTP_GET, handleDocsDe);
  g_wifiServer->on("/docs/it", HTTP_GET, handleDocsIt);
  g_wifiServer->on("/docs/it/", HTTP_GET, handleDocsIt);
  g_wifiServer->on("/docs/it/index.html", HTTP_GET, handleDocsIt);
  g_wifiServer->on("/docs/assets/curve_examples.png", HTTP_GET, handleDocsCurveAsset);
  g_wifiServer->on("/docs/assets/trig_cal.png", HTTP_GET, handleDocsTriggerAsset);
  g_wifiServer->on("/favicon.svg", HTTP_GET, handleFaviconSvg);
  g_wifiServer->on("/favicon.ico", HTTP_GET, []() {
    g_wifiServer->sendHeader("Location", "/favicon.svg");
    g_wifiServer->send(302, "text/plain", "");
  });
  g_wifiServer->on("/restore", HTTP_POST,
                   []() { if (!requireControllerAuth()) return; handleRestore(); },
                   []() { if (!requireControllerAuth()) return; handleRestoreUpload(); });
  g_wifiServer->on("/ota-fw", HTTP_POST,
                   []() { if (!requireControllerAuth()) return; handleOta(); },
                   []() { if (!requireControllerAuth()) return; handleOtaUpload(); });
  g_wifiServer->on("/ota-spiffs", HTTP_POST,
                   []() { if (!requireControllerAuth()) return; handleOta(); },
                   []() { if (!requireControllerAuth()) return; handleOtaUpload(); });

  /* Collect the binary size header sent by the UI so Update.begin() can
   * allocate the exact partition space needed instead of UPDATE_SIZE_UNKNOWN. */
  static const char* kOtaCollectHeaders[] = {"X-Binary-Size", "X-Ota-Token"};
  g_wifiServer->collectHeaders(kOtaCollectHeaders, 2);
}

bool isWiFiPortalActive() {
  return g_wifiServer != nullptr;
}

bool isOtaInProgress() {
  return g_otaInProgress || isOtaDeferredRestartActive();
}

static void stopWiFiTransportOnly() {
  if (g_mdnsActive) {
    MDNS.end();
    g_mdnsActive = false;
  }

  if (g_wifiActiveMode == WIFI_PORTAL_AP) {
    WiFi.softAPdisconnect(true);
  } else if (g_wifiActiveMode == WIFI_PORTAL_HOME) {
    WiFi.disconnect();
  }

  WiFi.mode(WIFI_OFF);
  g_wifiActiveMode = WIFI_PORTAL_OFF;
  g_wifiApFallbackActive = false;
  g_wifiConnectedSsid[0] = '\0';
}

static bool startWiFiApTransport() {
  char ssid[20];
  getWiFiPortalSsid(ssid, sizeof(ssid));

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid, WIFI_PASS, WIFI_AP_CHANNEL, 0, WIFI_MAX_CONNECTIONS)) {
    WiFi.mode(WIFI_OFF);
    return false;
  }
  delay(100);
  g_wifiActiveMode = WIFI_PORTAL_AP;
  return true;
}

static bool startWiFiHomeTransportAttempt() {
  WiFi.persistent(false);
  WiFi.disconnect(true, false, 250);
  WiFi.mode(WIFI_OFF);
  delay(120);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(g_wifiHostName);
  delay(50);
  /* Boost TX power before connecting — STA mode needs more power than AP mode
   * to reach a distant router. Reverts to default on WiFi.mode(WIFI_OFF). */
  esp_wifi_set_max_tx_power(84); /* 84 * 0.25 dBm = 21 dBm (maximum) */

  obdFill(&g_obd, OBD_WHITE, 1);
  obdWriteString(&g_obd, 0, centerX8x8("Connecting..."), 0, (char*)"Connecting...", FONT_8x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 2 * HEIGHT8x8, (char*)"SSID:", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 3 * HEIGHT8x8, (char*)g_wifiClientSsid, FONT_6x8, OBD_BLACK, 1);

  WiFi.begin(g_wifiClientSsid, g_wifiClientPassword);

  uint32_t startedAt = millis();
  while ((millis() - startedAt) < WIFI_STA_CONNECT_TIMEOUT_MS) {
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
      break;
    }
    if (status == WL_CONNECT_FAILED) {
      break;
    }
    delay(100);
  }

  g_wifiLastConnectStatus = WiFi.status();
  if (g_wifiLastConnectStatus != WL_CONNECTED) {
    WiFi.disconnect(true, false, 250);
    WiFi.mode(WIFI_OFF);
    delay(120);
    return false;
  }

  copyBoundedString(g_wifiConnectedSsid, sizeof(g_wifiConnectedSsid), WiFi.SSID().c_str());
  if (MDNS.begin(g_wifiHostName)) {
    g_mdnsActive = true;
  }
  g_wifiActiveMode = WIFI_PORTAL_HOME;
  return true;
}

static bool startWiFiHomeTransport() {
  if (!hasWiFiClientCredentials()) {
    return false;
  }

  buildWifiHostNameIfNeeded();

  for (uint8_t attempt = 0; attempt < 2; attempt++) {
    if (startWiFiHomeTransportAttempt()) {
      return true;
    }
    delay(200);
  }

  return false;
}

static bool startWiFiServerOnly() {
  g_spiffsMounted = SPIFFS.begin(false);

  g_wifiServer = new WebServer(80);
  if (g_wifiServer == nullptr) {
    stopWiFiTransportOnly();
    if (g_spiffsMounted) {
      SPIFFS.end();
      g_spiffsMounted = false;
    }
    return false;
  }

  registerWebRoutes();
  g_wifiServer->begin();
  return true;
}

bool startWiFiPortal() {
  if (g_wifiServer != nullptr) {
    return true;
  }

  loadWiFiNetworkSettingsIfNeeded();
  g_wifiApFallbackActive = false;
  g_wifiConnectedSsid[0] = '\0';

  bool networkReady = false;
  if (g_wifiConfiguredMode == WIFI_CONFIG_HOME && hasWiFiClientCredentials()) {
    networkReady = startWiFiHomeTransport();
  }

  if (!networkReady) {
    g_wifiApFallbackActive = (g_wifiConfiguredMode == WIFI_CONFIG_HOME);
    if (g_wifiApFallbackActive) {
      const char* reason = (g_wifiLastConnectStatus == WL_CONNECT_FAILED)
                             ? "Wrong password?"
                             : "Router unreachable";
      obdFill(&g_obd, OBD_WHITE, 1);
      obdWriteString(&g_obd, 0, centerX8x8("WiFi failed"), 0, (char*)"WiFi failed", FONT_8x8, OBD_BLACK, 1);
      obdWriteString(&g_obd, 0, 0, 2 * HEIGHT8x8, (char*)reason, FONT_6x8, OBD_BLACK, 1);
      obdWriteString(&g_obd, 0, 0, 4 * HEIGHT8x8, (char*)"Switching to AP", FONT_6x8, OBD_BLACK, 1);
      obdWriteString(&g_obd, 0, 0, 5 * HEIGHT8x8, (char*)"mode. Retrying", FONT_6x8, OBD_BLACK, 1);
      obdWriteString(&g_obd, 0, 0, 6 * HEIGHT8x8, (char*)"later.", FONT_6x8, OBD_BLACK, 1);
      delay(2500);
    }
    networkReady = startWiFiApTransport();
  }

  if (!networkReady) {
    return false;
  }

  return startWiFiServerOnly();
}

void serviceWiFiPortal() {
  if (g_wifiServer != nullptr) {
    g_wifiServer->handleClient();
  }
}

void serviceConnectivityPortal() {
  /* serviceWiFiPortal() is handled by the dedicated WiFiTask in ESPEED32.ino */
  serviceUsbSerialCommands();
  isOtaDeferredRestartActive();
  if (g_wifiRestartPending && !isOtaInProgress() &&
      (int32_t)(millis() - g_wifiRestartAtMs) >= 0) {
    g_wifiRestartPending = false;
    if (isWiFiPortalActive()) {
      stopWiFiPortal();
      startWiFiPortal();
    }
  }
}

void stopWiFiPortal() {
  if (isOtaInProgress()) {
    return;
  }

  g_wifiRestartPending = false;
  g_wifiRestartAtMs = 0;

  telemetryStopLogging();

  if (g_wifiServer != nullptr) {
    g_wifiServer->stop();
    delete g_wifiServer;
    g_wifiServer = nullptr;
  }

  stopWiFiTransportOnly();

  if (g_spiffsMounted) {
    SPIFFS.end();
    g_spiffsMounted = false;
  }
}


/**
 * @brief Shared dismissable 2-line message screen.
 */
static void showDismissableMessageScreen(const char* title, const char* line1, const char* line2) {
  obdFill(&g_obd, OBD_WHITE, 1);
  obdWriteString(&g_obd, 0, centerX8x8(title), 0, (char*)title, FONT_8x8, OBD_BLACK, 1);
  if (line1 != nullptr && line1[0] != '\0') {
    obdWriteString(&g_obd, 0, 0, 2 * HEIGHT8x8, (char*)line1, FONT_6x8, OBD_BLACK, 1);
  }
  if (line2 != nullptr && line2[0] != '\0') {
    obdWriteString(&g_obd, 0, 0, 4 * HEIGHT8x8, (char*)line2, FONT_6x8, OBD_BLACK, 1);
  }

  while (!g_rotaryEncoder.isEncoderButtonClicked()) {
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      delay(BUTTON_SHORT_PRESS_DEBOUNCE_MS);
      break;
    }
    vTaskDelay(1);
  }
  obdFill(&g_obd, OBD_WHITE, 1);
}

static void drawWrappedFont6x8Lines(uint8_t startRow, uint8_t maxRows, const char* text) {
  if (text == nullptr || text[0] == '\0' || startRow >= 8 || maxRows == 0) {
    return;
  }

  const uint8_t rowsAvailable = (maxRows < (uint8_t)(8 - startRow)) ? maxRows : (uint8_t)(8 - startRow);
  const size_t charsPerLine = 21;
  size_t textLen = strlen(text);
  for (uint8_t row = 0; row < rowsAvailable; row++) {
    size_t offset = (size_t)row * charsPerLine;
    if (offset >= textLen) {
      break;
    }
    size_t remaining = textLen - offset;
    size_t chunkLen = (remaining < charsPerLine) ? remaining : charsPerLine;
    char line[22];
    memcpy(line, text + offset, chunkLen);
    line[chunkLen] = '\0';
    obdWriteString(&g_obd, 0, 0, (startRow + row) * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
  }
}

static void drawWiFiPortalScreenPage(uint8_t pageIndex, const IPAddress& ip) {
  char line[40];
  obdFill(&g_obd, OBD_WHITE, 1);

  if (pageIndex != 0) {
    char userLine[UI_AUTH_USER_MAX_LEN + 7];
    char passLine[UI_AUTH_PASS_MAX_LEN + 7];
    const char* title = "Login 2/2";
    snprintf(userLine, sizeof(userLine), "User: %s", g_uiAuthUsername);
    snprintf(passLine, sizeof(passLine), "Pass: %s", g_uiAuthPassword);
    obdWriteString(&g_obd, 0, centerX8x8(title), 0, (char*)title, FONT_8x8, OBD_BLACK, 1);
    drawWrappedFont6x8Lines(2, 2, userLine);
    drawWrappedFont6x8Lines(4, 3, passLine);
    return;
  }

  if (g_wifiActiveMode == WIFI_PORTAL_HOME) {
    const char* shownSsid = (g_wifiConnectedSsid[0] != '\0') ? g_wifiConnectedSsid : g_wifiClientSsid;
    const char* title = "Client 1/2";
    obdWriteString(&g_obd, 0, centerX8x8(title), 0, (char*)title, FONT_8x8, OBD_BLACK, 1);
    snprintf(line, sizeof(line), "SSID: %.15s", shownSsid);
    obdWriteString(&g_obd, 0, 0, 3 * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
    obdWriteString(&g_obd, 0, 0, 6 * HEIGHT8x8, (char*)"Open in browser", FONT_6x8, OBD_BLACK, 1);
    snprintf(line, sizeof(line), "%s", ip.toString().c_str());
    obdWriteString(&g_obd, 0, 0, 7 * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
  } else {
    char ssid[20];
    const char* title = g_wifiApFallbackActive ? "Fallback 1/2" : "AP 1/2";
    getWiFiPortalSsid(ssid, sizeof(ssid));
    obdWriteString(&g_obd, 0, centerX8x8(title), 0, (char*)title, FONT_8x8, OBD_BLACK, 1);
    snprintf(line, sizeof(line), "SSID: %s", ssid);
    obdWriteString(&g_obd, 0, 0, 2 * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
    snprintf(line, sizeof(line), "Pass: %s", WIFI_PASS);
    obdWriteString(&g_obd, 0, 0, 3 * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
    obdWriteString(&g_obd, 0, 0, 6 * HEIGHT8x8, (char*)"Open in browser", FONT_6x8, OBD_BLACK, 1);
    snprintf(line, sizeof(line), "%s", ip.toString().c_str());
    obdWriteString(&g_obd, 0, 0, 7 * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
  }
}

/**
 * @brief Main WiFi info screen - called from settings menu
 * @details Starts configured WiFi transport and shows connection details on OLED.
 *          Returns when user presses encoder button or brake button.
 */
void showWiFiPortalScreen() {
  bool wasAlreadyActive = isWiFiPortalActive();

  if (!startWiFiPortal()) {
    showDismissableMessageScreen("WiFi failed", "Could not start", "Press to exit");
    return;
  }

  IPAddress ip = getWiFiPortalIP();
  uint8_t pageIndex = 0;
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, 1, false);
  resetUiEncoder(pageIndex);
  drawWiFiPortalScreenPage(pageIndex, ip);

  while (true) {
    serviceConnectivityPortal();

    if (!isOtaInProgress()) {
      if (g_rotaryEncoder.encoderChanged()) {
        uint8_t nextPage = (uint8_t)readUiEncoder();
        if (nextPage > 1) {
          nextPage = 1;
        }
        if (nextPage != pageIndex) {
          pageIndex = nextPage;
          drawWiFiPortalScreenPage(pageIndex, ip);
        }
      }

      if (g_rotaryEncoder.isEncoderButtonClicked()) {
        if (pageIndex == 0) {
          pageIndex = 1;
          resetUiEncoder(pageIndex);
          drawWiFiPortalScreenPage(pageIndex, ip);
          delay(120);
          continue;
        }
        break;
      }

      if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
        delay(BUTTON_SHORT_PRESS_DEBOUNCE_MS);
        break;
      }
    }

    vTaskDelay(1);
  }

  if (!wasAlreadyActive) {
    stopWiFiPortal();
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}

void showWiFiQrScreen() {
  bool wasAlreadyActive = isWiFiPortalActive();

  if (!startWiFiPortal()) {
    showDismissableMessageScreen("WiFi failed", "Could not start", "Press to exit");
    return;
  }

  if (g_wifiActiveMode != WIFI_PORTAL_AP) {
    if (!wasAlreadyActive) {
      stopWiFiPortal();
    }
    showDismissableMessageScreen("QR unavailable", "QR works only", "in AP mode");
    return;
  }

  char ssid[20];
  char payload[64];
  uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE];

  getWiFiPortalSsid(ssid, sizeof(ssid));
  if (!buildWiFiQrPayload(ssid, WIFI_PASS, payload) ||
      !buildWiFiQrMatrix(payload, modules)) {
    showDismissableMessageScreen("QR failed", "Could not build", "Press to exit");
    if (!wasAlreadyActive) {
      stopWiFiPortal();
    }
    return;
  }

  drawWiFiQrMatrix(modules);

  while (true) {
    serviceConnectivityPortal();

    if (!isOtaInProgress()) {
      if (g_rotaryEncoder.isEncoderButtonClicked()) {
        break;
      }

      if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
        delay(BUTTON_SHORT_PRESS_DEBOUNCE_MS);
        break;
      }
    }

    vTaskDelay(1);
  }

  if (!wasAlreadyActive) {
    stopWiFiPortal();
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}

void showWiFiHomeSetupScreen() {
  loadWiFiNetworkSettingsIfNeeded();

  char line[32];
  obdFill(&g_obd, OBD_WHITE, 1);
  const char* title = "Client setup";
  obdWriteString(&g_obd, 0, centerX8x8(title), 0, (char*)title, FONT_8x8, OBD_BLACK, 1);

  if (g_wifiClientSsid[0] != '\0') {
    snprintf(line, sizeof(line), "SSID: %.15s", g_wifiClientSsid);
  } else {
    snprintf(line, sizeof(line), "SSID: (not set)");
  }
  obdWriteString(&g_obd, 0, 0, 2 * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);

  snprintf(line, sizeof(line), "Pass: %s", g_wifiClientPassword[0] != '\0' ? "saved" : "empty");
  obdWriteString(&g_obd, 0, 0, 4 * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 6 * HEIGHT8x8, (char*)"Set via web UI", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 7 * HEIGHT8x8, (char*)"or USB serial", FONT_6x8, OBD_BLACK, 1);

  while (true) {
    serviceConnectivityPortal();

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      break;
    }

    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      delay(BUTTON_SHORT_PRESS_DEBOUNCE_MS);
      break;
    }

    vTaskDelay(1);
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}


/**
 * @brief USB backup/restore screen - called from settings menu (USB item).
 * @details Shows a mini guide on the OLED and handles BACKUP/RESTORE serial commands.
 *          No WiFi is started. Returns when user presses encoder button or brake button.
 */
void showUSBPortalScreen() {
  obdFill(&g_obd, OBD_WHITE, 1);
  const char* usbTitle = "USB mode";
  obdWriteString(&g_obd, 0, centerX8x8(usbTitle), 0,  (char*)usbTitle, FONT_8x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0,  2 * HEIGHT8x8,  (char*)"1. Connect USB cable",  FONT_6x8,  OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0,  3 * HEIGHT8x8,  (char*)"2. Open espeed32.html", FONT_6x8,  OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0,  4 * HEIGHT8x8,  (char*)"   in Chrome/Edge",     FONT_6x8,  OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0,  5 * HEIGHT8x8,  (char*)"3. Click Connect USB",  FONT_6x8,  OBD_BLACK, 1);

  static bool brakeBtnInUsb = false;
  static uint32_t lastBrakeBtnUsbTime = 0;

  while (true) {
    serviceConnectivityPortal();

    if (g_rotaryEncoder.isEncoderButtonClicked()) break;

    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      if (!brakeBtnInUsb && millis() - lastBrakeBtnUsbTime > BUTTON_SHORT_PRESS_DEBOUNCE_MS) {
        brakeBtnInUsb = true;
        lastBrakeBtnUsbTime = millis();
        while (digitalRead(BUTT_PIN) == BUTTON_PRESSED) { vTaskDelay(5); }
        break;
      }
    } else {
      brakeBtnInUsb = false;
    }

    vTaskDelay(1);
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}
