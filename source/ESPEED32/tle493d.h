#ifndef TLE493D_H_
#define TLE493D_H_

/*********************************************************************************************************************/
/*                                                   Includes                                                        */
/*********************************************************************************************************************/
#include "HAL.h"

#ifdef TLE493D_MAG

#include <Wire.h>

/* Datasheet tAPC is in the sub-millisecond range; keep a small margin plus retries. */
static constexpr uint16_t TLE493D_I2C_STABILIZE_MS = 2;

enum class TLE493DVariant : uint8_t {
  NONE = 0,
  W2B6,
  W2B6_A0,
  P3B6
};

extern TLE493DVariant g_tleVariant;
extern uint8_t g_tleAddress;
extern uint8_t g_tleOverrideMode;
extern int16_t g_tleLastAngle;
extern bool g_tleLastAngleValid;

bool TLE493D_ReadFrame(uint8_t address, uint8_t* data, uint8_t bytes);
int16_t TLE493D_ComputeAngle10(int16_t x, int16_t y);
uint8_t TLE493D_NormalizeOverrideMode(uint16_t mode);
uint8_t TLE493D_LoadOverrideMode();
void TLE493D_SaveOverrideMode(uint16_t mode);
void TLE493D_ClearStoredConfig(bool clearMode);
bool TLE493D_ApplyMode(uint8_t overrideMode);

/* Fault tracking: call TLE493D_NoteReadOutcome() after every TLE493D_ReadFrame()
 * attempt. A single failed read is expected occasionally and not a fault; only a
 * long run of consecutive failures (i.e. the sensor has actually stopped
 * responding, not just missed one frame) is reported as a sustained fault. */
void TLE493D_NoteReadOutcome(bool success);
bool TLE493D_HasSustainedFault();

#endif  /* TLE493D_MAG */

#endif  /* TLE493D_H_ */
