#include "connectivity_portal_internal.h"

/* Telemetry HTTP API (/api/telemetry/*) and the JSON payloads it shares with the USB serial commands. */

static const size_t TELEMETRY_LIVE_RESPONSE_LIMIT = 16U;
static const size_t TELEMETRY_LIVE_EVENT_LIMIT = 4U;

static uint32_t getTelemetryArgU32(const char* name, uint32_t defaultValue) {
  if (g_wifiServer == nullptr || name == nullptr || !g_wifiServer->hasArg(name)) {
    return defaultValue;
  }

  String raw = g_wifiServer->arg(name);
  raw.trim();
  if (raw.length() == 0) {
    return defaultValue;
  }

  bool valid = true;
  for (uint16_t i = 0; i < raw.length(); i++) {
    if (!isDigit(raw[i])) {
      valid = false;
      break;
    }
  }
  if (!valid) {
    return defaultValue;
  }

  return (uint32_t)raw.toInt();
}

static size_t getTelemetryLiveLimit() {
  uint32_t limit = getTelemetryArgU32("limit", TELEMETRY_LIVE_RESPONSE_LIMIT);
  if (limit == 0U) {
    limit = TELEMETRY_LIVE_RESPONSE_LIMIT;
  }
  if (limit > TELEMETRY_LIVE_RESPONSE_LIMIT) {
    limit = TELEMETRY_LIVE_RESPONSE_LIMIT;
  }
  return (size_t)limit;
}

static void formatTenthPercentValue(uint16_t raw, char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  snprintf(out, outLen, "%u.%u", raw / 10U, raw % 10U);
}

static void appendPercentX10JsonValue(String& out, uint16_t raw) {
  out += String((unsigned int)(raw / 10U));
  out += ".";
  out += String((unsigned int)(raw % 10U));
}

static const char* getTelemetryReleaseModeLabel(uint8_t mode) {
  switch (mode) {
    case RELEASE_BRAKE_QUICK: return "QUICK";
    case RELEASE_BRAKE_DRAG:  return "DRAG";
    default:                  return "OFF";
  }
}

static void appendTelemetryCarParamJson(String& json, const CarParam_type& car) {
  char buf[256];
  json += "{";
  json += "\"name\":\"";
  appendJsonEscaped(json, car.carName);
  json += "\"";
  snprintf(buf, sizeof(buf),
           ",\"carNumber\":%u,\"minSpeedHalfPct\":%u,\"brake\":%u.%u,\"maxSpeed\":%u,"
           "\"curveInputPct\":%u,\"curveDiffPct\":%u,\"fade\":%u,\"antiSpin\":%u,"
           "\"freqPwm100Hz\":%u,\"brakeButton\":%u,\"releaseMode\":%u,\"releaseZone\":%u,\"releaseLevel\":%u",
           (unsigned int)car.carNumber,
           (unsigned int)car.minSpeed,
           (unsigned int)(car.brake / BRAKE_SCALE),
           (unsigned int)brakeFracDigit(car.brake),
           (unsigned int)car.maxSpeed,
           (unsigned int)car.throttleCurveVertex.inputThrottle,
           (unsigned int)car.throttleCurveVertex.curveSpeedDiff,
           (unsigned int)car.fade,
           (unsigned int)car.antiSpin,
           (unsigned int)car.freqPWM,
           (unsigned int)car.brakeButtonReduction,
           (unsigned int)car.quickBrakeEnabled,
           (unsigned int)car.quickBrakeThreshold,
           (unsigned int)car.quickBrakeStrength);
  json += buf;
  json += "}";
}

static void appendTelemetryCarNamesJson(String& json, const StoredVar_type& storedVar) {
  json += "[";
  for (uint8_t i = 0; i < CAR_MAX_COUNT; i++) {
    if (i > 0) {
      json += ",";
    }
    json += "\"";
    appendJsonEscaped(json, storedVar.carParam[i].carName);
    json += "\"";
  }
  json += "]";
}

