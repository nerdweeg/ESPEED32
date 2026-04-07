#include "telemetry_logging.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

static portMUX_TYPE g_telemetryMux = portMUX_INITIALIZER_UNLOCKED;
static TelemetrySample* g_telemetrySamples = nullptr;
static TelemetryEvent* g_telemetryEvents = nullptr;
static TelemetryConfigSnapshot g_telemetryConfigSnapshot;
static bool g_telemetryConfigValid = false;
static bool g_telemetryLoggingActive = false;
static bool g_telemetryWrapped = false;
static uint16_t g_telemetryHead = 0;
static uint16_t g_telemetryCount = 0;
static uint16_t g_telemetryEventHead = 0;
static uint16_t g_telemetryEventCount = 0;
static uint32_t g_telemetryNextSeq = 1;
static uint32_t g_telemetryNextEventId = 1;
static uint32_t g_telemetrySessionId = 0;
static uint32_t g_telemetrySessionStartMs = 0;
static uint32_t g_telemetryLastCaptureMs = 0;
static uint8_t g_telemetrySessionStartCarIndex = 0;
static uint8_t g_telemetryLastSelectedCarIndex = 0;
static bool g_telemetryLastActiveCarValid = false;
static CarParam_type g_telemetryLastActiveCar;

static uint32_t telemetryOldestSeqLocked() {
  if (g_telemetryCount == 0 || g_telemetryNextSeq == 0) {
    return 0;
  }
  return g_telemetryNextSeq - (uint32_t)g_telemetryCount;
}

static uint32_t telemetryLatestSeqLocked() {
  if (g_telemetryCount == 0 || g_telemetryNextSeq <= 1U) {
    return 0;
  }
  return g_telemetryNextSeq - 1U;
}

static uint32_t telemetryOldestEventIdLocked() {
  if (g_telemetryEventCount == 0 || g_telemetryNextEventId == 0) {
    return 0;
  }
  return g_telemetryNextEventId - (uint32_t)g_telemetryEventCount;
}

static uint32_t telemetryLatestEventIdLocked() {
  if (g_telemetryEventCount == 0 || g_telemetryNextEventId <= 1U) {
    return 0;
  }
  return g_telemetryNextEventId - 1U;
}

static uint16_t telemetryOldestIndexLocked() {
  if (g_telemetryCount == 0) {
    return 0;
  }
  return (uint16_t)((g_telemetryHead + TELEMETRY_BUFFER_CAPACITY - g_telemetryCount) % TELEMETRY_BUFFER_CAPACITY);
}

static uint16_t telemetryOldestEventIndexLocked() {
  if (g_telemetryEventCount == 0) {
    return 0;
  }
  return (uint16_t)((g_telemetryEventHead + TELEMETRY_EVENT_BUFFER_CAPACITY - g_telemetryEventCount) % TELEMETRY_EVENT_BUFFER_CAPACITY);
}

static uint16_t telemetryComputeCarParamChangeMask(const CarParam_type* before, const CarParam_type* after) {
  if (before == nullptr || after == nullptr) {
    return 0U;
  }

  uint16_t mask = 0U;
  if (before->minSpeed != after->minSpeed) mask |= TELEMETRY_CHANGE_MIN_SPEED;
  if (before->brake != after->brake) mask |= TELEMETRY_CHANGE_BRAKE;
  if (before->maxSpeed != after->maxSpeed) mask |= TELEMETRY_CHANGE_MAX_SPEED;
  if (before->throttleCurveVertex.inputThrottle != after->throttleCurveVertex.inputThrottle) mask |= TELEMETRY_CHANGE_CURVE_INPUT;
  if (before->throttleCurveVertex.curveSpeedDiff != after->throttleCurveVertex.curveSpeedDiff) mask |= TELEMETRY_CHANGE_CURVE_DIFF;
  if (before->fade != after->fade) mask |= TELEMETRY_CHANGE_FADE;
  if (before->antiSpin != after->antiSpin) mask |= TELEMETRY_CHANGE_ANTI_SPIN;
  if (strncmp(before->carName, after->carName, CAR_NAME_MAX_SIZE) != 0) mask |= TELEMETRY_CHANGE_CAR_NAME;
  if (before->freqPWM != after->freqPWM) mask |= TELEMETRY_CHANGE_FREQ_PWM;
  if (before->brakeButtonReduction != after->brakeButtonReduction) mask |= TELEMETRY_CHANGE_BRAKE_BUTTON;
  if (before->quickBrakeEnabled != after->quickBrakeEnabled) mask |= TELEMETRY_CHANGE_RELEASE_MODE;
  if (before->quickBrakeThreshold != after->quickBrakeThreshold) mask |= TELEMETRY_CHANGE_RELEASE_ZONE;
  if (before->quickBrakeStrength != after->quickBrakeStrength) mask |= TELEMETRY_CHANGE_RELEASE_LEVEL;
  return mask;
}

