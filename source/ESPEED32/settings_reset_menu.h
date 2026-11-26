#ifndef SETTINGS_RESET_MENU_H_
#define SETTINGS_RESET_MENU_H_

#include <stdint.h>

void showResetSubmenu();
bool showResetConfirmDialog(const char* label);
void resetAllCarsToFactoryDefaults();
const char* getResetDoneText(uint16_t lang);

#endif  /* SETTINGS_RESET_MENU_H_ */
