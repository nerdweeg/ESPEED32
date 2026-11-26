#include "connectivity_portal.h"
#include "connectivity_portal_internal.h"
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
#include "app_globals.h"
#include "app_init.h"
#include "menu_input.h"
#include "wifi_qr.h"

/* External references from main .ino */

/* Web server instance (heap-allocated only when active) */
WebServer* g_wifiServer = nullptr;
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
static bool g_wifiStopPending = false;
static bool g_wifiRestartAfterStopPending = false;
static uint32_t g_wifiRestartAtMs = 0;
uint16_t g_wifiConfiguredMode = WIFI_CONFIG_AP;
static uint8_t g_wifiActiveMode = WIFI_PORTAL_OFF;
static bool g_wifiApFallbackActive = false;
static bool g_wifiForceApMode = false;  /* Panic boot: ignore HOME mode until the portal is stopped */
static wl_status_t g_wifiLastConnectStatus = WL_IDLE_STATUS;
char g_wifiClientSsid[WIFI_STA_SSID_MAX_LEN + 1] = "";
char g_wifiClientPassword[WIFI_STA_PASS_MAX_LEN + 1] = "";
char g_uiAuthUsername[UI_AUTH_USER_MAX_LEN + 1] = "espeed32";
char g_uiAuthPassword[UI_AUTH_PASS_MAX_LEN + 1] = "";
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
static const size_t TELEMETRY_USB_SERIAL_LIVE_LIMIT = 96U;

static bool readMacAddress(esp_mac_type_t type, uint8_t out[6]) {
  if (out == nullptr) return false;
  return esp_read_mac(out, type) == ESP_OK;
}

static void serviceUsbSerialCommands();
static String buildInfoJson();
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