static String buildTelemetryConfigSummaryJsonFromSnapshot(const TelemetryConfigSnapshot& snapshot,
                                                          uint8_t activeCarIndex) {
  if (activeCarIndex >= CAR_MAX_COUNT) {
    activeCarIndex = 0;
  }

  String json;
  json.reserve(960);
  json += "{";
  json += "\"selectedCarNumber\":";
  json.concat((unsigned int)activeCarIndex);
  json += ",\"antiSpinStepMs\":";
  json.concat((unsigned int)snapshot.antiSpinStepMs);
  json += ",\"encoderInvert\":";
  json.concat((unsigned int)(snapshot.encoderInvertEnabled ? 1U : 0U));
  json += ",\"adcVoltageRangeMv\":";
  json.concat((unsigned int)snapshot.adcVoltageRange_mV);
  json += ",\"statusSlots\":[";
  for (uint8_t i = 0; i < STATUS_SLOTS; i++) {
    if (i > 0) {
      json += ",";
    }
    json.concat((unsigned int)normalizeStatusSlotForUi(snapshot.storedVar.statusSlot[i]));
  }
  json += "],\"carNames\":";
  appendTelemetryCarNamesJson(json, snapshot.storedVar);
  json += ",\"activeCar\":";
  appendTelemetryCarParamJson(json, snapshot.storedVar.carParam[activeCarIndex]);
  json += "}";
  return json;
}

static void appendTelemetryEventJson(String& json, const TelemetryEvent& event) {
  json += "{";
  json += "\"id\":";
  json.concat((unsigned long)event.id);
  json += ",\"tMs\":";
  json.concat((unsigned long)event.t_ms);
  json += ",\"sampleSeq\":";
  json.concat((unsigned long)event.sampleSeq);
  json += ",\"type\":\"";
  json += (event.type == TELEMETRY_EVENT_CAR_SELECT) ? "car_select" : "car_params";
  json += "\",\"carIndex\":";
  json.concat((unsigned int)event.carIndex);
  json += ",\"previousCarIndex\":";
  json.concat((unsigned int)event.previousCarIndex);
  json += ",\"changedMask\":";
  json.concat((unsigned int)event.changedMask);
  json += ",\"carParams\":";
  appendTelemetryCarParamJson(json, event.carParam);
  json += "}";
}

static void appendTelemetryStatusFields(String& json, const TelemetryStatus& status) {
  char buf[320];
  uint8_t sessionCarIndex = (status.sessionStartCarIndex < CAR_MAX_COUNT) ? status.sessionStartCarIndex : 0;
  uint8_t currentCarIndex = (g_carSel < CAR_MAX_COUNT) ? (uint8_t)g_carSel : (uint8_t)g_storedVar.selectedCarNumber;
  uint8_t triggerPct = (uint8_t)(((uint32_t)g_escVar.trigger_norm * 100U) / THROTTLE_NORMALIZED);
  uint8_t outputPct = (uint8_t)constrain((int)g_escVar.outputSpeed_pct, 0, 100);
  uint16_t brakePct = constrain(g_escVar.effectiveBrake_raw, 0, BRAKE_MAX_VALUE);
  if (digitalRead(BUTT_PIN) == BUTTON_PRESSED && g_escVar.trigger_norm == 0) {
    brakePct = constrain(g_storedVar.carParam[currentCarIndex].brakeButtonReduction * BRAKE_SCALE, 0, BRAKE_MAX_VALUE);
  }

  snprintf(buf, sizeof(buf),
           ",\"loggingActive\":%u,\"hasData\":%u,\"wrapped\":%u,\"sampleRateMs\":%u,\"capacity\":%u,"
           "\"storedCount\":%u,\"eventCount\":%u,\"eventCapacity\":%u,"
           "\"sessionId\":%lu,\"sessionStartMs\":%lu,\"oldestSeq\":%lu,\"latestSeq\":%lu,"
           "\"oldestEventId\":%lu,\"latestEventId\":%lu,"
           "\"wifiActive\":%u,\"sessionStartCarIndex\":%u,\"currentCarIndex\":%u",
           status.loggingActive ? 1U : 0U,
           status.hasData ? 1U : 0U,
           status.wrapped ? 1U : 0U,
           status.sampleRateMs,
           status.capacity,
           status.storedCount,
           status.eventCount,
           status.eventCapacity,
           (unsigned long)status.sessionId,
           (unsigned long)status.sessionStartMs,
           (unsigned long)status.oldestSeq,
           (unsigned long)status.latestSeq,
           (unsigned long)status.oldestEventId,
           (unsigned long)status.latestEventId,
           isWiFiPortalActive() ? 1U : 0U,
           sessionCarIndex,
           currentCarIndex);
  json += buf;

  json += ",\"sessionStartCarName\":\"";
  appendJsonEscaped(json, g_storedVar.carParam[sessionCarIndex].carName);
  json += "\",\"currentCarName\":\"";
  appendJsonEscaped(json, g_storedVar.carParam[currentCarIndex].carName);
  json += "\",\"current\":{";

  snprintf(buf, sizeof(buf),
           "\"triggerPct\":%u,\"outputPct\":%u,\"brakePct\":%u.%u,\"sensiPct\":%u,\"sensiHalfPct\":%u,"
           "\"vinMv\":%u,\"currentMa\":%u,\"releaseMode\":%u,\"currentSense\":%u",
           triggerPct,
           outputPct,
           (unsigned int)(brakePct / BRAKE_SCALE),
           (unsigned int)brakeFracDigit(brakePct),
           g_escVar.effectiveSensi_raw,
           g_escVar.effectiveSensi_raw,
           g_escVar.Vin_mV,
           g_escVar.motorCurrent_mA,
           g_storedVar.carParam[currentCarIndex].quickBrakeEnabled,
           HAL_HasMotorCurrentSense() ? 1U : 0U);
  json += buf;

  json += ",\"statusSlots\":[";
  for (uint8_t i = 0; i < STATUS_SLOTS; i++) {
    if (i > 0) json += ",";
    json.concat((unsigned int)normalizeStatusSlotForUi(g_storedVar.statusSlot[i]));
  }
  json += "]}";
}

