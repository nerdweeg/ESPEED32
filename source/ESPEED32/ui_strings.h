#ifndef UI_STRINGS_H_
#define UI_STRINGS_H_

#include <Arduino.h>

/* Menu item names: [language][item] */
extern const char* MENU_NAMES[][12];
extern const char* SETTINGS_MENU_NAMES[][12];
extern const char* LOCK_MENU_NAMES[][4];
extern const char* LOCK_SHORTCUT_LABELS[][11];
extern const char* POWER_MENU_NAMES[][6];
extern const char* DISPLAY_MENU_NAMES[][7];
extern const char* STEPS_MENU_NAMES[][4];
extern const char* RACE_LABELS[][4];
extern const char* CAR_MENU_OPTIONS[][5];
extern const char* VIEW_MODE_LABELS[][3];
extern const char* SOUND_MENU_NAMES[][3];
extern const char* ON_OFF_LABELS[][2];
extern const char* RELEASE_BRAKE_MODE_LABELS[][3];
extern const char* LANG_LABELS[];
extern const char* TEXT_CASE_LABELS[][2];
extern const char* FONT_SIZE_LABELS[][2];

/* Pascal-case variants */
extern const char* MENU_NAMES_PASCAL[][12];
extern const char* SETTINGS_MENU_NAMES_PASCAL[][12];
extern const char* LOCK_MENU_NAMES_PASCAL[][4];
extern const char* POWER_MENU_NAMES_PASCAL[][6];
extern const char* DISPLAY_MENU_NAMES_PASCAL[][7];
extern const char* STEPS_MENU_NAMES_PASCAL[][4];
extern const char* RACE_LABELS_PASCAL[][4];
extern const char* CAR_MENU_OPTIONS_PASCAL[][5];
extern const char* VIEW_MODE_LABELS_PASCAL[][3];
extern const char* SOUND_MENU_NAMES_PASCAL[][3];
extern const char* ON_OFF_LABELS_PASCAL[][2];
extern const char* RELEASE_BRAKE_MODE_LABELS_PASCAL[][3];
extern const char* BACK_LABELS[];
extern const char* BACK_LABELS_PASCAL[];

/* Misc UI text arrays: [language] */
extern const char* STR_SELECT_CAR[];
extern const char* STR_COPY_FROM[];
extern const char* STR_COPY_TO[];
extern const char* STR_ALL[];
extern const char* STR_COPIED_ALL[];
extern const char* STR_COPIED[];
extern const char* STR_RENAME_CAR[];
extern const char* STR_CONFIRM[];
extern const char* STR_LIMITER[];
extern const char* STR_CALIBRATION[];
extern const char* STR_PRESS_RELEASE[];
extern const char* STR_PUSH_DONE[];

#endif  /* UI_STRINGS_H_ */
