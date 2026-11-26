#ifndef POWER_UI_H_
#define POWER_UI_H_

#include <stdint.h>

bool refreshIdleInteractionFromControls(volatile uint32_t* lastInteraction, bool* screensaverActive, uint16_t* lastEncoderPos);
bool serviceIdlePowerTransitions(uint32_t* lastInteraction, bool* screensaverActive);
bool consumeScreensaverWakeInput(bool wakeTriggered);
void showPowerSave();
void showPowerSave(uint32_t inactivityStartMs);
void showDeepSleep();

#endif  /* POWER_UI_H_ */