String buildTelemetryStatusPayload(const char* message) {
  TelemetryStatus status;
  telemetryGetStatus(&status);

  String json;
  json.reserve(768);
  json += "{\"ok\":true";
  if (message != nullptr && message[0] != '\0') {
    json += ",\"message\":\"";
    appendJsonEscaped(json, message);
    json += "\"";
  }
  appendTelemetryStatusFields(json, status);
  json += "}";
  return json;
}

String buildTelemetryLivePayload(uint32_t afterSeq, size_t limit) {
  static TelemetrySample samples[256];
  static TelemetryEvent events[TELEMETRY_LIVE_EVENT_LIMIT];
  TelemetryStatus status;
  bool includeEvents = (afterSeq == 0U);
  bool truncated = false;
  bool hasMore = false;
  bool eventsTruncated = false;
  size_t copied = telemetryCopySamplesAfter(afterSeq, samples, limit, &truncated, &hasMore, &status);
  size_t eventCopied = includeEvents ? telemetryCopyEvents(events, TELEMETRY_LIVE_EVENT_LIMIT, &eventsTruncated, nullptr) : 0U;

  String json;
  json.reserve(960 + (copied * 92U) + (eventCopied * 180U));
  json += "{\"ok\":true";
  appendTelemetryStatusFields(json, status);
  json += ",\"truncated\":";
  json += truncated ? "true" : "false";
  json += ",\"hasMore\":";
  json += hasMore ? "true" : "false";
  json += ",\"returned\":";
  json.concat((unsigned long)copied);
  json += ",\"eventTruncated\":";
  json += eventsTruncated ? "true" : "false";
  json += ",\"events\":[";
  for (size_t i = 0; i < eventCopied; i++) {
    if (i > 0) {
      json += ",";
    }
    appendTelemetryEventJson(json, events[i]);
  }
  json += "],\"samples\":[";

  for (size_t i = 0; i < copied; i++) {
    const TelemetrySample& s = samples[i];
    if (i > 0) {
      json += ",";
    }
    json += "[";
    json.concat((unsigned long)s.seq);
    json += ",";
    json.concat((unsigned long)s.t_ms);
    json += ",";
    json.concat((unsigned int)s.trigger_pct);
    json += ",";
    json.concat((unsigned int)s.output_pct);
    json += ",";
    json.concat((unsigned int)s.vin_mV);
    json += ",";
    json.concat((unsigned int)s.current_mA);
    json += ",";
    appendPercentX10JsonValue(json, s.brake_pct);
    json += ",";
    json.concat((unsigned int)s.sensi_halfPct);
    json += ",";
    json.concat((unsigned int)s.carIndex);
    json += ",";
    json.concat((unsigned int)s.releaseMode);
    json += ",";
    json.concat((unsigned int)s.flags);
    json += "]";
  }

  json += "]}";
  return json;
}