static void telemetryRecordEventLocked(uint8_t type,
                                       uint32_t tMs,
                                       uint32_t sampleSeq,
                                       uint8_t carIndex,
                                       uint8_t previousCarIndex,
                                       uint16_t changedMask,
                                       const CarParam_type* carParam) {
  if (g_telemetryEvents == nullptr || carParam == nullptr || carIndex >= CAR_MAX_COUNT) {
    return;
  }

  TelemetryEvent event;
  event.id = g_telemetryNextEventId++;
  if (g_telemetryNextEventId == 0U) {
    g_telemetryNextEventId = 1U;
  }
  event.t_ms = tMs;
  event.sampleSeq = sampleSeq;
  event.changedMask = changedMask;
  event.type = type;
  event.carIndex = carIndex;
  event.previousCarIndex = previousCarIndex;
  event.carParam = *carParam;

  g_telemetryEvents[g_telemetryEventHead] = event;
  g_telemetryEventHead = (uint16_t)((g_telemetryEventHead + 1U) % TELEMETRY_EVENT_BUFFER_CAPACITY);
  if (g_telemetryEventCount < TELEMETRY_EVENT_BUFFER_CAPACITY) {
    g_telemetryEventCount++;
  }
}

static void telemetryFillStatusLocked(TelemetryStatus* outStatus) {
  if (outStatus == nullptr) {
    return;
  }
  outStatus->sessionId = g_telemetrySessionId;
  outStatus->sessionStartMs = g_telemetrySessionStartMs;
  outStatus->oldestSeq = telemetryOldestSeqLocked();
  outStatus->latestSeq = telemetryLatestSeqLocked();
  outStatus->oldestEventId = telemetryOldestEventIdLocked();
  outStatus->latestEventId = telemetryLatestEventIdLocked();
  outStatus->storedCount = g_telemetryCount;
  outStatus->capacity = TELEMETRY_BUFFER_CAPACITY;
  outStatus->eventCount = g_telemetryEventCount;
  outStatus->eventCapacity = TELEMETRY_EVENT_BUFFER_CAPACITY;
  outStatus->sampleRateMs = TELEMETRY_SAMPLE_INTERVAL_MS;
  outStatus->sessionStartCarIndex = g_telemetrySessionStartCarIndex;
  outStatus->loggingActive = g_telemetryLoggingActive;
  outStatus->hasData = (g_telemetryCount > 0);
  outStatus->wrapped = g_telemetryWrapped;
}

