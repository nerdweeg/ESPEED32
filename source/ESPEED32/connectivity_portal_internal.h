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

#endif  /* CONNECTIVITY_PORTAL_INTERNAL_H_ */