String buildTelemetryEventsPayload() {
  static TelemetryEvent events[TELEMETRY_EVENT_BUFFER_CAPACITY];
  TelemetryStatus status;
  bool eventsTruncated = false;
  size_t copied = telemetryCopyEvents(events, TELEMETRY_EVENT_BUFFER_CAPACITY, &eventsTruncated, &status);

  String json;
  json.reserve(768 + (copied * 180U));
  json += "{\"ok\":true";
  appendTelemetryStatusFields(json, status);
  json += ",\"eventTruncated\":";
  json += eventsTruncated ? "true" : "false";
  json += ",\"eventsTruncated\":";
  json += eventsTruncated ? "true" : "false";
  json += ",\"returned\":";
  json.concat((unsigned long)copied);
  json += ",\"events\":[";
  for (size_t i = 0; i < copied; i++) {
    if (i > 0) {
      json += ",";
    }
    appendTelemetryEventJson(json, events[i]);
  }
  json += "]}";
  return json;
}

String buildTelemetryConfigSnapshotJson() {
  TelemetryConfigSnapshot snapshot;
  TelemetryStatus status;
  bool hasSnapshot = telemetryGetConfigSnapshot(&snapshot);
  telemetryGetStatus(&status);
  if (!hasSnapshot) {
    snapshot.storedVar = g_storedVar;
    snapshot.antiSpinStepMs = g_antiSpinStepMs;
    snapshot.encoderInvertEnabled = g_encoderInvertEnabled;
    snapshot.adcVoltageRange_mV = g_adcVoltageRange_mV;
  }
  uint8_t activeCarIndex = status.hasData ? status.sessionStartCarIndex : (uint8_t)g_carSel;
  if (activeCarIndex >= CAR_MAX_COUNT) {
    activeCarIndex = (uint8_t)g_storedVar.selectedCarNumber;
  }
  return buildTelemetryConfigSummaryJsonFromSnapshot(snapshot, activeCarIndex);
}

void sendSerialLengthPrefixedPayload(const String& payload) {
  Serial.print(payload.length());
  Serial.print('\n');
  Serial.print(payload);
  Serial.flush();
}

void handleTelemetryStatus() {
  g_wifiServer->send(200, "application/json", buildTelemetryStatusPayload(""));
}

void handleTelemetryConfig() {
  g_wifiServer->send(200, "application/json", buildTelemetryConfigSnapshotJson());
}

void handleTelemetryStart() {
  if (!telemetryStartLogging(&g_storedVar,
                             g_antiSpinStepMs,
                             g_encoderInvertEnabled,
                             g_adcVoltageRange_mV,
                             (uint8_t)g_carSel)) {
    const char* err = telemetryGetLastStartError();
    if (err == nullptr || err[0] == '\0') err = "Telemetry start failed";
    String payload = "{\"ok\":false,\"error\":\"";
    appendJsonEscaped(payload, err);
    payload += "\"}";
    g_wifiServer->send(500, "application/json", payload);
    return;
  }

  g_wifiServer->send(200, "application/json", buildTelemetryStatusPayload("Telemetry logging started"));
}

void handleTelemetryStop() {
  telemetryStopLogging();
  g_wifiServer->send(200, "application/json", buildTelemetryStatusPayload("Telemetry logging stopped"));
}

void handleTelemetryClear() {
  telemetryStopLogging();
  telemetryClear();
  g_wifiServer->send(200, "application/json", buildTelemetryStatusPayload("Telemetry buffer cleared"));
}

void handleTelemetryLive() {
  uint32_t afterSeq = getTelemetryArgU32("after", 0U);
  size_t limit = getTelemetryLiveLimit();
  g_wifiServer->send(200, "application/json", buildTelemetryLivePayload(afterSeq, limit));
}

void handleTelemetryEvents() {
  g_wifiServer->send(200, "application/json", buildTelemetryEventsPayload());
}

