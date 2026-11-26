#include "connectivity_portal_internal.h"
#include "menu_input.h"
#include "wifi_qr.h"

/* OLED screens for the WiFi portal (info pages, QR code, home WiFi setup) and USB info. */

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
    char user[UI_AUTH_USER_MAX_LEN + 1];
    char pass[UI_AUTH_PASS_MAX_LEN + 1];
    char userLine[UI_AUTH_USER_MAX_LEN + 7];
    char passLine[UI_AUTH_PASS_MAX_LEN + 7];
    const char* title = "Login 2/2";
    getCurrentUiAuthUsername(user, sizeof(user));
    getCurrentUiAuthPassword(pass, sizeof(pass));
    snprintf(userLine, sizeof(userLine), "User: %s", user);
    snprintf(passLine, sizeof(passLine), "Pass: %s", pass);
    obdWriteString(&g_obd, 0, centerX8x8(title), 0, (char*)title, FONT_8x8, OBD_BLACK, 1);
    drawWrappedFont6x8Lines(2, 2, userLine);
    drawWrappedFont6x8Lines(4, 3, passLine);
    return;
  }

  if (getActiveWiFiPortalMode() == WIFI_PORTAL_HOME) {
    char shownSsid[WIFI_STA_SSID_MAX_LEN + 1];
    getWiFiPortalClientSsid(shownSsid, sizeof(shownSsid));
    const char* title = "Client 1/2";
    obdWriteString(&g_obd, 0, centerX8x8(title), 0, (char*)title, FONT_8x8, OBD_BLACK, 1);
    snprintf(line, sizeof(line), "SSID: %.15s", shownSsid);
    obdWriteString(&g_obd, 0, 0, 3 * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
    obdWriteString(&g_obd, 0, 0, 6 * HEIGHT8x8, (char*)"Open in browser", FONT_6x8, OBD_BLACK, 1);
    snprintf(line, sizeof(line), "%s", ip.toString().c_str());
    obdWriteString(&g_obd, 0, 0, 7 * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
  } else {
    char ssid[20];
    const char* title = isWiFiPortalApFallbackActive() ? "Fallback 1/2" : "AP 1/2";
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

  if (getActiveWiFiPortalMode() != WIFI_PORTAL_AP) {
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
  char ssid[WIFI_STA_SSID_MAX_LEN + 1];
  char pass[WIFI_STA_PASS_MAX_LEN + 1];
  getConfiguredWiFiClientSsid(ssid, sizeof(ssid));
  getConfiguredWiFiClientPassword(pass, sizeof(pass));

  char line[32];
  obdFill(&g_obd, OBD_WHITE, 1);
  const char* title = "Client setup";
  obdWriteString(&g_obd, 0, centerX8x8(title), 0, (char*)title, FONT_8x8, OBD_BLACK, 1);

  if (ssid[0] != '\0') {
    snprintf(line, sizeof(line), "SSID: %.15s", ssid);
  } else {
    snprintf(line, sizeof(line), "SSID: (not set)");
  }
  obdWriteString(&g_obd, 0, 0, 2 * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);

  snprintf(line, sizeof(line), "Pass: %s", pass[0] != '\0' ? "saved" : "empty");
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

  static BrakeButtonEdge brakeBtnInUsb;

  while (true) {
    serviceConnectivityPortal();

    if (g_rotaryEncoder.isEncoderButtonClicked()) break;

    if (brakeBtnInUsb.pressed()) {
      waitForBrakeButtonRelease();
      break;
    }

    vTaskDelay(1);
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}
