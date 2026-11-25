#ifndef THROTTLE_PROCESSING_H_
#define THROTTLE_PROCESSING_H_

#include <stdint.h>

/* Trigger-to-output processing called from Task2 (see throttle_processing.cpp). */
uint16_t normalizeAndClamp(uint16_t raw, uint16_t minIn, uint16_t maxIn, uint16_t normalizedMax, bool isReversed);
uint16_t addDeadBand(uint16_t inputVal, uint16_t minVal, uint16_t maxVal, uint16_t deadBand);
uint16_t throttleCurve2(uint16_t inputThrottleNorm);
uint16_t throttleAntiSpin3(uint16_t requestedSpeedX10);

#endif  /* THROTTLE_PROCESSING_H_ */
