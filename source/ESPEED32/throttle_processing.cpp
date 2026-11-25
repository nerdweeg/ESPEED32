#include "throttle_processing.h"
#include "app_globals.h"
#include "ext_pot.h"

/* Trigger-to-output processing used by Task2 every control period:
 * normalization, deadband, throttle curve and anti-spin ramp. */

#define ANTIS_SPEED_START_MIN 30  /* [%] Start anti-spin only for throttle requests above this threshold */
#define ANTIS_SPEED_START_MAX 65  /* [%] Maximum anti-spin start threshold */

/**
 * @brief Apply anti-spin control to prevent car drift
 * @details Applies a ramp to output speed to prevent sudden speed changes that cause wheel spin.
 *          Anti-spin time is the ramp time from the anti-spin start point up toward maxSpeed.
 *          It is not a pure trigger delay.
 *          Anti-spin is bypassed at low duty cycles (< antispinPercStart) where current is too low to cause spinning.
 *          The antispinPercStart threshold varies with the antiSpin setting:
 *          - High antiSpin (200ms) = Low threshold (powerful motor/slippery track)
 *          - Low antiSpin = High threshold (good traction)
 * 
 * @param requestedSpeedX10 [0.1%] Requested output speed (0-1000)
 * @return [0.1%] Actual output speed respecting anti-spin ramp limits
 */
uint16_t throttleAntiSpin3(uint16_t requestedSpeedX10) {
  static uint32_t lastOutputSpeedx1000 = 0;
  static unsigned long prevCall_uS = 0;
  
  unsigned long currCall_uS = micros();
  unsigned long deltaTime_uS = currCall_uS - prevCall_uS;
  prevCall_uS = currCall_uS;

  /* Calculate dynamic anti-spin start threshold based on anti-spin setting */
  uint16_t antispinPercStart = map((long)g_storedVar.carParam[g_carSel].antiSpin, 0, (long)ANTISPIN_MAX_VALUE, 
                                    (long)ANTIS_SPEED_START_MAX, (long)ANTIS_SPEED_START_MIN);
  uint16_t antispinPercStartRaw = antispinPercStart * SENSI_SCALE;

  /* Bypass anti-spin if disabled */
  if (g_storedVar.carParam[g_carSel].antiSpin == 0) {
    lastOutputSpeedx1000 = (uint32_t)getEffectiveSensiRaw() * 100U;
    return requestedSpeedX10;
  }

  /* Bypass anti-spin at low speeds (insufficient current for wheel spin) */
  if (requestedSpeedX10 < antispinPercStartRaw) {
    lastOutputSpeedx1000 = (uint32_t)requestedSpeedX10 * 100U;
    return requestedSpeedX10;
  }

  /* Allow immediate deceleration (braking) */
  if ((uint32_t)requestedSpeedX10 * 100U <= lastOutputSpeedx1000) {
    lastOutputSpeedx1000 = (uint32_t)requestedSpeedX10 * 100U;
    return requestedSpeedX10;
  }

  /* Apply anti-spin ramp for acceleration */
  uint16_t minSpeedTmpRaw = max(getEffectiveSensiRaw(), antispinPercStartRaw);
  uint16_t maxSpeedRaw = g_storedVar.carParam[g_carSel].maxSpeed * SENSI_SCALE;
  if (maxSpeedRaw <= minSpeedTmpRaw) {
    lastOutputSpeedx1000 = (uint32_t)requestedSpeedX10 * 100U;
    return requestedSpeedX10;
  }
  
  /* Convert the 0.1%-unit span into x1000-percent ramp units.
   * deltaTime is in us while antiSpin is in ms, so denominator is antiSpin * scale. */
  uint32_t maxDeltaSpeedx1000 = ((uint32_t)(maxSpeedRaw - minSpeedTmpRaw) * deltaTime_uS) /
                                 ((uint32_t)g_storedVar.carParam[g_carSel].antiSpin * (uint32_t)SENSI_SCALE);

  uint32_t outputSpeedX1000;
  uint32_t targetSpeedX1000 = (uint32_t)requestedSpeedX10 * 100U;
  
  /* Check if we can increase speed by full delta or if we're close to target */
  if (maxDeltaSpeedx1000 < (targetSpeedX1000 - lastOutputSpeedx1000)) {
    outputSpeedX1000 = lastOutputSpeedx1000 + maxDeltaSpeedx1000;
  } else {
    outputSpeedX1000 = targetSpeedX1000;  /* Target reached */
  }

  /* Ensure ramp starts from minSpeed, not zero */
  if (outputSpeedX1000 < (uint32_t)getEffectiveSensiRaw() * 100U) {
    outputSpeedX1000 = (uint32_t)getEffectiveSensiRaw() * 100U;
  }

  lastOutputSpeedx1000 = outputSpeedX1000;
  return (uint16_t)((outputSpeedX1000 + 50U) / 100U);
}


/**
 * Accounts for a deadband in an input value.
 * 
 * @param inputVal The input value
 * @param minVal The lower bound of the input value range
 * @param maxVal The upper bound of the input value range
 * @param deadBand The deadband as absolute value (not a percentage)
 * @return If the input value is less than the minVal + deadBand, it returns minVal.
           If the input value is more than the maxVal - deadBand, it returns the maxVal.
           Otherwise, it returns the inputVal, scaled in order to range from minVal to maxVal.
 */
