#ifndef EXT_POT_H_
#define EXT_POT_H_

#include <stdint.h>

extern uint16_t g_extPotTarget[];

bool isExtPotEnabled();
bool isExtPotBrakeTarget();
bool isExtPotSensiTarget();
uint16_t getExtPotTarget(uint8_t potIndex);
int8_t getExtPotIndexForTarget(uint16_t target);
void setExtPotTarget(uint8_t potIndex, uint16_t target);
void cycleExtPotTarget(uint8_t potIndex);
void sanitizeExtPotTargets(int8_t preferredPotIndex);
void resetExtPotFilter();
void updateExtPotRuntimeValues();
uint16_t getEffectiveBrakeRaw();
uint16_t getEffectiveBrakePct();
uint16_t getEffectiveSensiRaw();

#endif  /* EXT_POT_H_ */