void handleTelemetryExportCsv() {
  TelemetryStatus status;
  telemetryGetStatus(&status);
  if (!status.hasData) {
    g_wifiServer->send(404, "application/json", "{\"ok\":false,\"error\":\"No telemetry data available\"}");
    return;
  }

  TelemetryConfigSnapshot snapshot;
  bool hasSnapshot = telemetryGetConfigSnapshot(&snapshot);
  if (!hasSnapshot) {
    snapshot.storedVar = g_storedVar;
    snapshot.antiSpinStepMs = g_antiSpinStepMs;
    snapshot.encoderInvertEnabled = g_encoderInvertEnabled;
    snapshot.adcVoltageRange_mV = g_adcVoltageRange_mV;
  }

  g_wifiServer->setContentLength(CONTENT_LENGTH_UNKNOWN);
  g_wifiServer->sendHeader("Content-Disposition", "attachment; filename=\"espeed32-telemetry.csv\"");
  g_wifiServer->send(200, "text/csv; charset=utf-8", "");

  g_wifiServer->sendContent("seq,t_ms,car_index,car_name,trigger_pct,output_pct,brake_pct,sensi_pct,vin_mV,current_mA,release_mode,brake_button,trigger_releasing,in_release_zone,release_active,current_sense\n");

  static TelemetrySample batch[96];
  uint32_t afterSeq = 0;

  while (true) {
    TelemetryStatus batchStatus;
    bool truncated = false;
    bool hasMore = false;
    size_t copied = telemetryCopySamplesAfter(afterSeq, batch, 96U, &truncated, &hasMore, &batchStatus);
    if (copied == 0) {
      break;
    }

    String chunk;
    chunk.reserve(copied * 96U);
    for (size_t i = 0; i < copied; i++) {
      const TelemetrySample& s = batch[i];
      char sensiBuf[12];
      char row[192];
      char brakeBuf[12];
      formatTenthPercentValue(s.sensi_halfPct, sensiBuf, sizeof(sensiBuf));
      snprintf(brakeBuf, sizeof(brakeBuf), "%u.%u", (unsigned int)(s.brake_pct / BRAKE_SCALE), (unsigned int)brakeFracDigit(s.brake_pct));
      snprintf(row, sizeof(row),
               "%lu,%lu,%u,%s,%u,%u,%s,%s,%u,%u,%s,%u,%u,%u,%u,%u\n",
               (unsigned long)s.seq,
               (unsigned long)s.t_ms,
               (unsigned int)s.carIndex,
               snapshot.storedVar.carParam[s.carIndex].carName,
               (unsigned int)s.trigger_pct,
               (unsigned int)s.output_pct,
               brakeBuf,
               sensiBuf,
               (unsigned int)s.vin_mV,
               (unsigned int)s.current_mA,
               getTelemetryReleaseModeLabel(s.releaseMode),
               (s.flags & TELEMETRY_FLAG_BRAKE_BUTTON) ? 1U : 0U,
               (s.flags & TELEMETRY_FLAG_TRIGGER_RELEASING) ? 1U : 0U,
               (s.flags & TELEMETRY_FLAG_IN_RELEASE_ZONE) ? 1U : 0U,
               (s.flags & TELEMETRY_FLAG_RELEASE_ACTIVE) ? 1U : 0U,
               (s.flags & TELEMETRY_FLAG_CURRENT_SENSE) ? 1U : 0U);
      chunk += row;
      afterSeq = s.seq;
    }

    g_wifiServer->sendContent(chunk);
    if (!hasMore) {
      break;
    }
  }

  g_wifiServer->sendContent("");
}

