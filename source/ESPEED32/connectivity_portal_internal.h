#ifndef CONNECTIVITY_PORTAL_INTERNAL_H_
#define CONNECTIVITY_PORTAL_INTERNAL_H_

/* Shared internals of the connectivity portal (connectivity_portal*.cpp, portal_*.cpp).
 * Not for use outside the portal modules. */

#include <WebServer.h>
#include "connectivity_portal.h"
#include "slot_ESC.h"
#include "HAL.h"
#include "telemetry_logging.h"
#include "app_globals.h"

extern WebServer* g_wifiServer;  /* Heap-allocated only while the portal is active */

static const size_t UI_AUTH_USER_MAX_LEN = 31;
static const size_t UI_AUTH_PASS_MAX_LEN = 63;

/* Controller login and WiFi state (connectivity_portal.cpp) */
void getCurrentUiAuthUsername(char* out, size_t outLen);
void getCurrentUiAuthPassword(char* out, size_t outLen);
void getWiFiPortalClientSsid(char* out, size_t outLen);  /* Connected SSID, else the configured one */

/* JSON helpers (connectivity_portal.cpp) */
void appendJsonEscaped(String& out, const char* in);
uint16_t normalizeStatusSlotForUi(uint16_t slotValue);

/* Telemetry API and payloads (portal_telemetry_api.cpp) */
String buildTelemetryStatusPayload(const char* message);
String buildTelemetryLivePayload(uint32_t afterSeq, size_t limit);
String buildTelemetryEventsPayload();
String buildTelemetryConfigSnapshotJson();
void sendSerialLengthPrefixedPayload(const String& payload);
void handleTelemetryStatus();
void handleTelemetryConfig();
void handleTelemetryLive();
void handleTelemetryEvents();
void handleTelemetryStart();
void handleTelemetryStop();
void handleTelemetryClear();
void handleTelemetryExportCsv();
void handleTelemetryExportJson();

/* Shared with the settings JSON module (connectivity_portal.cpp) */
extern uint16_t g_wifiConfiguredMode;
extern char g_wifiClientSsid[WIFI_STA_SSID_MAX_LEN + 1];
extern char g_wifiClientPassword[WIFI_STA_PASS_MAX_LEN + 1];
extern char g_uiAuthUsername[UI_AUTH_USER_MAX_LEN + 1];
extern char g_uiAuthPassword[UI_AUTH_PASS_MAX_LEN + 1];
void copyBoundedString(char* out, size_t outLen, const char* in);
void loadWiFiNetworkSettingsIfNeeded();
void getDefaultUiAuthUsername(char* out, size_t outLen);
void getDefaultUiAuthPassword(char* out, size_t outLen);
void getBackupControllerId(char* out, size_t outLen);
void setCurrentUiAuthCredentials(const char* username, const char* password);
bool areCurrentUiAuthCredentialsDefault();
bool ensureBackupSecretAvailable(bool createIfMissing);
bool decryptBackupWiFiPasswordFromJson(const String& json, char* outPassword, size_t outPasswordLen, String* errorMsg);
bool decryptBackupUiAuthPasswordFromJson(const String& json, char* outPassword, size_t outPasswordLen, String* errorMsg);

/* Settings JSON (portal_settings_json.cpp) */
const char* getBackupReleaseModeName(uint16_t mode);
bool parseJsonStr(const String& json, const char* key, char* outStr, int maxLen);
uint8_t getRequestedCarIndex();
String buildJsonBackup();
String buildSchemaJson();
String buildStateJson(uint8_t carIndex);
bool parseAndValidateJson(const String& json, StoredVar_type* sv, uint16_t* antiSpinStepMs, uint16_t* antiSpinStepPct, uint16_t* antiSpinDisplayMode, uint16_t* brakeStepRaw, uint16_t* sensiStepRaw, uint16_t* advancedMenuEnabled, uint16_t* encoderInvertEnabled, uint16_t* pwmFreqMaxProfile, uint16_t* adcVoltageRangeMv, uint16_t* wifiConfiguredMode, char* wifiClientSsid, size_t wifiClientSsidLen, char* wifiClientPassword, size_t wifiClientPasswordLen, char* uiAuthUsername, size_t uiAuthUsernameLen, char* uiAuthPassword, size_t uiAuthPasswordLen, String* errorMsg, String* warningMsg = nullptr);
bool parseAndApplyWebPatch(const String& json, String* errorMsg, uint8_t* appliedCarIndex);

#endif  /* CONNECTIVITY_PORTAL_INTERNAL_H_ */
