#include "ui_text_access.h"
#include "slot_ESC.h"
#include "ui_strings.h"

extern StoredVar_type g_storedVar;

const char* getMenuName(uint8_t lang, uint8_t item) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL) ? MENU_NAMES_PASCAL[lang][item] : MENU_NAMES[lang][item];
}

const char* getSettingsMenuName(uint8_t lang, uint8_t item) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL) ? SETTINGS_MENU_NAMES_PASCAL[lang][item] : SETTINGS_MENU_NAMES[lang][item];
}

const char* getPowerMenuName(uint8_t lang, uint8_t item) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL) ? POWER_MENU_NAMES_PASCAL[lang][item] : POWER_MENU_NAMES[lang][item];
}

const char* getDisplayMenuName(uint8_t lang, uint8_t item) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL) ? DISPLAY_MENU_NAMES_PASCAL[lang][item] : DISPLAY_MENU_NAMES[lang][item];
}

const char* getStepsMenuName(uint8_t lang, uint8_t item) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL) ? STEPS_MENU_NAMES_PASCAL[lang][item] : STEPS_MENU_NAMES[lang][item];
}

const char* getRaceLabel(uint8_t lang, uint8_t param) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL) ? RACE_LABELS_PASCAL[lang][param] : RACE_LABELS[lang][param];
}

const char* getCarMenuOption(uint8_t lang, uint8_t option) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL) ? CAR_MENU_OPTIONS_PASCAL[lang][option] : CAR_MENU_OPTIONS[lang][option];
}

const char* getViewModeLabel(uint8_t lang, uint8_t mode) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL) ? VIEW_MODE_LABELS_PASCAL[lang][mode] : VIEW_MODE_LABELS[lang][mode];
}

const char* getSoundMenuName(uint8_t lang, uint8_t item) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL) ? SOUND_MENU_NAMES_PASCAL[lang][item] : SOUND_MENU_NAMES[lang][item];
}

const char* getOnOffLabel(uint8_t lang, uint8_t state) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL) ? ON_OFF_LABELS_PASCAL[lang][state] : ON_OFF_LABELS[lang][state];
}

const char* getReleaseBrakeModeLabel(uint8_t lang, uint8_t mode) {
  if (mode > RELEASE_BRAKE_DRAG) mode = RELEASE_BRAKE_OFF;
  return (g_storedVar.textCase == TEXT_CASE_PASCAL)
    ? RELEASE_BRAKE_MODE_LABELS_PASCAL[lang][mode]
    : RELEASE_BRAKE_MODE_LABELS[lang][mode];
}

const char* getLockMenuName(uint8_t lang, uint8_t item) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL) ? LOCK_MENU_NAMES_PASCAL[lang][item] : LOCK_MENU_NAMES[lang][item];
}

const char* getLockShortcutLabel(uint8_t lang, uint8_t idx) {
  if (idx > LOCK_SHORTCUT_COUNT) idx = 0;
  return LOCK_SHORTCUT_LABELS[lang][idx];
}

const char* getBackLabel(uint8_t lang) {
  return (g_storedVar.textCase == TEXT_CASE_PASCAL) ? BACK_LABELS_PASCAL[lang] : BACK_LABELS[lang];
}

uint8_t getMenuLines() {
  return (g_storedVar.listFontSize == FONT_SIZE_SMALL) ? 5 : 3;
}
