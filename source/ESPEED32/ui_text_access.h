#ifndef UI_TEXT_ACCESS_H_
#define UI_TEXT_ACCESS_H_

#include <Arduino.h>

const char* getMenuName(uint8_t lang, uint8_t item);
const char* getSettingsMenuName(uint8_t lang, uint8_t item);
const char* getPowerMenuName(uint8_t lang, uint8_t item);
const char* getDisplayMenuName(uint8_t lang, uint8_t item);
const char* getStepsMenuName(uint8_t lang, uint8_t item);
const char* getRaceLabel(uint8_t lang, uint8_t param);
const char* getCarMenuOption(uint8_t lang, uint8_t option);
const char* getViewModeLabel(uint8_t lang, uint8_t mode);
const char* getSoundMenuName(uint8_t lang, uint8_t item);
const char* getOnOffLabel(uint8_t lang, uint8_t state);
const char* getReleaseBrakeModeLabel(uint8_t lang, uint8_t mode);
const char* getLockMenuName(uint8_t lang, uint8_t item);
const char* getLockShortcutLabel(uint8_t lang, uint8_t idx);
const char* getBackLabel(uint8_t lang);
uint8_t getMenuLines();

#endif  /* UI_TEXT_ACCESS_H_ */
