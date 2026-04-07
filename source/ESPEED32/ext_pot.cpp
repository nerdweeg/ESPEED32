#include "ext_pot.h"
#include <Arduino.h>
#include "slot_ESC.h"
#include "HAL.h"

extern StoredVar_type g_storedVar;
extern ESC_type g_escVar;
extern uint16_t g_carSel;

uint16_t g_extPotTarget[EXT_POT_COUNT] = {
  EXT_POT1_TARGET_DEFAULT,
  EXT_POT2_TARGET_DEFAULT
};

static uint32_t g_extPotFilteredRaw[EXT_POT_COUNT] = {0, 0};
static bool g_extPotFilterInit[EXT_POT_COUNT] = {false, false};

static uint16_t clampExtPotTarget(uint16_t target) {
  return constrain(target, EXT_POT_TARGET_MIN, EXT_POT_TARGET_MAX);
}

static uint16_t readExtPotRaw(uint8_t potIndex) {
  return (potIndex == 0) ? HAL_ReadExternalPot1Raw() : HAL_ReadExternalPot2Raw();
}

static uint16_t readExtPotFilteredRaw(uint8_t potIndex) {
  if (potIndex >= EXT_POT_COUNT) return 0;

  uint16_t raw = readExtPotRaw(potIndex);
  if (!g_extPotFilterInit[potIndex]) {
    g_extPotFilteredRaw[potIndex] = raw;
    g_extPotFilterInit[potIndex] = true;
  } else {
    g_extPotFilteredRaw[potIndex] = ((g_extPotFilteredRaw[potIndex] * 7UL) + raw) / 8UL;
  }
  return (uint16_t)g_extPotFilteredRaw[potIndex];
}

static uint16_t mapExtPotToBrakeRaw(uint16_t raw) {
  return (uint16_t)map(raw, 0, ACD_RESOLUTION_STEPS, 0, BRAKE_MAX_VALUE);
}

static uint16_t mapExtPotToSensiRaw(uint16_t raw) {
  uint16_t maxSensiRaw = min((uint16_t)MIN_SPEED_MAX_VALUE,
                             (uint16_t)(g_storedVar.carParam[g_carSel].maxSpeed * SENSI_SCALE));
  return (uint16_t)map(raw, 0, ACD_RESOLUTION_STEPS, 0, maxSensiRaw);
}

uint16_t getExtPotTarget(uint8_t potIndex) {
  if (potIndex >= EXT_POT_COUNT) return EXT_POT_TARGET_OFF;
  return g_extPotTarget[potIndex];
}

int8_t getExtPotIndexForTarget(uint16_t target) {
  for (uint8_t i = 0; i < EXT_POT_COUNT; i++) {
    if (g_extPotTarget[i] == target) return (int8_t)i;
  }
  return -1;
}

void sanitizeExtPotTargets(int8_t preferredPotIndex) {
  for (uint8_t i = 0; i < EXT_POT_COUNT; i++) {
    g_extPotTarget[i] = clampExtPotTarget(g_extPotTarget[i]);
  }

  if (g_extPotTarget[0] == EXT_POT_TARGET_OFF || g_extPotTarget[0] != g_extPotTarget[1]) {
    return;
  }

  if (preferredPotIndex == 1) {
    g_extPotTarget[0] = EXT_POT_TARGET_OFF;
  } else {
    g_extPotTarget[1] = EXT_POT_TARGET_OFF;
  }
}

void setExtPotTarget(uint8_t potIndex, uint16_t target) {
  if (potIndex >= EXT_POT_COUNT) return;

  g_extPotTarget[potIndex] = clampExtPotTarget(target);
  sanitizeExtPotTargets((int8_t)potIndex);
  resetExtPotFilter();
}

void cycleExtPotTarget(uint8_t potIndex) {
  if (potIndex >= EXT_POT_COUNT) return;

  uint16_t nextTarget = g_extPotTarget[potIndex] + 1;
  if (nextTarget > EXT_POT_TARGET_MAX) nextTarget = EXT_POT_TARGET_OFF;
  setExtPotTarget(potIndex, nextTarget);
}

bool isExtPotEnabled() {
  return isExtPotBrakeTarget() || isExtPotSensiTarget();
}

bool isExtPotBrakeTarget() {
  for (uint8_t i = 0; i < EXT_POT_COUNT; i++) {
    if (g_extPotTarget[i] == EXT_POT_TARGET_BRAKE) return true;
  }
  return false;
}

bool isExtPotSensiTarget() {
  for (uint8_t i = 0; i < EXT_POT_COUNT; i++) {
    if (g_extPotTarget[i] == EXT_POT_TARGET_SENSI) return true;
  }
  return false;
}

void resetExtPotFilter() {
  for (uint8_t i = 0; i < EXT_POT_COUNT; i++) {
    g_extPotFilterInit[i] = false;
    g_extPotFilteredRaw[i] = 0;
  }
}

void updateExtPotRuntimeValues() {
  g_escVar.effectiveBrake_raw = g_storedVar.carParam[g_carSel].brake;
  g_escVar.effectiveSensi_raw = g_storedVar.carParam[g_carSel].minSpeed;

  for (uint8_t i = 0; i < EXT_POT_COUNT; i++) {
    uint16_t filteredRaw = readExtPotFilteredRaw(i);
    if (g_extPotTarget[i] == EXT_POT_TARGET_BRAKE) {
      g_escVar.effectiveBrake_raw = constrain(mapExtPotToBrakeRaw(filteredRaw), 0, BRAKE_MAX_VALUE);
    } else if (g_extPotTarget[i] == EXT_POT_TARGET_SENSI) {
      g_escVar.effectiveSensi_raw = constrain(mapExtPotToSensiRaw(filteredRaw), 0,
                                              min((uint16_t)MIN_SPEED_MAX_VALUE, (uint16_t)(g_storedVar.carParam[g_carSel].maxSpeed * SENSI_SCALE)));
    }
  }
}

uint16_t getEffectiveBrakeRaw() {
  return isExtPotBrakeTarget() ? g_escVar.effectiveBrake_raw : g_storedVar.carParam[g_carSel].brake;
}

uint16_t getEffectiveBrakePct() {
  return brakeToWholePctRounded(getEffectiveBrakeRaw());
}

uint16_t getEffectiveSensiRaw() {
  return isExtPotSensiTarget() ? g_escVar.effectiveSensi_raw : g_storedVar.carParam[g_carSel].minSpeed;
}
