#include "task_control_loop.h"
#include <Arduino.h>
#include "HAL.h"
#include "slot_ESC.h"
#include "connectivity_portal.h"
#include "ext_pot.h"
#include "telemetry_logging.h"

extern StateMachine_enum g_currState;
extern StoredVar_type g_storedVar;
extern ESC_type g_escVar;
extern uint16_t g_carSel;
extern uint32_t g_lastEncoderInteraction;

extern uint16_t normalizeAndClamp(uint16_t raw, uint16_t minIn, uint16_t maxIn, uint16_t normalizedMax, bool isReversed);
extern uint16_t addDeadBand(uint16_t inputVal, uint16_t minVal, uint16_t maxVal, uint16_t deadBand);
extern uint16_t throttleCurve2(uint16_t inputThrottleNorm);
extern uint16_t throttleAntiSpin3(uint16_t requestedSpeedX10);

void Task2code(void *pvParameters) {
  static unsigned long prevCallTime_uS = 0;
  static unsigned long currTrigger_raw = 0, prevTrigger_raw = 0;

  HalfBridge_Enable();

  for (;;) {
    unsigned long currCallTime_uS = micros();
    unsigned long deltaTime_uS = currCallTime_uS - prevCallTime_uS;
    
    /* Execute every ESC_PERIOD_US microseconds */
    if (deltaTime_uS > ESC_PERIOD_US) {
      prevCallTime_uS = currCallTime_uS;

      /* Read and filter trigger input */
      prevTrigger_raw = currTrigger_raw;
      currTrigger_raw = HAL_ReadTriggerRaw();
      g_escVar.trigger_raw = (prevTrigger_raw + currTrigger_raw) / 2;  /* Simple moving average filter */
      
      /* Normalize and apply deadband */
      g_escVar.trigger_norm = normalizeAndClamp(g_escVar.trigger_raw, g_storedVar.minTrigger_raw, 
                                                g_storedVar.maxTrigger_raw, THROTTLE_NORMALIZED, THROTTLE_REV);
      g_escVar.trigger_norm = addDeadBand(g_escVar.trigger_norm, 0, THROTTLE_NORMALIZED, THROTTLE_DEADBAND_NORM);
      updateExtPotRuntimeValues();
      
      /* Lap detection requires motor-load sensing.
         Builds without current sense keep lap timing disabled. */
      {
        static uint8_t lapState = 0;  /* 0=TRACKING, 1=GAP, 2=COOLDOWN */
        static uint32_t gapStartMs = 0;
        static uint32_t lapRegisteredMs = 0;
        static uint32_t driveCurrentEma_mA = 0;

        uint32_t nowMs = millis();
        uint32_t vinMv = HAL_ReadVoltageDivider(AN_VIN_DIV, RVIFBL, RVIFBH);
        bool hasCurrentSense = HAL_HasMotorCurrentSense();
        uint16_t motorCurrent_mA = hasCurrentSense ? HAL_ReadMotorCurrent() : 0;
        g_escVar.motorCurrent_mA = motorCurrent_mA;

        /* Update display voltage with 8-sample moving average to filter ADC noise */
        static uint32_t vinFilter[8] = {0};
        static uint8_t vinFilterIdx = 0;
        vinFilter[vinFilterIdx] = vinMv;
        vinFilterIdx = (vinFilterIdx + 1) % 8;
        uint32_t vinSum = 0;
        for (uint8_t f = 0; f < 8; f++) vinSum += vinFilter[f];
        g_escVar.Vin_mV = (uint16_t)(vinSum / 8);

        /* Battery monitoring is non-drive-critical and uses ADC2 on the standard profile.
           Keep the last stable reading if WiFi is active or the sample looks invalid. */
        static uint32_t batFilter[8] = {0};
        static uint8_t batFilterIdx = 0;
        static uint8_t batFilterCount = 0;
        static uint32_t lastBatterySampleMs = 0;
        if ((nowMs - lastBatterySampleMs) >= 50U) {
          lastBatterySampleMs = nowMs;
          if (!isWiFiPortalActive()) {
            uint32_t batMv = HAL_ReadVoltageDivider(AN_BAT_DIV, BAT_RVIFBL, BAT_RVIFBH);
            if (batMv >= 1000U && batMv <= 6000U) {
              batFilter[batFilterIdx] = batMv;
              if (batFilterCount < 8U) batFilterCount++;
              batFilterIdx = (uint8_t)((batFilterIdx + 1U) % 8U);
              uint32_t batSum = 0;
              for (uint8_t f = 0; f < batFilterCount; f++) batSum += batFilter[f];
              g_escVar.Bat_mV = (uint16_t)(batSum / batFilterCount);
            } else if (batFilterCount == 0U) {
              g_escVar.Bat_mV = 0;
            }
          }
        }

        uint32_t throttlePct = ((uint32_t)g_escVar.trigger_norm * 100U) / THROTTLE_NORMALIZED;
        bool throttleActive = (throttlePct >= LAP_TRIGGER_ACTIVE_PCT);
        bool inDeadSpot = false;
        bool deadSpotRecovered = false;

        if (hasCurrentSense) {
          if (throttleActive && motorCurrent_mA >= LAP_CURRENT_GAP_MIN_MA) {
            if (driveCurrentEma_mA == 0) driveCurrentEma_mA = motorCurrent_mA;
            else driveCurrentEma_mA = (driveCurrentEma_mA * 7U + motorCurrent_mA) / 8U;
          } else if (driveCurrentEma_mA > 0) {
            driveCurrentEma_mA = (driveCurrentEma_mA * 7U) / 8U;
          }

          uint32_t currentGapThreshold = driveCurrentEma_mA / 6U;      /* ~17% of running baseline */
          uint32_t currentRecoverThreshold = driveCurrentEma_mA / 3U;  /* ~33% of running baseline */
          if (currentGapThreshold < LAP_CURRENT_GAP_MIN_MA) currentGapThreshold = LAP_CURRENT_GAP_MIN_MA;
          if (currentRecoverThreshold < LAP_CURRENT_RECOVER_MIN_MA) currentRecoverThreshold = LAP_CURRENT_RECOVER_MIN_MA;

          bool currentDetectArmed = throttleActive && (driveCurrentEma_mA >= LAP_CURRENT_BASE_MIN_MA);
          inDeadSpot = currentDetectArmed && (motorCurrent_mA <= currentGapThreshold);
          deadSpotRecovered = (motorCurrent_mA >= currentRecoverThreshold);
        } else {
          driveCurrentEma_mA = 0;
          lapState = 0;
        }

        switch (lapState) {
          case 0: /* TRACKING */
            if (inDeadSpot) {
              gapStartMs = nowMs;
              lapState = 1;
            }
            break;
          case 1: /* GAP */
            if (deadSpotRecovered) {
              if ((nowMs - gapStartMs) <= LAP_GAP_MAX_MS) {
                /* Valid dead spot crossing */
                if (g_escVar.lapStartTime_ms > 0) {
                  uint32_t lapTime = nowMs - g_escVar.lapStartTime_ms;
                  uint8_t idx = g_escVar.lapCount % LAP_MAX_COUNT;
                  g_escVar.lapTimes[idx] = lapTime;
                  if (g_escVar.lapCount == 0 || lapTime < g_escVar.bestLapTime_ms)
                    g_escVar.bestLapTime_ms = lapTime;
                  g_escVar.lapCount++;
                }
                g_escVar.lapStartTime_ms = nowMs;
                lapRegisteredMs = nowMs;
                lapState = 2;
              } else {
                lapState = 0; /* Too long = crash/stop */
              }
            } else if ((nowMs - gapStartMs) > LAP_GAP_MAX_MS) {
              lapState = 0; /* Still in gap and too long - abort */
            }
            break;
          case 2: /* COOLDOWN */
            if ((nowMs - lapRegisteredMs) >= LAP_MIN_TIME_MS) {
              lapState = 0;
            }
            break;
        }
      }

      /* Apply motor control (skip if in calibration or init state) */
      if (!(g_currState == CALIBRATION || g_currState == INIT)) {
        static uint16_t prevTriggerNorm = 0;
        bool triggerReleasing = g_escVar.trigger_norm < prevTriggerNorm;
        uint16_t releaseBrakeMode = g_storedVar.carParam[g_carSel].quickBrakeEnabled;
        uint16_t releaseZone_norm = (uint32_t)g_storedVar.carParam[g_carSel].quickBrakeThreshold
                                    * THROTTLE_NORMALIZED / 100;
        bool inReleaseZone = (releaseZone_norm > 0) && (g_escVar.trigger_norm < releaseZone_norm);
        bool brakeButtonPressed = (digitalRead(BUTT_PIN) == BUTTON_PRESSED);
        bool releaseActive = false;
        uint8_t activeBrakeKind = ACTIVE_BRAKE_NONE;
        uint16_t appliedBrakeX10 = 0;

        if (g_escVar.trigger_norm == 0) {
          /* Apply brake when trigger is released */
          /* Check if brake button is pressed - use alternate brake value */
          uint16_t effectiveBrakeX10 = getEffectiveBrakeRaw();
          if (brakeButtonPressed) {
            /* Use brakeButtonReduction as alternate brake value (not a reduction) */
            effectiveBrakeX10 = g_storedVar.carParam[g_carSel].brakeButtonReduction * BRAKE_SCALE;
            activeBrakeKind = ACTIVE_BRAKE_ALT;
          } else {
            activeBrakeKind = ACTIVE_BRAKE_BASE;
          }
          HalfBridge_SetPwmDragX10(0, effectiveBrakeX10);
          g_escVar.outputSpeed_pct = 0;
          appliedBrakeX10 = constrain(effectiveBrakeX10, 0, BRAKE_MAX_VALUE);
          throttleAntiSpin3(0);  /* Keep anti-spin timer updated */
        } else {
          bool applyQuickBrake = (releaseBrakeMode == RELEASE_BRAKE_QUICK) && inReleaseZone;
          bool applyDragBrake = (releaseBrakeMode == RELEASE_BRAKE_DRAG) && triggerReleasing;
          releaseActive = applyQuickBrake || applyDragBrake;

          if (applyQuickBrake) {
            /* QUICK mode: cut output and apply brake in the release zone */
            uint16_t quickBrakeX10 = g_storedVar.carParam[g_carSel].quickBrakeStrength * BRAKE_SCALE;
            HalfBridge_SetPwmDragX10(0, quickBrakeX10);
            g_escVar.outputSpeed_pct = 0;
            activeBrakeKind = ACTIVE_BRAKE_QUICK;
            appliedBrakeX10 = constrain(quickBrakeX10, 0, BRAKE_MAX_VALUE);
            throttleAntiSpin3(0);  /* Keep anti-spin timer updated */
          } else {
            /* Apply throttle curve and anti-spin */
            uint16_t outputSpeedX10 = throttleCurve2(g_escVar.trigger_norm);
            outputSpeedX10 = throttleAntiSpin3(outputSpeedX10);
            g_escVar.outputSpeed_pct = pctX10ToWholePctRounded(outputSpeedX10);
            uint16_t dragX10 = applyDragBrake ? g_storedVar.carParam[g_carSel].quickBrakeStrength * BRAKE_SCALE : 0;
            HalfBridge_SetPwmDragX10(outputSpeedX10, dragX10);
            activeBrakeKind = applyDragBrake ? ACTIVE_BRAKE_DRAG : ACTIVE_BRAKE_NONE;
            appliedBrakeX10 = constrain(dragX10, 0, BRAKE_MAX_VALUE);
          }
          /* Update last interaction time to prevent screensaver while driving */
          g_lastEncoderInteraction = millis();
        }

        g_escVar.activeBrakeKind = activeBrakeKind;
        g_escVar.activeBrake_pct = (uint8_t)constrain((int)brakeToWholePctRounded(appliedBrakeX10), 0, 100);

        uint8_t triggerPct = (uint8_t)(((uint32_t)g_escVar.trigger_norm * 100U) / THROTTLE_NORMALIZED);
        uint8_t outputPct = (uint8_t)constrain((int)g_escVar.outputSpeed_pct, 0, 100);
        uint8_t telemetryFlags = 0;
        if (brakeButtonPressed) telemetryFlags |= TELEMETRY_FLAG_BRAKE_BUTTON;
        if (triggerReleasing) telemetryFlags |= TELEMETRY_FLAG_TRIGGER_RELEASING;
        if (inReleaseZone) telemetryFlags |= TELEMETRY_FLAG_IN_RELEASE_ZONE;
        if (releaseActive) telemetryFlags |= TELEMETRY_FLAG_RELEASE_ACTIVE;
        if (HAL_HasMotorCurrentSense()) telemetryFlags |= TELEMETRY_FLAG_CURRENT_SENSE;

        telemetryCaptureSample((uint8_t)g_carSel,
                               &g_storedVar.carParam[g_carSel],
                               triggerPct,
                               outputPct,
                               g_escVar.Vin_mV,
                               g_escVar.motorCurrent_mA,
                               appliedBrakeX10,
                               g_escVar.effectiveSensi_raw,
                               (uint8_t)releaseBrakeMode,
                               telemetryFlags);
        prevTriggerNorm = g_escVar.trigger_norm;
      } else {
        g_escVar.activeBrakeKind = ACTIVE_BRAKE_NONE;
        g_escVar.activeBrake_pct = 0;
      }
    }
  }
}
