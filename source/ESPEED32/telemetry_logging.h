#ifndef TELEMETRY_LOGGING_H_
#define TELEMETRY_LOGGING_H_

#include <stddef.h>
#include <stdint.h>
#include <Arduino.h>
#include "slot_ESC.h"

#define TELEMETRY_SAMPLE_INTERVAL_MS 20U
#define TELEMETRY_BUFFER_CAPACITY    3000U  /* Maximum sample capacity when enough heap is available */
#define TELEMETRY_BUFFER_MIN_CAPACITY 512U /* Minimum useful sample capacity before start fails */
#define TELEMETRY_BUFFER_ALLOC_STEP   128U /* Step-down size when adapting sample capacity to free heap */
#define TELEMETRY_EVENT_BUFFER_CAPACITY 64U /* Maximum event capacity when enough heap is available */
#define TELEMETRY_EVENT_BUFFER_MIN_CAPACITY 16U
#define TELEMETRY_EVENT_BUFFER_ALLOC_STEP 16U
#define TELEMETRY_HEAP_RESERVE_BYTES  65536U /* Keep headroom for WiFi/web/OTA while telemetry is active */
#define TELEMETRY_HEAP_MIN_RESERVE_BYTES 49152U

#define TELEMETRY_FLAG_BRAKE_BUTTON      0x01U
#define TELEMETRY_FLAG_TRIGGER_RELEASING 0x02U
#define TELEMETRY_FLAG_IN_RELEASE_ZONE   0x04U
#define TELEMETRY_FLAG_RELEASE_ACTIVE    0x08U
#define TELEMETRY_FLAG_CURRENT_SENSE     0x10U

#define TELEMETRY_EVENT_CAR_SELECT 0x01U
#define TELEMETRY_EVENT_CAR_PARAMS 0x02U

#define TELEMETRY_CHANGE_MIN_SPEED      0x0001U
#define TELEMETRY_CHANGE_BRAKE          0x0002U
#define TELEMETRY_CHANGE_MAX_SPEED      0x0004U
#define TELEMETRY_CHANGE_CURVE_INPUT    0x0008U
#define TELEMETRY_CHANGE_CURVE_DIFF     0x0010U
#define TELEMETRY_CHANGE_FADE           0x0020U
#define TELEMETRY_CHANGE_ANTI_SPIN      0x0040U
#define TELEMETRY_CHANGE_CAR_NAME       0x0080U
#define TELEMETRY_CHANGE_FREQ_PWM       0x0100U
#define TELEMETRY_CHANGE_BRAKE_BUTTON   0x0200U
#define TELEMETRY_CHANGE_RELEASE_MODE   0x0400U
#define TELEMETRY_CHANGE_RELEASE_ZONE   0x0800U
#define TELEMETRY_CHANGE_RELEASE_LEVEL  0x1000U

typedef struct {
  StoredVar_type storedVar;
  uint16_t antiSpinStepMs;
  uint16_t encoderInvertEnabled;
  uint16_t adcVoltageRange_mV;
} TelemetryConfigSnapshot;

typedef struct {
  uint32_t seq;
  uint32_t t_ms;
  uint16_t vin_mV;
  uint16_t current_mA;
  uint16_t sensi_halfPct; /* [0.1%] Historical field name kept for storage/API compatibility */
  uint8_t trigger_pct;
  uint8_t output_pct;
  uint16_t brake_pct;      /* [0.1%] Applied brake value */
  uint8_t carIndex;
  uint8_t releaseMode;
  uint8_t flags;
} TelemetrySample;

typedef struct {
  uint32_t id;
  uint32_t t_ms;
  uint32_t sampleSeq;
  uint16_t changedMask;
  uint8_t type;
  uint8_t carIndex;
  uint8_t previousCarIndex;
  CarParam_type carParam;
} TelemetryEvent;

typedef struct {
  uint32_t sessionId;
  uint32_t sessionStartMs;
  uint32_t oldestSeq;
  uint32_t latestSeq;
  uint32_t oldestEventId;
  uint32_t latestEventId;
  uint16_t storedCount;
  uint16_t capacity;
  uint16_t eventCount;
  uint16_t eventCapacity;
  uint16_t sampleRateMs;
  uint8_t sessionStartCarIndex;
  bool loggingActive;
  bool hasData;
  bool wrapped;
} TelemetryStatus;

bool telemetryStartLogging(const StoredVar_type* storedVar,
                           uint16_t antiSpinStepMs,
                           uint16_t encoderInvertEnabled,
                           uint16_t adcVoltageRange_mV,
                           uint8_t activeCarIndex);
const char* telemetryGetLastStartError();
void telemetryStopLogging();
void telemetryClear();
bool telemetryIsLoggingActive();
bool telemetryHasData();
void telemetryGetStatus(TelemetryStatus* outStatus);
bool telemetryGetConfigSnapshot(TelemetryConfigSnapshot* outSnapshot);
size_t telemetryCopySamplesAfter(uint32_t afterSeq,
                                 TelemetrySample* outSamples,
                                 size_t maxSamples,
                                 bool* outTruncated,
                                 bool* outHasMore,
                                 TelemetryStatus* outStatus);
size_t telemetryCopyEvents(TelemetryEvent* outEvents,
                           size_t maxEvents,
                           bool* outTruncated,
                           TelemetryStatus* outStatus);
void telemetryCaptureSample(uint8_t carIndex,
                            const CarParam_type* activeCarParam,
                            uint8_t triggerPct,
                            uint8_t outputPct,
                            uint16_t vin_mV,
                            uint16_t current_mA,
                            uint16_t brakePct,
                            uint16_t sensi_halfPct,
                            uint8_t releaseMode,
                            uint8_t flags);

#endif  /* TELEMETRY_LOGGING_H_ */
