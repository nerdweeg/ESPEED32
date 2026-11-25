#ifndef CONNECTIVITY_PORTAL_H_
#define CONNECTIVITY_PORTAL_H_

#include <WiFi.h>
#include <WebServer.h>

/* WiFi AP Configuration */
#define WIFI_SSID           "ESPEED32"
#define WIFI_PASS           "espeed32"
#define WIFI_AP_CHANNEL     1
#define WIFI_MAX_CONNECTIONS 1
#define WIFI_STA_SSID_MAX_LEN 32
#define WIFI_STA_PASS_MAX_LEN 63

/* Configured WiFi behavior */
#define WIFI_CONFIG_AP      0
#define WIFI_CONFIG_HOME    1

/* Active WiFi transport */
#define WIFI_PORTAL_OFF     0
#define WIFI_PORTAL_AP      1
#define WIFI_PORTAL_HOME    2

/* Function prototypes - called from settings menu */
void showWiFiPortalScreen();
void showWiFiQrScreen();
void showWiFiHomeSetupScreen();
void showUSBPortalScreen();

/* Non-blocking WiFi portal controls (used by timed/background mode) */
bool startWiFiPortal();
void stopWiFiPortal();
void serviceWiFiPortal();
void serviceConnectivityPortal();
bool isWiFiPortalActive();
bool isOtaInProgress();
bool isOtaUploadActive();  /* Side-effect free; safe to poll from Task2 */
IPAddress getWiFiPortalIP();
void getWiFiPortalSsid(char* out, size_t outLen);
void getWiFiPortalHostName(char* out, size_t outLen);
void getDeviceIdSuffix(char* out, size_t outLen);
uint16_t getConfiguredWiFiMode();
void setConfiguredWiFiMode(uint16_t mode);
uint16_t getActiveWiFiPortalMode();
bool isWiFiPortalApFallbackActive();
void forceWiFiPortalApMode();
bool hasConfiguredWiFiClientCredentials();
void getConfiguredWiFiClientSsid(char* out, size_t outLen);
void getConfiguredWiFiClientPassword(char* out, size_t outLen);
void setConfiguredWiFiClientCredentials(const char* ssid, const char* password);
void saveWiFiNetworkSettings();
void resetWiFiNetworkSettings();
void requestWiFiAutoStartOnNextBoot();
bool consumeWiFiAutoStartOnNextBootRequest();
bool getSpiffsReleaseString(char* out, size_t outLen);

/* Shared OLED helper for centered 8x8 titles */
int centerX8x8(const char* text);

#endif
