#ifndef APP_GLOBALS_H_
#define APP_GLOBALS_H_

/* Globals and app-level functions defined in ESPEED32.ino.
 * Include this instead of re-declaring them with local `extern` lines, so the
 * compiler checks every use against one declaration. */

#include <Arduino.h>
#include "slot_ESC.h"

/* FreeRTOS tasks */
extern TaskHandle_t Task2;

/* State machine and runtime state */
extern StateMachine_enum g_currState;
extern StoredVar_type g_storedVar;
extern ESC_type g_escVar;
extern uint16_t g_carSel;
extern uint32_t g_lastEncoderInteraction;
extern bool g_forceRaceRedraw;
extern portMUX_TYPE g_lapStatsMux;  /* Guards lap stats in g_escVar across cores */

/* Display, encoder and storage handles */
extern OBDISP g_obd;
extern AiEsp32RotaryEncoder g_rotaryEncoder;
extern Preferences g_pref;
extern char msgStr[50];  /* Shared display message buffer */
#ifdef USE_BACKBUFFER
extern uint8_t ucBackBuffer[1024];
#else
extern uint8_t *ucBackBuffer;
#endif

/* Menus */
extern Menu_type g_mainMenu;
extern Menu_type g_settingsMenu;
extern Menu_type g_carMenu;
extern uint8_t g_encoderMainSelector;

/* Global settings stored outside StoredVar_type */
extern uint16_t g_statsEnabled;
extern uint16_t g_antiSpinStepMs;
extern uint16_t g_antiSpinStepPct;
extern uint16_t g_antiSpinDisplayMode;
extern uint16_t g_brakeStep;
extern uint16_t g_sensiStep;
extern uint16_t g_advancedMenuEnabled;
extern uint16_t g_encoderInvertEnabled;
extern uint16_t g_pwmFreqMaxProfile;

/* Storage */
void saveEEPROM(const StoredVar_type& toSave);

/* Menu navigation */
bool checkRaceModeEscape();
void requestEscapeToMain();
bool isEscapeToMainRequested();
void setInSettingsMenu(bool active);
void setLastEncoderInteraction(uint32_t interactionMs);
uint8_t getMainMenuSelector();
uint8_t getMainMenuItemsCount();
bool isSettingsLocked();
void toggleSettingsLock();

/* Timed WiFi portal and telemetry logging */
void serviceTimedWiFiPortal();
void startTimedWiFiPortal(uint16_t minutes);
void stopTimedWiFiPortal();
uint16_t getWiFiTimedMinutes();
void setWiFiTimedMinutes(uint16_t minutes);
void startTimedTelemetryLogging(uint16_t minutes);
void stopTimedTelemetryLogging();
uint16_t getTelemetryTimedMinutes();
void setTelemetryTimedMinutes(uint16_t minutes);

#endif  /* APP_GLOBALS_H_ */
