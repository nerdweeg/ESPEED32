#include "settings_about_screen.h"
#include <Arduino.h>
#include <esp_mac.h>
#include "slot_ESC.h"
#include "connectivity_portal.h"
#include "app_globals.h"
#include "power_ui.h"
#include "ui_render.h"

/**
 * Show scrollable device information screen.
 * Includes firmware/data versions, chip details, WiFi/BT MAC addresses and build info.
 */
void showAboutScreen() {
  static const uint8_t ABOUT_MAX_LINES = 40;
  static const uint8_t ABOUT_VISIBLE_LINES = 7;  /* rows below title */
  char lines[ABOUT_MAX_LINES][22];
  uint8_t lineCount = 0;
  char line[40];
  char spiffsRelease[16];

  auto addLine = [&](const char* txt) {
    if (lineCount >= ABOUT_MAX_LINES || txt == nullptr) return;
    strncpy(lines[lineCount], txt, 21);
    lines[lineCount][21] = '\0';
    lineCount++;
  };

  auto addField = [&](const char* label, const char* value) {
    if (label == nullptr) return;
    char valueCopy[40];
    char inlineLine[40];
    if (value == nullptr || value[0] == '\0') {
      strncpy(valueCopy, "-", sizeof(valueCopy) - 1);
    } else {
      strncpy(valueCopy, value, sizeof(valueCopy) - 1);
    }
    valueCopy[sizeof(valueCopy) - 1] = '\0';
    snprintf(inlineLine, sizeof(inlineLine), "%s: %s", label, valueCopy);

    /* Compact layout for short values; 2-line layout for long values. */
    if (strlen(inlineLine) <= 21) {
      addLine(inlineLine);
    } else {
      snprintf(line, sizeof(line), "%s:", label);
      addLine(line);
      snprintf(line, sizeof(line), "  %s", valueCopy);  /* keep visual spacing from label */
      addLine(line);
    }
  };

  uint8_t wifiMac[6] = {0};
  uint8_t btMac[6] = {0};
  bool wifiOk = (esp_read_mac(wifiMac, ESP_MAC_WIFI_STA) == ESP_OK);
  bool btOk = (esp_read_mac(btMac, ESP_MAC_BT) == ESP_OK);

  snprintf(line, sizeof(line), "v%d.%d", SW_MAJOR_VERSION, SW_MINOR_VERSION);
  addField("Firmware", line);
  snprintf(line, sizeof(line), "v%d", STORED_VAR_VERSION);
  addField("Data", line);
  spiffsRelease[0] = '\0';
  if (getSpiffsReleaseString(spiffsRelease, sizeof(spiffsRelease))) {
    addField("FS/UI", spiffsRelease);
  } else {
    addField("FS/UI", "-");
  }
  HAL_GetTriggerSensorInfo(line, sizeof(line));
  addField("HAL sensor", line);
  if (wifiOk) {
    snprintf(line, sizeof(line), "%02X%02X", wifiMac[4], wifiMac[5]);
  } else {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(line, sizeof(line), "%02X%02X", (uint8_t)(mac >> 8), (uint8_t)(mac));
  }
  addField("Device ID", line);

  String chipModel = String(ESP.getChipModel());
  snprintf(line, sizeof(line), "%s r%d c%d %uMB",
           chipModel.c_str(),
           ESP.getChipRevision(),
           ESP.getChipCores(),
           (unsigned int)(ESP.getFlashChipSize() / (1024UL * 1024UL)));
  addField("Chip", line);

  if (wifiOk) {
    snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X",
             wifiMac[0], wifiMac[1], wifiMac[2], wifiMac[3], wifiMac[4], wifiMac[5]);
    addField("WiFi MAC", line);
  } else {
    addField("WiFi MAC", "unavailable");
  }

  if (btOk) {
    snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X",
             btMac[0], btMac[1], btMac[2], btMac[3], btMac[4], btMac[5]);
    addField("BT MAC", line);
  } else {
    addField("BT MAC", "unavailable");
  }

  snprintf(line, sizeof(line), "%s %s", __DATE__, __TIME__);
  addField("Build", line);

  uint16_t pageCount = (lineCount == 0) ? 1 : ((lineCount + ABOUT_VISIBLE_LINES - 1) / ABOUT_VISIBLE_LINES);
  uint16_t maxPage = (pageCount > 0) ? (pageCount - 1) : 0;
  uint16_t page = 0;

  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, maxPage, false);
  resetUiEncoder(0);

  auto drawAbout = [&]() {
    obdFill(&g_obd, OBD_WHITE, 1);
    obdWriteString(&g_obd, 0, centerX8x8("About"), 0, (char*)"About", FONT_8x8, OBD_BLACK, 1);

    if (maxPage > 0) {
      snprintf(line, sizeof(line), "%u/%u", (unsigned int)(page + 1), (unsigned int)(maxPage + 1));
      int sx = OLED_WIDTH - ((int)strlen(line) * 6);
      if (sx < 0) sx = 0;
      obdWriteString(&g_obd, 0, sx, 0, line, FONT_6x8, OBD_BLACK, 1);
    }

    uint16_t pageStart = page * ABOUT_VISIBLE_LINES;
    for (uint8_t i = 0; i < ABOUT_VISIBLE_LINES; i++) {
      uint16_t idx = pageStart + i;
      if (idx >= lineCount) break;
      obdWriteString(&g_obd, 0, 0, (i + 1) * HEIGHT8x8, lines[idx], FONT_6x8, OBD_BLACK, 1);
    }
  };

  drawAbout();

  /* Easter egg */
  delay(400);
  sound(NOTE_A, 100); delay(80);
  sound(NOTE_A, 350); delay(80);
  sound(NOTE_A, 100);

  bool aboutBtnHeld = false;
  uint32_t lastInteraction = millis();
  bool screensaverActive = false;
  uint16_t screensaverEncoderPos = (uint16_t)readUiEncoder();

  while (true) {
    bool wakeUp = refreshIdleInteractionFromControls(&lastInteraction, &screensaverActive, &screensaverEncoderPos);
    if (wakeUp) {
      drawAbout();
    }

    if (consumeScreensaverWakeInput(wakeUp)) { continue; }

    if (!wakeUp && g_storedVar.screensaverTimeout > 0 &&
        millis() - lastInteraction > (g_storedVar.screensaverTimeout * 1000UL)) {
      if (g_escVar.trigger_norm == 0) {
        if (!screensaverActive) {
          screensaverActive = true;
          screensaverEncoderPos = readUiEncoder();
          showScreensaver();
        }
        if (serviceIdlePowerTransitions(&lastInteraction, &screensaverActive)) {
          drawAbout();
        }
        delay(10);
        continue;
      }
    }

    if (!wakeUp && screensaverActive) {
      if (serviceIdlePowerTransitions(&lastInteraction, &screensaverActive)) {
        drawAbout();
      }
      delay(10);
      continue;
    }

    if (g_rotaryEncoder.encoderChanged()) {
      lastInteraction = millis();
      uint16_t newPage = readUiEncoder();
      if (newPage != page) {
        page = newPage;
        drawAbout();
      }
    }

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
      if (maxPage > 0 && page < maxPage) {
        page++;
        resetUiEncoder(page);
        drawAbout();
      } else {
        break;
      }
    }

    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      lastInteraction = millis();
      if (!aboutBtnHeld) {
        aboutBtnHeld = true;
      }
    } else {
      if (aboutBtnHeld) break;
    }

    vTaskDelay(1);
  }

  obdFill(&g_obd, OBD_WHITE, 1);
}