uint16_t addDeadBand(uint16_t inputVal, uint16_t minVal, uint16_t maxVal, uint16_t deadBand) 
{
  uint16_t retVal = 0;

  /* If the inputVal is less than deadBand_pct, return 0 */
  if (inputVal < minVal + deadBand) 
  {
    retVal = 0;
  } 
  /* If the inputVal is more than maxValue - deadBand, return maxValue */
  else if (inputVal > maxVal - deadBand) 
  {
    retVal = maxVal;
  } 
  else 
  {
    retVal = map(inputVal, deadBand, maxVal - deadBand, minVal, maxVal);  /* Scale the inputValue (which ranges from (minVal + deadBand) to (maxVal - deadBand))
                                                                             so that it ranges from minVal to maxVal */
  }

  return retVal;
}

/**
 * throttleCurve2: Map trigger position(throttle) to speed (duty) on a broken line curve, with midpoint set as throttleCurveVertex
 * dual throttle curve: When decelerating , if dragB is higher than 100%-minSpeed, then set a lower minSpeed
 * @param inputThrottleNorm The input Trigger value, normalized between 0 and THROTTLE_NORMALIZED
 * @return duty cycle in 0.1% units to be applied at that trigger position on the selected curve
 */
uint16_t throttleCurve2(uint16_t inputThrottleNorm )
{
  uint32_t throttleCurveVertexSpeedRaw;  /* Output speed in 0.1% units at the curve vertex */
  uint16_t outputSpeedRaw = 0;           /* Output speed in 0.1% units */
  uint16_t tmpMinSpeedRaw;               /* Minimum speed in 0.1% units */
  uint16_t maxSpeedRaw;                  /* Maximum speed in 0.1% units */
  uint16_t fadeThrottleNorm;
  uint16_t curveVertexInputNorm;

  /* dual throttle curve: When decelerating , if dragB is higher than 100%-minSpeed, then set a lower minSpeed on the curve to allow the desired drag brake to be applied*/
  tmpMinSpeedRaw = getEffectiveSensiRaw();
  maxSpeedRaw = g_storedVar.carParam[g_carSel].maxSpeed * SENSI_SCALE;
  if (maxSpeedRaw < tmpMinSpeedRaw) {
    maxSpeedRaw = tmpMinSpeedRaw;
  }
  fadeThrottleNorm = fadePctToThrottleNorm(min((uint16_t)FADE_MAX_VALUE, g_storedVar.carParam[g_carSel].fade));
  curveVertexInputNorm = curveVertexInputWithFade(fadeThrottleNorm, g_storedVar.carParam[g_carSel].throttleCurveVertex.inputThrottle);

  /* Calculate the output speed of the throttle curve vertex
     This is calculated as the curveSpeedDiff (from 10% to 90%) percentage of the difference between minSpeed and maxSpeed */
  throttleCurveVertexSpeedRaw = tmpMinSpeedRaw + (((uint32_t)maxSpeedRaw - (uint32_t)tmpMinSpeedRaw) * ((uint32_t)g_storedVar.carParam[g_carSel].throttleCurveVertex.curveSpeedDiff) / 100);

  if (inputThrottleNorm == 0)   /* If input throttle is 0 --> output speed is 0% */
  {
    outputSpeedRaw = 0;
  }
  else if (fadeThrottleNorm > 0 && inputThrottleNorm <= fadeThrottleNorm)
  {
    /* FADE fills the gap between 0 output and SENSI over the first part of trigger travel. */
    outputSpeedRaw = (uint16_t)map(inputThrottleNorm, 0, fadeThrottleNorm, 0, tmpMinSpeedRaw);
  }
  else if (inputThrottleNorm <= curveVertexInputNorm)
  {
    if (curveVertexInputNorm <= fadeThrottleNorm)
    {
      outputSpeedRaw = (uint16_t)throttleCurveVertexSpeedRaw;
    }
    else
    {
      outputSpeedRaw = (uint16_t)map(inputThrottleNorm, fadeThrottleNorm, curveVertexInputNorm, tmpMinSpeedRaw, throttleCurveVertexSpeedRaw);
    }
  }
  else
  {
    if (curveVertexInputNorm >= THROTTLE_NORMALIZED)
    {
      outputSpeedRaw = maxSpeedRaw;
    }
    else
    {
      outputSpeedRaw = (uint16_t)map(inputThrottleNorm, curveVertexInputNorm, THROTTLE_NORMALIZED, throttleCurveVertexSpeedRaw, maxSpeedRaw);
    }
  }

  return outputSpeedRaw;
}

/**
 * Take an ADC raw value where the max and min has been recorded and returns the value scaled
 * 
 * @param raw The raw ADC value
 * @param minIn The lowest reading of the ADC
 * @param maxIn The highest reading of the ADC
 * @param normalizedMax The new max value of the scale
 * @param isReversed Whether the throttle is reversed or not
 * @return The raw ADC value scaled from 0 to normalizedMax
 */
uint16_t normalizeAndClamp(uint16_t raw, uint16_t minIn, uint16_t maxIn, uint16_t normalizedMax, bool isReversed)
{
  uint16_t retVal = 0;  /* From 0 to normalizedMax */

  if (maxIn == minIn)  /* If maxIn == minIn avoid division by 0 */
  {
    retVal = 0;
  }
  else
  {
    raw = constrain(raw, minIn, maxIn); /* Make sure the raw value is constrained between minIn and maxIn */

    if (isReversed == true)  /* If throttle is reversed (it goes to low values when pressed) */
    {
      raw = abs(maxIn - raw);
    }
    else
    {
      raw = abs(raw - minIn);
    }

    retVal = ((uint32_t)raw * normalizedMax) / (maxIn - minIn); /* Scale the raw reading */
  }

  return retVal;
}
