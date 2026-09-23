#include "diagnostics_self_test.h"
#include <Arduino.h>
#include "HAL.h"
#include "slot_ESC.h"
#include "connectivity_portal.h"

extern OBDISP g_obd;
extern AiEsp32RotaryEncoder g_rotaryEncoder;
extern Preferences g_pref;

extern void resetEncoderForMainMenu();

static constexpr uint16_t SELFTEST_IDLE_CURRENT_MAX_MA = 500;  /* [mA] Idle motor current pass/fail threshold */
static constexpr uint16_t SELFTEST_VIN_MIN_MV = 2500;          /* [mV] Minimum supply voltage pass/fail threshold */

/* Draw the step header: progress line + bold step name */
static void selfTestStep(uint8_t step, uint8_t total, const char* name) {
  obdFill(&g_obd, OBD_WHITE, 1);
  char hdr[22];
  sprintf(hdr, "Self-Test   %d/%d", step, total);
  obdWriteString(&g_obd, 0, 0, 0 * HEIGHT8x8, hdr, FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 1 * HEIGHT8x8, (char*)name, FONT_8x8, OBD_BLACK, 1);
}

/* Draw PASS/FAIL at bottom-right; optionally draw "enc>" prompt bottom-left */
static void selfTestResult(bool pass, bool showPrompt = false) {
  if (showPrompt) {
    obdWriteString(&g_obd, 0, 0, 7 * HEIGHT8x8, (char*)"enc >", FONT_6x8, OBD_BLACK, 1);
  }
  obdWriteString(&g_obd, 0, 80, 7 * HEIGHT8x8,
    pass ? (char*)"  PASS" : (char*)"  FAIL", FONT_6x8, OBD_BLACK, 1);
}

/* Block until encoder button clicked or brake button pressed+released.
 * Returns true if encoder button (= confirm), false if brake (= skip/exit). */
static bool selfTestWaitEnc() {
  while (true) {
    if (g_rotaryEncoder.isEncoderButtonClicked()) return true;
    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      while (digitalRead(BUTT_PIN) == BUTTON_PRESSED) vTaskDelay(1);
      return false;
    }
    vTaskDelay(1);
  }
}

/**
 * @brief Interactive hardware self-test sequence (7 steps).
 * @details Checks: display pixels, buzzer, encoder rotation, encoder button,
 *          brake button, trigger sensor, and input voltage.
 *          Launch by holding brake button at startup, or via Settings > Test.
 */