void handleTelemetryExportJson() {
  TelemetryStatus status;
  telemetryGetStatus(&status);
  if (!status.hasData) {
    g_wifiServer->send(404, "application/json", "{\"ok\":false,\"error\":\"No telemetry data available\"}");
    return;
  }

  TelemetryConfigSnapshot snapshot;
  bool hasSnapshot = telemetryGetConfigSnapshot(&snapshot);
  if (!hasSnapshot) {
    snapshot.storedVar = g_storedVar;
    snapshot.antiSpinStepMs = g_antiSpinStepMs;
    snapshot.encoderInvertEnabled = g_encoderInvertEnabled;
    snapshot.adcVoltageRange_mV = g_adcVoltageRange_mV;
  }

  char versionBuf[8];
  snprintf(versionBuf, sizeof(versionBuf), "%d.%d", SW_MAJOR_VERSION, SW_MINOR_VERSION);

  g_wifiServer->setContentLength(CONTENT_LENGTH_UNKNOWN);
  g_wifiServer->sendHeader("Content-Disposition", "attachment; filename=\"espeed32-telemetry.json\"");
  g_wifiServer->send(200, "application/json; charset=utf-8", "");

  static TelemetryEvent events[TELEMETRY_EVENT_BUFFER_CAPACITY];
  bool eventsTruncated = false;
  size_t eventCount = telemetryCopyEvents(events, TELEMETRY_EVENT_BUFFER_CAPACITY, &eventsTruncated, nullptr);

  String head;
  head.reserve(1800 + (eventCount * 180U));
  head += "{";
  head += "\"ok\":true,\"format\":\"espeed32-telemetry-v1\",\"deviceId\":\"";
  char deviceId[5];
  getDeviceIdSuffix(deviceId, sizeof(deviceId));
  appendJsonEscaped(head, deviceId);
  head += "\",\"firmware\":\"";
  appendJsonEscaped(head, versionBuf);
  head += "\",\"loggingActive\":";
  head += status.loggingActive ? "true" : "false";
  head += ",\"sampleRateMs\":";
  head += String(status.sampleRateMs);
  head += ",\"capacity\":";
  head += String(status.capacity);
  head += ",\"storedCount\":";
  head += String(status.storedCount);
  head += ",\"sessionId\":";
  head += String((unsigned long)status.sessionId);
  head += ",\"sessionStartMs\":";
  head += String((unsigned long)status.sessionStartMs);
  head += ",\"sessionStartCarIndex\":";
  head += String(status.sessionStartCarIndex);
  head += ",\"configAtStart\":";
  head += buildTelemetryConfigSummaryJsonFromSnapshot(snapshot, status.sessionStartCarIndex);
  head += ",\"eventsTruncated\":";
  head += eventsTruncated ? "true" : "false";
  head += ",\"events\":[";
  for (size_t i = 0; i < eventCount; i++) {
    if (i > 0) {
      head += ",";
    }
    appendTelemetryEventJson(head, events[i]);
  }
  head += "],\"samples\":[";
  g_wifiServer->sendContent(head);

  static TelemetrySample batch[64];
  uint32_t afterSeq = 0;
  bool first = true;

  while (true) {
    TelemetryStatus batchStatus;
    bool truncated = false;
    bool hasMore = false;
    size_t copied = telemetryCopySamplesAfter(afterSeq, batch, 64U, &truncated, &hasMore, &batchStatus);
    if (copied == 0) {
      break;
    }

    String chunk;
    chunk.reserve(copied * 128U);
    for (size_t i = 0; i < copied; i++) {
      const TelemetrySample& s = batch[i];
      if (!first) {
        chunk += ",";
      }
      first = false;
      chunk += "{\"seq\":";
      chunk += String((unsigned long)s.seq);
      chunk += ",\"tMs\":";
      chunk += String((unsigned long)s.t_ms);
      chunk += ",\"triggerPct\":";
      chunk += String((unsigned int)s.trigger_pct);
      chunk += ",\"outputPct\":";
      chunk += String((unsigned int)s.output_pct);
      chunk += ",\"vinMv\":";
      chunk += String((unsigned int)s.vin_mV);
      chunk += ",\"currentMa\":";
      chunk += String((unsigned int)s.current_mA);
      chunk += ",\"brakePct\":";
      appendPercentX10JsonValue(chunk, s.brake_pct);
      chunk += ",\"sensiPct\":";
      chunk += String((unsigned int)s.sensi_halfPct);
      chunk += ",\"sensiHalfPct\":";
      chunk += String((unsigned int)s.sensi_halfPct);
      chunk += ",\"carIndex\":";
      chunk += String((unsigned int)s.carIndex);
      chunk += ",\"releaseMode\":";
      chunk += String((unsigned int)s.releaseMode);
      chunk += ",\"flags\":";
      chunk += String((unsigned int)s.flags);
      chunk += "}";
      afterSeq = s.seq;
    }

    g_wifiServer->sendContent(chunk);
    if (!hasMore) {
      break;
    }
  }

  g_wifiServer->sendContent("]}");
}