uint16_t normalizeStatusSlotForUi(uint16_t slotValue) {
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

void getBackupControllerId(char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  uint64_t mac = ESP.getEfuseMac();
  snprintf(out, outLen, "%012llX", (unsigned long long)(mac & 0xFFFFFFFFFFFFULL));
}

void getDefaultUiAuthUsername(char* out, size_t outLen) {
  copyBoundedString(out, outLen, "espeed32");
}

void getDefaultUiAuthPassword(char* out, size_t outLen) {
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

/* What the WiFi/login preference keys hold in flash, as last loaded or written.
 * saveEEPROM() calls saveWiFiNetworkSettings() on every menu confirm, so this lets
 * writeWiFiNetworkSettingsToPrefs() skip the NVS write when nothing changed. */
static struct {
  bool valid;               /* false until flash contents are known */
  bool legacyUserKey;       /* obsolete PREF_KEY_UI_AUTH_USER still present */
  uint8_t mode;
  char ssid[WIFI_STA_SSID_MAX_LEN + 1];
  char pass[WIFI_STA_PASS_MAX_LEN + 1];
  char authPass[UI_AUTH_PASS_MAX_LEN + 1];
} g_wifiPrefsPersisted = {};

void loadWiFiNetworkSettingsIfNeeded() {
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
    g_wifiPrefsPersisted.valid = true;
    g_wifiPrefsPersisted.legacyUserKey = pref.isKey(PREF_KEY_UI_AUTH_USER);
    g_wifiPrefsPersisted.mode = (uint8_t)mode;
    copyBoundedString(g_wifiPrefsPersisted.ssid, sizeof(g_wifiPrefsPersisted.ssid), g_wifiClientSsid);
    copyBoundedString(g_wifiPrefsPersisted.pass, sizeof(g_wifiPrefsPersisted.pass), g_wifiClientPassword);
    copyBoundedString(g_wifiPrefsPersisted.authPass, sizeof(g_wifiPrefsPersisted.authPass), g_uiAuthPassword);
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

  uint8_t mode = (uint8_t)((g_wifiConfiguredMode == WIFI_CONFIG_HOME) ? WIFI_CONFIG_HOME : WIFI_CONFIG_AP);
  if (g_wifiPrefsPersisted.valid && !g_wifiPrefsPersisted.legacyUserKey &&
      g_wifiPrefsPersisted.mode == mode &&
      strcmp(g_wifiPrefsPersisted.ssid, g_wifiClientSsid) == 0 &&
      strcmp(g_wifiPrefsPersisted.pass, g_wifiClientPassword) == 0 &&
      strcmp(g_wifiPrefsPersisted.authPass, g_uiAuthPassword) == 0) {
    return;  /* Flash already holds these values */
  }

  Preferences pref;
  if (!pref.begin("stored_var", false)) {
    return;
  }
  pref.putUChar(PREF_KEY_WIFI_MODE, mode);
  pref.putString(PREF_KEY_WIFI_STA_SSID, g_wifiClientSsid);
  pref.putString(PREF_KEY_WIFI_STA_PASS, g_wifiClientPassword);
  pref.remove(PREF_KEY_UI_AUTH_USER);
  pref.putString(PREF_KEY_UI_AUTH_PASS, g_uiAuthPassword);
  pref.end();

  g_wifiPrefsPersisted.valid = true;
  g_wifiPrefsPersisted.legacyUserKey = false;
  g_wifiPrefsPersisted.mode = mode;
  copyBoundedString(g_wifiPrefsPersisted.ssid, sizeof(g_wifiPrefsPersisted.ssid), g_wifiClientSsid);
  copyBoundedString(g_wifiPrefsPersisted.pass, sizeof(g_wifiPrefsPersisted.pass), g_wifiClientPassword);
  copyBoundedString(g_wifiPrefsPersisted.authPass, sizeof(g_wifiPrefsPersisted.authPass), g_uiAuthPassword);
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

void copyBoundedString(char* out, size_t outLen, const char* in) {
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

void getCurrentUiAuthUsername(char* out, size_t outLen) {
  getDefaultUiAuthUsername(out, outLen);
}

void getCurrentUiAuthPassword(char* out, size_t outLen) {
  loadWiFiNetworkSettingsIfNeeded();
  copyBoundedString(out, outLen, g_uiAuthPassword);
}

void setCurrentUiAuthCredentials(const char* username, const char* password) {
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

bool areCurrentUiAuthCredentialsDefault() {
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

bool ensureBackupSecretAvailable(bool createIfMissing) {
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

bool decryptBackupWiFiPasswordFromJson(const String& json, char* outPassword,
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

bool decryptBackupUiAuthPasswordFromJson(const String& json, char* outPassword,
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

/* OTA progress tracking */
static size_t g_otaTotal = 0;
static size_t g_otaWritten = 0;
static volatile bool g_otaInProgress = false;  /* Read by Task2 on core 1 (isOtaUploadActive) */
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
    case LANG_SWE: return "/docs/sv/index.html";
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
 *   "TEVENTS"        → "<bytecount>\n<json>"
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
    size_t limit = TELEMETRY_USB_SERIAL_LIVE_LIMIT;
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
    if (limit > TELEMETRY_USB_SERIAL_LIVE_LIMIT) limit = TELEMETRY_USB_SERIAL_LIVE_LIMIT;
    sendSerialLengthPrefixedPayload(buildTelemetryLivePayload(afterSeq, limit));

  } else if (cmd == "TEVENTS") {
    sendSerialLengthPrefixedPayload(buildTelemetryEventsPayload());

  } else if (cmd == "TSTART") {
    if (!telemetryStartLogging(&g_storedVar,
                               g_antiSpinStepMs,
                               g_encoderInvertEnabled,
                               g_adcVoltageRange_mV,
                               (uint8_t)g_carSel)) {
      const char* err = telemetryGetLastStartError();
      if (err == nullptr || err[0] == '\0') err = "Telemetry start failed";
      Serial.print("ERR:");
      Serial.println(err);
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
      !streamHtmlFromSpiffs("/docs/it/index.html") &&
      !streamHtmlFromSpiffs("/docs/sv/index.html")) {
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

static void handleDocsSv() {
  if (!streamHtmlFromSpiffs("/docs/sv/index.html")) {
    sendDocsMissing("/docs/sv/index.html");
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
      obdWriteString(&g_obd, 0, 16, 0, (char*)"FW Update", FONT_8x8, OBD_BLACK, 1);
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
        obdWriteString(&g_obd, 0, 16, 24, (char*)"FW OK!", FONT_12x16, OBD_BLACK, 1);
      }
    } else {
      obdFill(&g_obd, OBD_WHITE, 1);
      if (g_otaTargetSpiffs) {
        obdWriteString(&g_obd, 0, 0, 24, (char*)"SPIFFS FAIL!", FONT_12x16, OBD_BLACK, 1);
      } else {
        obdWriteString(&g_obd, 0, 8, 24, (char*)"FW FAIL!", FONT_12x16, OBD_BLACK, 1);
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
    g_wifiServer->send(409, "text/plain", "Firmware update already in progress. Please wait for it to complete.");
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

static bool otaRequestWantsRestart() {
  if (g_wifiServer == nullptr || !g_wifiServer->hasArg("restart")) {
    return true;
  }
  String raw = g_wifiServer->arg("restart");
  raw.trim();
  raw.toLowerCase();
  return !(raw == "0" || raw == "false" || raw == "no" || raw == "off");
}

void getWiFiPortalSsid(char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  buildWifiSuffixIfNeeded();
  snprintf(out, outLen, "%s_%s", WIFI_SSID, g_wifiSuffix);
}

void getDeviceIdSuffix(char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  buildWifiSuffixIfNeeded();
  copyBoundedString(out, outLen, g_wifiSuffix);
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

void forceWiFiPortalApMode() {
  g_wifiForceApMode = true;
}

bool hasConfiguredWiFiClientCredentials() {
  return hasWiFiClientCredentials();
}

void getConfiguredWiFiClientSsid(char* out, size_t outLen) {
  loadWiFiNetworkSettingsIfNeeded();
  copyBoundedString(out, outLen, g_wifiClientSsid);
}

void getWiFiPortalClientSsid(char* out, size_t outLen) {
  loadWiFiNetworkSettingsIfNeeded();
  copyBoundedString(out, outLen, (g_wifiConnectedSsid[0] != '\0') ? g_wifiConnectedSsid : g_wifiClientSsid);
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
  g_wifiServer->on("/api/telemetry/config", HTTP_GET, []() { if (!requireControllerAuth()) return; handleTelemetryConfig(); });
  g_wifiServer->on("/api/telemetry/live", HTTP_GET, []() { if (!requireControllerAuth()) return; handleTelemetryLive(); });
  g_wifiServer->on("/api/telemetry/events", HTTP_GET, []() { if (!requireControllerAuth()) return; handleTelemetryEvents(); });
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
  g_wifiServer->on("/docs/sv", HTTP_GET, handleDocsSv);
  g_wifiServer->on("/docs/sv/", HTTP_GET, handleDocsSv);
  g_wifiServer->on("/docs/sv/index.html", HTTP_GET, handleDocsSv);
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

bool isOtaUploadActive() {
  return g_otaInProgress;
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
  delay(120);
  g_wifiActiveMode = WIFI_PORTAL_OFF;
  g_wifiApFallbackActive = false;
  g_wifiConnectedSsid[0] = '\0';
}

static void stopWiFiPortalNow() {
  g_wifiStopPending = false;
  bool willRestart = g_wifiRestartAfterStopPending && !isOtaInProgress();

  if (g_wifiServer != nullptr) {
    if (!willRestart) telemetryStopLogging();
    g_wifiServer->stop();
    delay(50);  /* Let lwIP drain in-flight requests before tearing down WiFi */
    delete g_wifiServer;
    g_wifiServer = nullptr;
  } else if (!willRestart) {
    telemetryStopLogging();  /* Server already null — still stop logging on clean shutdown */
  }

  stopWiFiTransportOnly();

  if (g_spiffsMounted) {
    SPIFFS.end();
    g_spiffsMounted = false;
  }

  if (willRestart) {
    g_wifiRestartAfterStopPending = false;
    startWiFiPortal();
  } else {
    g_wifiRestartAfterStopPending = false;
    g_wifiForceApMode = false;
  }
}

static bool startWiFiApTransport() {
  char ssid[20];
  getWiFiPortalSsid(ssid, sizeof(ssid));

  WiFi.persistent(false);
  WiFi.disconnect(true, false, 250);
  WiFi.mode(WIFI_OFF);
  delay(120);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  esp_wifi_set_max_tx_power(84); /* Keep AP/client link as strong/stable as possible. */
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
  g_wifiStopPending = false;
  g_wifiRestartAfterStopPending = false;

  if (g_wifiServer != nullptr) {
    return true;
  }

  if (g_wifiActiveMode != WIFI_PORTAL_OFF) {
    stopWiFiTransportOnly();  /* Recover from half-torn-down WiFi state before rebuilding portal/server. */
  }

  loadWiFiNetworkSettingsIfNeeded();
  g_wifiApFallbackActive = false;
  g_wifiConnectedSsid[0] = '\0';

  bool networkReady = false;
  if (!g_wifiForceApMode && g_wifiConfiguredMode == WIFI_CONFIG_HOME && hasWiFiClientCredentials()) {
    networkReady = startWiFiHomeTransport();
  }

  if (!networkReady) {
    g_wifiApFallbackActive = (!g_wifiForceApMode && g_wifiConfiguredMode == WIFI_CONFIG_HOME);
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
  if (g_wifiStopPending) {
    stopWiFiPortalNow();
    return;
  }

  if (g_wifiServer != nullptr) {
    g_wifiServer->handleClient();
  }

  if (g_wifiStopPending) {
    stopWiFiPortalNow();
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
      g_wifiRestartAfterStopPending = true;
      stopWiFiPortal();
    } else {
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

  if (g_wifiServer != nullptr || g_wifiStopPending) {
    g_wifiStopPending = true;
    return;
  }

  stopWiFiPortalNow();
}

/**
 * @brief Shared dismissable 2-line message screen.
 */