void showSelfTest() {
  const uint8_t TOTAL = 9;
  bool results[TOTAL] = {};
  const char* testNames[TOTAL] = {
    "Display", "Buzzer ", "Enc Rot",
    "Enc Btn", "Brk Btn", "Trigger",
    "NVS    ", "Motor  ", "Voltage"
  };
  char line[22];

  /* Drain pending encoder clicks; wait for brake button release from startup hold */
  while (g_rotaryEncoder.isEncoderButtonClicked()) {}
  while (digitalRead(BUTT_PIN) == BUTTON_PRESSED) vTaskDelay(1);
  delay(80);

  /* ======== Intro screen ======== */
  obdFill(&g_obd, OBD_WHITE, 1);
  obdWriteString(&g_obd, 0, centerX8x8("Self-Test"), 0 * HEIGHT8x8, (char*)"Self-Test", FONT_8x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0,  2 * HEIGHT8x8, (char*)"9 hardware checks", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0,  4 * HEIGHT8x8, (char*)"Enc btn = PASS/next", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0,  5 * HEIGHT8x8, (char*)"Brk btn = FAIL/skip", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0,  7 * HEIGHT8x8, (char*)"enc >   (start)", FONT_6x8, OBD_BLACK, 1);
  selfTestWaitEnc();

  /* ======== Step 1: Display ======== */
  /* Phase A: all pixels ON - verify no dead/stuck-off pixels */
  obdFill(&g_obd, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 25, 3 * HEIGHT8x8, (char*)"All pixels ON", FONT_6x8, OBD_WHITE, 1);
  obdWriteString(&g_obd, 0, 16, 6 * HEIGHT8x8, (char*)"1/9 Display", FONT_6x8, OBD_WHITE, 1);
  delay(2000);
  /* Phase B: all pixels OFF - verify no stuck-on pixels */
  obdFill(&g_obd, OBD_WHITE, 1);
  obdWriteString(&g_obd, 0, 22, 3 * HEIGHT8x8, (char*)"All pixels OFF", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 16, 6 * HEIGHT8x8, (char*)"1/9 Display", FONT_6x8, OBD_BLACK, 1);
  delay(1000);
  /* Require operator confirmation, same as every other step, so a dead/
   * malfunctioning display can't silently report PASS. */
  obdWriteString(&g_obd, 0, 0, 4 * HEIGHT8x8, (char*)"Both phases OK?", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 5 * HEIGHT8x8, (char*)"Enc btn = yes", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 6 * HEIGHT8x8, (char*)"Brk btn = no", FONT_6x8, OBD_BLACK, 1);
  while (g_rotaryEncoder.isEncoderButtonClicked()) {}
  results[0] = selfTestWaitEnc();
  selfTestResult(results[0]);
  delay(1000);

  /* ======== Step 2: Buzzer ======== */
  selfTestStep(2, TOTAL, "Buzzer");
  obdWriteString(&g_obd, 0, 0, 3 * HEIGHT8x8, (char*)"Listen for tone.", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 5 * HEIGHT8x8, (char*)"Enc btn = heard it", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 6 * HEIGHT8x8, (char*)"Brk btn = nothing", FONT_6x8, OBD_BLACK, 1);
  sound(NOTE_A, 250);
  delay(80);
  sound(NOTE_C, 200);
  delay(100);
  while (g_rotaryEncoder.isEncoderButtonClicked()) {}
  results[1] = selfTestWaitEnc();
  selfTestResult(results[1]);
  delay(1000);

  /* ======== Step 3: Encoder Rotate ======== */
  g_rotaryEncoder.setAcceleration(0);
  setUiEncoderBoundaries(-50, 50, false);
  resetUiEncoder(0);
  while (g_rotaryEncoder.isEncoderButtonClicked()) {}
  selfTestStep(3, TOTAL, "Enc Rotate");
  obdWriteString(&g_obd, 0, 0, 3 * HEIGHT8x8, (char*)"Rotate encoder...", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 4 * HEIGHT8x8, (char*)"(auto-pass at 3)", FONT_6x8, OBD_BLACK, 1);
  int16_t encPeak = 0;
  while (abs(encPeak) < 3) {
    int16_t pos = (int16_t)readUiEncoder();
    if (abs(pos) > abs(encPeak)) encPeak = pos;
    sprintf(line, "Clicks: %+d      ", pos);
    obdWriteString(&g_obd, 0, 0, 6 * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
    vTaskDelay(10);
  }
  results[2] = true;
  selfTestResult(true);
  delay(1500);

  /* ======== Step 4: Encoder Button ======== */
  while (g_rotaryEncoder.isEncoderButtonClicked()) {}
  selfTestStep(4, TOTAL, "Enc Button");
  obdWriteString(&g_obd, 0, 0, 3 * HEIGHT8x8, (char*)"Press encoder btn.", FONT_6x8, OBD_BLACK, 1);
  while (!g_rotaryEncoder.isEncoderButtonClicked()) vTaskDelay(1);
  results[3] = true;
  selfTestResult(true);
  delay(1500);

  /* ======== Step 5: Brake Button ======== */
  while (digitalRead(BUTT_PIN) == BUTTON_PRESSED) vTaskDelay(1);
  selfTestStep(5, TOTAL, "Brk Button");
  obdWriteString(&g_obd, 0, 0, 3 * HEIGHT8x8, (char*)"Press brake btn.", FONT_6x8, OBD_BLACK, 1);
  while (digitalRead(BUTT_PIN) != BUTTON_PRESSED) vTaskDelay(1);
  while (digitalRead(BUTT_PIN) == BUTTON_PRESSED) vTaskDelay(1);
  results[4] = true;
  selfTestResult(true);
  delay(1500);

  /* ======== Step 6: Trigger / Magnetic Sensor ======== */
  selfTestStep(6, TOTAL, "Trigger");
  obdWriteString(&g_obd, 0, 0, 3 * HEIGHT8x8, (char*)"Pull trigger fully.", FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 4 * HEIGHT8x8, (char*)"Hold 1s, release.", FONT_6x8, OBD_BLACK, 1);
  int16_t trigBase = HAL_ReadTriggerRaw();
  int16_t trigPeak = 0;
  uint32_t trigStart_ms = millis();
  while (millis() - trigStart_ms < 15000) {
    int16_t raw = HAL_ReadTriggerRaw();
    int16_t delta = abs(raw - trigBase);
    if (delta > trigPeak) trigPeak = delta;
    uint8_t secLeft = (uint8_t)((15000 - (millis() - trigStart_ms)) / 1000);
    sprintf(line, "d:%4d  %2ds left ", trigPeak, secLeft);
    obdWriteString(&g_obd, 0, 0, 6 * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
    if (trigPeak > 200) { results[5] = true; break; }
    vTaskDelay(20);
  }
  selfTestResult(results[5], !results[5]);
  if (results[5]) {
    delay(1500);
  } else {
    selfTestWaitEnc();
  }

  /* ======== Step 7: NVS / Flash ======== */
  selfTestStep(7, TOTAL, "NVS");
  obdWriteString(&g_obd, 0, 0, 3 * HEIGHT8x8, (char*)"Write-read check...", FONT_6x8, OBD_BLACK, 1);
  {
    const uint32_t MAGIC = 0xC0FFEE42u;
    g_pref.begin("selftest", false);
    g_pref.putUInt("magic", MAGIC);
    uint32_t rb = g_pref.getUInt("magic", 0);
    g_pref.clear();
    g_pref.end();
    results[6] = (rb == MAGIC);
  }
  selfTestResult(results[6]);
  delay(1500);

  /* ======== Step 8: Motor Controller (idle current sense) ======== */
  selfTestStep(8, TOTAL, "Motor ctrl");
  bool hasCurrentSense = HAL_HasMotorCurrentSense();
#if CURRENT_SENSE_PROFILE == CURRENT_SENSE_PROFILE_BTS7960
  const char* motorCtrlSubLine = hasCurrentSense ? "(current profile only)" : "(current sense N/A)";
#else
  const char* motorCtrlSubLine = hasCurrentSense ? "(no PWM applied)" : "(current sense N/A)";
#endif
  obdWriteString(&g_obd, 0, 0, 3 * HEIGHT8x8,
    hasCurrentSense ? (char*)"Idle current check" : (char*)"Driver diag check",
    FONT_6x8, OBD_BLACK, 1);
  obdWriteString(&g_obd, 0, 0, 4 * HEIGHT8x8,
    (char*)motorCtrlSubLine,
    FONT_6x8, OBD_BLACK, 1);
  uint16_t idleCurrent_mA = hasCurrentSense ? HAL_ReadMotorCurrent() : 0;
#if CURRENT_SENSE_PROFILE == CURRENT_SENSE_PROFILE_BTN99X0
  bool motorDiagOk = (HalfBridge_GetDiagnosis() == 0);  /* 0 = NO_ERROR */
  results[7] = hasCurrentSense ? ((idleCurrent_mA < SELFTEST_IDLE_CURRENT_MAX_MA) && motorDiagOk) : motorDiagOk;
#elif CURRENT_SENSE_PROFILE == CURRENT_SENSE_PROFILE_BTS7960
  results[7] = hasCurrentSense ? (idleCurrent_mA < SELFTEST_IDLE_CURRENT_MAX_MA) : true;
#else
  bool motorDiagOk = (HalfBridge_GetDiagnosis() == 0);  /* 0 = NO_ERROR */
  results[7] = hasCurrentSense ? (idleCurrent_mA < SELFTEST_IDLE_CURRENT_MAX_MA) : motorDiagOk;
#endif
  if (hasCurrentSense) sprintf(line, "Idle: %d mA", idleCurrent_mA);
  else sprintf(line, "Idle: skipped");
  obdWriteString(&g_obd, 0, 0, 6 * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
  selfTestResult(results[7]);
  delay(3000);

  /* ======== Step 9: Voltage ======== */
  selfTestStep(9, TOTAL, "Voltage");
  uint16_t vin_mV = HAL_ReadVoltageDivider(AN_VIN_DIV, RVIFBL, RVIFBH);
  results[8] = vin_mV > SELFTEST_VIN_MIN_MV;
  sprintf(line, "%d mV", vin_mV);
  obdWriteString(&g_obd, 0, 0, 3 * HEIGHT8x8, line, FONT_8x8, OBD_BLACK, 1);
  sprintf(line, results[8] ? "OK  (>%dmV)" : "LOW (<%dmV)", SELFTEST_VIN_MIN_MV);
  obdWriteString(&g_obd, 0, 0, 5 * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
  selfTestResult(results[8], true);
  selfTestWaitEnc();

  /* ======== Summary: two pages (5 + 4 tests) ======== */
  uint8_t passCount = 0;
  for (uint8_t i = 0; i < TOTAL; i++) if (results[i]) passCount++;

  g_rotaryEncoder.setAcceleration(MENU_ACCELERATION);
  setUiEncoderBoundaries(0, 1, false);
  resetUiEncoder(0);

  uint8_t summaryPage = 0;
  auto drawSummary = [&](uint8_t page) {
    obdFill(&g_obd, OBD_WHITE, 1);
    sprintf(line, "Summary %d/%d  (%d/2)", passCount, TOTAL, (int)(page + 1));
    obdWriteString(&g_obd, 0, 0, 0, line, FONT_6x8, OBD_BLACK, 1);

    if (page == 0) {
      for (uint8_t i = 0; i < 5; i++) {
        sprintf(line, "%-7s  %s", testNames[i], results[i] ? "PASS" : "FAIL");
        obdWriteString(&g_obd, 0, 0, (i + 1) * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
      }
      obdWriteString(&g_obd, 0, 0, 7 * HEIGHT8x8, (char*)"turn=page enc>=next", FONT_6x8, OBD_BLACK, 1);
    } else {
      for (uint8_t i = 5; i < TOTAL; i++) {
        sprintf(line, "%-7s  %s", testNames[i], results[i] ? "PASS" : "FAIL");
        obdWriteString(&g_obd, 0, 0, (i - 4) * HEIGHT8x8, line, FONT_6x8, OBD_BLACK, 1);
      }
      obdWriteString(&g_obd, 0, 0, 7 * HEIGHT8x8, (char*)"turn=page enc>=exit", FONT_6x8, OBD_BLACK, 1);
    }
  };

  drawSummary(summaryPage);
  bool summaryExit = false;
  while (!summaryExit) {
    if (g_rotaryEncoder.encoderChanged()) {
      uint8_t newPage = (uint8_t)readUiEncoder();
      if (newPage != summaryPage) {
        summaryPage = newPage;
        drawSummary(summaryPage);
      }
    }

    if (g_rotaryEncoder.isEncoderButtonClicked()) {
      if (summaryPage < 1) {
        summaryPage++;
        resetUiEncoder(summaryPage);
        drawSummary(summaryPage);
      } else {
        summaryExit = true;
      }
    }

    if (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
      while (digitalRead(BUTT_PIN) == BUTTON_PRESSED) vTaskDelay(1);
      summaryExit = true;
    }

    vTaskDelay(1);
  }

  /* Restore encoder for main menu */
  resetEncoderForMainMenu();
  obdFill(&g_obd, OBD_WHITE, 1);
}