bool telemetryStartLogging(const StoredVar_type* storedVar,
                           uint16_t antiSpinStepMs,
                           uint16_t encoderInvertEnabled,
                           uint16_t adcVoltageRange_mV,
                           uint8_t activeCarIndex) {
  if (storedVar == nullptr || activeCarIndex >= CAR_MAX_COUNT) {
    return false;
  }

  uint32_t nowMs = millis();
  TelemetrySample* allocatedBuffer = g_telemetrySamples;
  TelemetryEvent* allocatedEventBuffer = g_telemetryEvents;
  if (allocatedBuffer == nullptr) {
    allocatedBuffer = (TelemetrySample*)malloc(sizeof(TelemetrySample) * TELEMETRY_BUFFER_CAPACITY);
    if (allocatedBuffer == nullptr) {
      return false;
    }
  }
  if (allocatedEventBuffer == nullptr) {
    allocatedEventBuffer = (TelemetryEvent*)malloc(sizeof(TelemetryEvent) * TELEMETRY_EVENT_BUFFER_CAPACITY);
    if (allocatedEventBuffer == nullptr) {
      if (g_telemetrySamples == nullptr && allocatedBuffer != nullptr) {
        free(allocatedBuffer);
      }
      return false;
    }
  }

  portENTER_CRITICAL(&g_telemetryMux);
  if (g_telemetrySamples == nullptr) {
    g_telemetrySamples = allocatedBuffer;
  }
  if (g_telemetryEvents == nullptr) {
    g_telemetryEvents = allocatedEventBuffer;
  }
  bool shouldFreeTempBuffer = (allocatedBuffer != nullptr && g_telemetrySamples != allocatedBuffer);
  bool shouldFreeTempEventBuffer = (allocatedEventBuffer != nullptr && g_telemetryEvents != allocatedEventBuffer);
  g_telemetryConfigSnapshot.storedVar = *storedVar;
  g_telemetryConfigSnapshot.antiSpinStepMs = antiSpinStepMs;
  g_telemetryConfigSnapshot.encoderInvertEnabled = encoderInvertEnabled ? 1U : 0U;
  g_telemetryConfigSnapshot.adcVoltageRange_mV = adcVoltageRange_mV;
  g_telemetryConfigValid = true;

  g_telemetryHead = 0;
  g_telemetryCount = 0;
  g_telemetryEventHead = 0;
  g_telemetryEventCount = 0;
  g_telemetryWrapped = false;
  g_telemetryNextSeq = 1;
  g_telemetryNextEventId = 1;
  g_telemetrySessionStartMs = nowMs;
  g_telemetryLastCaptureMs = 0;
  g_telemetrySessionStartCarIndex = activeCarIndex;
  g_telemetryLastSelectedCarIndex = activeCarIndex;
  g_telemetryLastActiveCar = storedVar->carParam[activeCarIndex];
  g_telemetryLastActiveCarValid = true;
  g_telemetrySessionId++;
  if (g_telemetrySessionId == 0) {
    g_telemetrySessionId = 1;
  }
  g_telemetryLoggingActive = true;
  portEXIT_CRITICAL(&g_telemetryMux);

  if (shouldFreeTempBuffer) {
    free(allocatedBuffer);
  }
  if (shouldFreeTempEventBuffer) {
    free(allocatedEventBuffer);
  }

  return true;
}

void telemetryStopLogging() {
  portENTER_CRITICAL(&g_telemetryMux);
  g_telemetryLoggingActive = false;
  portEXIT_CRITICAL(&g_telemetryMux);
}

void telemetryClear() {
  TelemetrySample* bufferToFree = nullptr;
  TelemetryEvent* eventBufferToFree = nullptr;
  portENTER_CRITICAL(&g_telemetryMux);
  g_telemetryLoggingActive = false;
  bufferToFree = g_telemetrySamples;
  eventBufferToFree = g_telemetryEvents;
  g_telemetrySamples = nullptr;
  g_telemetryEvents = nullptr;
  g_telemetryHead = 0;
  g_telemetryCount = 0;
  g_telemetryEventHead = 0;
  g_telemetryEventCount = 0;
  g_telemetryWrapped = false;
  g_telemetryNextSeq = 1;
  g_telemetryNextEventId = 1;
  g_telemetrySessionStartMs = 0;
  g_telemetryLastCaptureMs = 0;
  g_telemetrySessionStartCarIndex = 0;
  g_telemetryLastSelectedCarIndex = 0;
  g_telemetryLastActiveCarValid = false;
  g_telemetryConfigValid = false;
  portEXIT_CRITICAL(&g_telemetryMux);

  if (bufferToFree != nullptr) {
    free(bufferToFree);
  }
  if (eventBufferToFree != nullptr) {
    free(eventBufferToFree);
  }
}

