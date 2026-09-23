#include "diagnostics_lap_stats.h"
#include <Arduino.h>
#include "HAL.h"
#include "slot_ESC.h"
#include "ui_render.h"

extern StoredVar_type g_storedVar;
extern ESC_type g_escVar;
extern portMUX_TYPE g_lapStatsMux;
extern OBDISP g_obd;
extern AiEsp32RotaryEncoder g_rotaryEncoder;
extern char msgStr[50];

extern bool consumeScreensaverWakeInput(bool wakeTriggered);
extern bool refreshIdleInteractionFromControls(uint32_t* lastInteraction, bool* screensaverActive, uint16_t* lastEncoderPos);
extern bool serviceIdlePowerTransitions(uint32_t* lastInteraction, bool* screensaverActive);
extern bool checkRaceModeEscape();
extern void requestEscapeToMain();
extern uint8_t getMainMenuSelector();
extern uint8_t getMainMenuItemsCount();

/**
 * Show the Lap Stats screen
 * Displays lap count, best time, current lap time, motor current,
 * and a scrollable list of the last 20 lap times.
 * Encoder scrolls through lap list.
 * Brake short press returns to main menu, long press resets lap stats.
 */
void showLapStats() {
  obdFill(&g_obd, OBD_WHITE, 1);

  /* Encoder for scrolling lap list (4 visible rows) */
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, 0, false);
  resetUiEncoder(0);

  int16_t scrollPos = 0;
  uint16_t prevLapCount = 0xFFFF;
  uint32_t prevBestLap = 0xFFFF;
  uint32_t prevCurrentLap = 0xFFFF;
  uint16_t prevMotorCurrent = 0xFFFF;
  int16_t prevScrollPos = -1;
  bool needFullRedraw = true;

  /* Screensaver support */
  uint32_t lastInteraction = millis();
  bool screensaverActive = false;
  uint16_t screensaverEncoderPos = (uint16_t)readUiEncoder();

  /* Title - y=0 (pixel row 0), FONT_8x8 = 8px tall */
  const char* title = "Stats";
  uint8_t titleWidth = strlen(title) * 8;
  obdWriteString(&g_obd, 0, (OLED_WIDTH - titleWidth) / 2, 0, (char *)title, FONT_8x8, OBD_WHITE, 1);

  while (true) {
    bool wakeUp = refreshIdleInteractionFromControls(&lastInteraction, &screensaverActive, &screensaverEncoderPos);
    if (wakeUp) {
      obdFill(&g_obd, OBD_WHITE, 1);
      needFullRedraw = true;
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
          obdFill(&g_obd, OBD_WHITE, 1);
          needFullRedraw = true;
        }
        delay(10);
        continue;
      }
    }

    /* Consume encoder clicks here so STATS follows the normal "brake = back" pattern. */
    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      lastInteraction = millis();
    }

    /* Brake button:
       - short press: back (exit)
       - long press: reset lap stats */
    static bool brakeInStats = false;
    static bool brakeLongHandled = false;
    static uint32_t brakePressStartMs = 0;
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      if (!brakeInStats) {
        brakeInStats = true;
        brakeLongHandled = false;
        brakePressStartMs = millis();
      }
      if (!brakeLongHandled && (millis() - brakePressStartMs > BUTTON_LONG_PRESS_MS)) {
        brakeLongHandled = true;
        portENTER_CRITICAL(&g_lapStatsMux);
        g_escVar.lapCount = 0;
        g_escVar.bestLapTime_ms = 0;
        g_escVar.lapStartTime_ms = 0;
        for (uint8_t i = 0; i < LAP_MAX_COUNT; i++) {
          g_escVar.lapTimes[i] = 0;
        }
        portEXIT_CRITICAL(&g_lapStatsMux);
        resetUiEncoder(0);
        needFullRedraw = true;
      }
    } else if (brakeInStats) {
      uint32_t pressMs = millis() - brakePressStartMs;
      bool shortPress = (!brakeLongHandled && pressMs > BUTTON_SHORT_PRESS_DEBOUNCE_MS);
      brakeInStats = false;
      brakeLongHandled = false;
      if (shortPress) {
        break;
      }
    }

    /* Update encoder scroll boundaries based on lap count */
    uint16_t totalLaps = g_escVar.lapCount;
    uint16_t displayLaps = min((uint16_t)LAP_MAX_COUNT, totalLaps);
    if (displayLaps > 4) {
      setUiEncoderBoundaries(0, displayLaps - 4, false);
    } else {
      setUiEncoderBoundaries(0, 0, false);
    }
    scrollPos = readUiEncoder();

    /* Redraw title if needed */
    if (needFullRedraw) {
      obdFill(&g_obd, OBD_WHITE, 1);
      obdWriteString(&g_obd, 0, (OLED_WIDTH - titleWidth) / 2, 0, (char *)title, FONT_8x8, OBD_WHITE, 1);
      prevLapCount = 0xFFFF;
      prevBestLap = 0xFFFF;
      prevCurrentLap = 0xFFFF;
      prevMotorCurrent = 0xFFFF;
      prevScrollPos = -1;
      needFullRedraw = false;
    }

    /* Row 1 (y=8): "Laps:XX  Best:X.XXs" (FONT_6x8, 21 chars max) */
    bool lapCountChanged = (totalLaps != prevLapCount);
    if (lapCountChanged || g_escVar.bestLapTime_ms != prevBestLap) {
      if (totalLaps > 0) {
        sprintf(msgStr, "Laps:%-3d Best:%d.%02ds",
                totalLaps,
                (int)(g_escVar.bestLapTime_ms / 1000),
                (int)((g_escVar.bestLapTime_ms % 1000) / 10));
      } else {
        sprintf(msgStr, "Laps:0              ");
      }
      obdWriteString(&g_obd, 0, 0, 8, msgStr, FONT_6x8, OBD_BLACK, 1);
      prevLapCount = totalLaps;
      prevBestLap = g_escVar.bestLapTime_ms;
    }

    /* Row 2 (y=16): "Curr:X.Xs  mA:XXXX" (FONT_6x8) */
    uint32_t currentLapTime = 0;
    if (g_escVar.lapStartTime_ms > 0) {
      currentLapTime = millis() - g_escVar.lapStartTime_ms;
    }
    uint16_t motorCurrent = g_escVar.motorCurrent_mA;
    bool hasCurrentSense = HAL_HasMotorCurrentSense();
    /* Update every ~200ms to avoid flicker */
    if ((currentLapTime / 200) != (prevCurrentLap / 200) || motorCurrent != prevMotorCurrent) {
      if (g_escVar.lapStartTime_ms > 0) {
        if (hasCurrentSense) {
          sprintf(msgStr, "Curr:%d.%ds  mA:%-4d",
                  (int)(currentLapTime / 1000),
                  (int)((currentLapTime % 1000) / 100),
                  motorCurrent);
        } else {
          sprintf(msgStr, "Curr:%d.%ds  mA:N/A ",
                  (int)(currentLapTime / 1000),
                  (int)((currentLapTime % 1000) / 100));
        }
      } else {
        if (hasCurrentSense) sprintf(msgStr, "Curr:---   mA:%-4d ", motorCurrent);
        else sprintf(msgStr, "Curr:---   mA:N/A ");
      }
      obdWriteString(&g_obd, 0, 0, 16, msgStr, FONT_6x8, OBD_BLACK, 1);
      prevCurrentLap = currentLapTime;
      prevMotorCurrent = motorCurrent;
    }

    /* Rows 3-6 (y=24,32,40,48): Scrollable lap list, newest first */
    if (scrollPos != prevScrollPos || lapCountChanged) {
      for (uint8_t row = 0; row < 4; row++) {
        uint8_t yPixel = 24 + (row * 8);
        /* Lap index: newest first, with scroll offset */
        int16_t lapIdx = (int16_t)totalLaps - 1 - scrollPos - row;
        if (lapIdx >= 0 && lapIdx < (int16_t)totalLaps) {
          uint8_t bufIdx = lapIdx % LAP_MAX_COUNT;
          uint32_t t = g_escVar.lapTimes[bufIdx];
          bool isBest = (t == g_escVar.bestLapTime_ms && t > 0);
          sprintf(msgStr, "#%-3d %d.%02ds %s       ",
                  lapIdx + 1,
                  (int)(t / 1000),
                  (int)((t % 1000) / 10),
                  isBest ? "*" : " ");
          msgStr[21] = '\0';
          obdWriteString(&g_obd, 0, 0, yPixel, msgStr, FONT_6x8, OBD_BLACK, 1);
        } else {
          obdWriteString(&g_obd, 0, 0, yPixel, (char *)"                     ", FONT_6x8, OBD_BLACK, 1);
        }
      }
      prevScrollPos = scrollPos;
    }

    /* Row 7 (y=56): Status line (throttle, car name, voltage) */
    displayStatusLine();

    /* Long press = escape to main for race mode toggle */
    if (checkRaceModeEscape()) { requestEscapeToMain(); break; }

    delay(50);  /* ~20 Hz UI refresh */
  }

  /* Restore main menu encoder settings */
  obdFill(&g_obd, OBD_WHITE, 1);
  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(1, getMainMenuItemsCount(), false);
  resetUiEncoder(getMainMenuSelector());
}