bool telemetryIsLoggingActive() {
  bool active = false;
  portENTER_CRITICAL(&g_telemetryMux);
  active = g_telemetryLoggingActive;
  portEXIT_CRITICAL(&g_telemetryMux);
  return active;
}

bool telemetryHasData() {
  bool hasData = false;
  portENTER_CRITICAL(&g_telemetryMux);
  hasData = (g_telemetryCount > 0);
  portEXIT_CRITICAL(&g_telemetryMux);
  return hasData;
}

void telemetryGetStatus(TelemetryStatus* outStatus) {
  portENTER_CRITICAL(&g_telemetryMux);
  telemetryFillStatusLocked(outStatus);
  portEXIT_CRITICAL(&g_telemetryMux);
}

bool telemetryGetConfigSnapshot(TelemetryConfigSnapshot* outSnapshot) {
  bool hasSnapshot = false;

  if (outSnapshot == nullptr) {
    return false;
  }

  portENTER_CRITICAL(&g_telemetryMux);
  hasSnapshot = g_telemetryConfigValid;
  if (hasSnapshot) {
    *outSnapshot = g_telemetryConfigSnapshot;
  }
  portEXIT_CRITICAL(&g_telemetryMux);

  return hasSnapshot;
}

size_t telemetryCopySamplesAfter(uint32_t afterSeq,
                                 TelemetrySample* outSamples,
                                 size_t maxSamples,
                                 bool* outTruncated,
                                 bool* outHasMore,
                                 TelemetryStatus* outStatus) {
  bool truncated = false;
  bool hasMore = false;
  size_t copied = 0;

  portENTER_CRITICAL(&g_telemetryMux);

  telemetryFillStatusLocked(outStatus);

  if (g_telemetryCount > 0 && outSamples != nullptr && maxSamples > 0) {
    uint32_t oldestSeq = telemetryOldestSeqLocked();
    uint32_t latestSeq = telemetryLatestSeqLocked();
    uint16_t oldestIndex = telemetryOldestIndexLocked();
    uint32_t firstSeq = afterSeq + 1U;

    if (firstSeq < oldestSeq) {
      firstSeq = oldestSeq;
      truncated = true;
    }

    if (firstSeq <= latestSeq) {
      size_t available = (size_t)(latestSeq - firstSeq + 1U);
      copied = (available < maxSamples) ? available : maxSamples;

      for (size_t i = 0; i < copied; i++) {
        uint32_t seq = firstSeq + (uint32_t)i;
        uint16_t offset = (uint16_t)(seq - oldestSeq);
        uint16_t index = (uint16_t)((oldestIndex + offset) % TELEMETRY_BUFFER_CAPACITY);
        outSamples[i] = g_telemetrySamples[index];
      }

      hasMore = (firstSeq + (uint32_t)copied - 1U) < latestSeq;
    }
  }

  portEXIT_CRITICAL(&g_telemetryMux);

  if (outTruncated != nullptr) {
    *outTruncated = truncated;
  }
  if (outHasMore != nullptr) {
    *outHasMore = hasMore;
  }

  return copied;
}

size_t telemetryCopyEvents(TelemetryEvent* outEvents,
                           size_t maxEvents,
                           bool* outTruncated,
                           TelemetryStatus* outStatus) {
  bool truncated = false;
  size_t copied = 0;

  portENTER_CRITICAL(&g_telemetryMux);

  telemetryFillStatusLocked(outStatus);

  if (g_telemetryEventCount > 0 && outEvents != nullptr && maxEvents > 0) {
    uint16_t oldestIndex = telemetryOldestEventIndexLocked();
    size_t available = g_telemetryEventCount;
    copied = (available < maxEvents) ? available : maxEvents;
    if (available > maxEvents) {
      truncated = true;
      oldestIndex = (uint16_t)((oldestIndex + (available - maxEvents)) % TELEMETRY_EVENT_BUFFER_CAPACITY);
    }

    for (size_t i = 0; i < copied; i++) {
      uint16_t index = (uint16_t)((oldestIndex + i) % TELEMETRY_EVENT_BUFFER_CAPACITY);
      outEvents[i] = g_telemetryEvents[index];
    }
  }

  portEXIT_CRITICAL(&g_telemetryMux);

  if (outTruncated != nullptr) {
    *outTruncated = truncated;
  }

  return copied;
}

void telemetryCaptureSample(uint8_t carIndex,
                            const CarParam_type* activeCarParam,
                            uint8_t triggerPct,
                            uint8_t outputPct,
                            uint16_t vin_mV,
                            uint16_t current_mA,
                            uint16_t brakePct,
                            uint16_t sensi_halfPct,
                            uint8_t releaseMode,
                            uint8_t flags) {
  if (carIndex >= CAR_MAX_COUNT) {
    return;
  }

  uint32_t nowMs = millis();

  portENTER_CRITICAL(&g_telemetryMux);

  if (!g_telemetryLoggingActive) {
    portEXIT_CRITICAL(&g_telemetryMux);
    return;
  }

  if (g_telemetrySamples == nullptr) {
    g_telemetryLoggingActive = false;
    portEXIT_CRITICAL(&g_telemetryMux);
    return;
  }

  if (g_telemetryCount > 0 && (uint32_t)(nowMs - g_telemetryLastCaptureMs) < TELEMETRY_SAMPLE_INTERVAL_MS) {
    portEXIT_CRITICAL(&g_telemetryMux);
    return;
  }

  uint32_t sampleSeq = g_telemetryNextSeq;
  uint32_t relativeTimeMs = nowMs - g_telemetrySessionStartMs;
  if (activeCarParam != nullptr) {
    if (!g_telemetryLastActiveCarValid) {
      g_telemetryLastSelectedCarIndex = carIndex;
      g_telemetryLastActiveCar = *activeCarParam;
      g_telemetryLastActiveCarValid = true;
    } else if (g_telemetryLastSelectedCarIndex != carIndex) {
      telemetryRecordEventLocked(TELEMETRY_EVENT_CAR_SELECT,
                                 relativeTimeMs,
                                 sampleSeq,
                                 carIndex,
                                 g_telemetryLastSelectedCarIndex,
                                 0U,
                                 activeCarParam);
      g_telemetryLastSelectedCarIndex = carIndex;
      g_telemetryLastActiveCar = *activeCarParam;
    } else {
      uint16_t changedMask = telemetryComputeCarParamChangeMask(&g_telemetryLastActiveCar, activeCarParam);
      if (changedMask != 0U) {
        telemetryRecordEventLocked(TELEMETRY_EVENT_CAR_PARAMS,
                                   relativeTimeMs,
                                   sampleSeq,
                                   carIndex,
                                   g_telemetryLastSelectedCarIndex,
                                   changedMask,
                                   activeCarParam);
        g_telemetryLastActiveCar = *activeCarParam;
      }
    }
  }

  TelemetrySample sample;
  sample.seq = g_telemetryNextSeq++;
  sample.t_ms = relativeTimeMs;
  sample.vin_mV = vin_mV;
  sample.current_mA = current_mA;
  sample.sensi_halfPct = sensi_halfPct;
  sample.trigger_pct = triggerPct;
  sample.output_pct = outputPct;
  sample.brake_pct = brakePct;
  sample.carIndex = carIndex;
  sample.releaseMode = releaseMode;
  sample.flags = flags;

  g_telemetrySamples[g_telemetryHead] = sample;
  g_telemetryHead = (uint16_t)((g_telemetryHead + 1U) % TELEMETRY_BUFFER_CAPACITY);
  if (g_telemetryCount < TELEMETRY_BUFFER_CAPACITY) {
    g_telemetryCount++;
  } else {
    g_telemetryWrapped = true;
  }
  g_telemetryLastCaptureMs = nowMs;

  portEXIT_CRITICAL(&g_telemetryMux);
}
