#include "ui_strings.h"

/* Menu item names in different languages: [language][item] */
const char* MENU_NAMES[][12] = {
  /* NOR */ {"BREMS", "SENSI", "ANTIS", "KURVE", "FADE", "PWM_F", "BREMS+", "STROM", "INNSTILL", "STATS", "LAS", "*BIL*"},
  /* ENG */ {"BRAKE", "SENSI", "ANTIS", "CURVE", "FADE", "PWM_F", "BRAKE+", "LIMIT", "SETTINGS", "STATS", "LOCK", "*CAR*"},
  /* CS  */ {"BRAKE", "ATTACK", "CHOKE2", "PROFIL", "FADE", "PWM_F", "BRAKE+", "CHOKE1", "SETTINGS", "STATS", "LOCK", "*CAR*"},
  /* ACD */ {"BRAKE", "SENSI", "ANTIS", "CURVE", "FADE", "PWM_F", "BRAKE+", "CHOKE", "SETTINGS", "STATS", "LOCK", "*CAR*"},
  /* ESP */ {"FRENO", "SENSI", "ANTIS", "CURVA", "FADE", "PWM_F", "FRENO+", "LIMITE", "AJUSTES", "STATS", "BLOQUEO", "*AUTO*"},
  /* DEU */ {"BREMSE", "SENSI", "ANTIS", "KURVE", "FADE", "PWM_F", "BREMSE+", "LIMIT", "SETUP", "STATS", "SPERRE", "*AUTO*"},
  /* ITA */ {"FRENO", "SENSI", "ANTIS", "CURVA", "FADE", "PWM_F", "FRENO+", "LIMITE", "SETUP", "STATS", "BLOCCO", "*AUTO*"},
  /* NLD */ {"REM", "SENSI", "ANTIS", "CURVE", "FADE", "PWM_F", "REM+", "LIMIET", "INSTELL", "STATS", "VERGR", "*AUTO*"},
  /* POR */ {"TRAVAO", "SENSI", "ANTIS", "CURVA", "FADE", "PWM_F", "TRAVAO+", "LIMITE", "AJUSTES", "STATS", "BLOQ", "*CARRO*"}
};

/* Settings menu item names: [language][item] */
const char* SETTINGS_MENU_NAMES[][12] = {
  /* NOR */ {"STROM", "SKJERM", "LYD", "HARDWARE", "STATS", "WIFI", "LOGGING", "LAS", "USB INFO", "NULLSTILL", "INFO", "TILBAKE"},
  /* ENG */ {"POWER", "DISPLAY", "SOUND", "HARDWARE", "STATS", "WIFI", "LOGGING", "LOCK", "USB INFO", "RESET", "ABOUT", "BACK"},
  /* CS  */ {"POWER", "DISPLAY", "SOUND", "HARDWARE", "STATS", "WIFI", "LOGGING", "LOCK", "USB INFO", "RESET", "ABOUT", "BACK"},
  /* ACD */ {"POWER", "DISPLAY", "SOUND", "HARDWARE", "STATS", "WIFI", "LOGGING", "LOCK", "USB INFO", "RESET", "ABOUT", "BACK"},
  /* ESP */ {"ENERGIA", "PANTALLA", "SONIDO", "HARDWARE", "STATS", "WIFI", "LOGGING", "BLOQUEO", "USB INFO", "RESET", "ACERCA", "ATRAS"},
  /* DEU */ {"STROM", "ANZEIGE", "TON", "HARDWARE", "STATS", "WIFI", "LOGGING", "SPERRE", "USB INFO", "RESET", "INFO", "ZURUCK"},
  /* ITA */ {"POTENZA", "SCHERMO", "SUONO", "HARDWARE", "STATS", "WIFI", "LOGGING", "BLOCCO", "USB INFO", "RESET", "INFO", "INDIETRO"},
  /* NLD */ {"STROOM", "DISPLAY", "GELUID", "HARDWARE", "STATS", "WIFI", "LOGGING", "VERGR", "USB INFO", "RESET", "INFO", "TERUG"},
  /* POR */ {"ENERGIA", "ECRA", "SOM", "HARDWARE", "STATS", "WIFI", "LOGGING", "BLOQ", "USB INFO", "RESET", "SOBRE", "VOLTAR"}
};

/* Power submenu item names: [language][item] */
const char* POWER_MENU_NAMES[][6] = {
  /* NOR */ {"SKJSP", "DVALE", "DYP SOV", "OPPSTART", "VIN KAL.", "TILBAKE"},
  /* ENG */ {"SCRSV", "SLEEP", "DEEP SLEEP", "STARTUP", "VIN CAL.", "BACK"},
  /* CS  */ {"SCRSV", "SLEEP", "DEEP SLEEP", "STARTUP", "VIN CAL.", "BACK"},
  /* ACD */ {"SCRSV", "SLEEP", "DEEP SLEEP", "STARTUP", "VIN CAL.", "BACK"},
  /* ESP */ {"SCRSV", "REPOSO", "SUSP PROF", "ARRANQUE", "VIN CAL.", "ATRAS"},
  /* DEU */ {"SCRSV", "SCHLAF", "TIEF SCHL", "START", "VIN KAL.", "ZURUCK"},
  /* ITA */ {"SCRSV", "RIPOSO", "SONNO PROF", "AVVIO", "VIN CAL.", "INDIETRO"},
  /* NLD */ {"SCRSV", "SLAPEN", "DIEP SLP", "START", "VIN KAL.", "TERUG"},
  /* POR */ {"SCRSV", "DORMIR", "SONO PROF", "ARRANQUE", "VIN CAL.", "VOLTAR"}
};

/* Display submenu item names: [language][item] */
const char* DISPLAY_MENU_NAMES[][7] = {
  /* NOR */ {"RACEMODUS", "SPRAK", "STYL", "SKRIFTSTRL", "STEG", "STATUSLINJE", "TILBAKE"},
  /* ENG */ {"RACE MODE", "LANGUAGE", "CASE", "FONT SIZE", "STEPS", "STATUS BAR", "BACK"},
  /* CS  */ {"RACE MODE", "LANGUAGE", "CASE", "FONT SIZE", "STEPS", "STATUS BAR", "BACK"},
  /* ACD */ {"RACE MODE", "LANGUAGE", "CASE", "FONT SIZE", "STEPS", "STATUS BAR", "BACK"},
  /* ESP */ {"MODO RACE", "IDIOMA", "ESTILO", "TAM TEXTO", "PASOS", "BARRA EST", "ATRAS"},
  /* DEU */ {"RACE MODUS", "SPRACHE", "STIL", "SCHRIFT", "SCHRITTE", "STATUSLEISTE", "ZURUCK"},
  /* ITA */ {"MODO GARA", "LINGUA", "STILE", "DIM TESTO", "PASSI", "BARRA STATO", "INDIETRO"},
  /* NLD */ {"RACE MODUS", "TAAL", "STIJL", "TEKSTGRT", "STAPPEN", "STATUSBALK", "TERUG"},
  /* POR */ {"MODO RACE", "IDIOMA", "ESTILO", "TAM TEXTO", "PASSOS", "BARRA EST", "VOLTAR"}
};

/* Steps submenu item names: [language][item] */
const char* STEPS_MENU_NAMES[][4] = {
  /* NOR */ {"ANTISPINN", "BREMSSTEG", "SENSISTEG", "TILBAKE"},
  /* ENG */ {"ANTISPIN", "BRAKE STEP", "SENSI STEP", "BACK"},
  /* CS  */ {"ANTISPIN", "BRAKE STEP", "SENSI STEP", "BACK"},
  /* ACD */ {"ANTISPIN", "BRAKE STEP", "SENSI STEP", "BACK"},
  /* ESP */ {"ANTISPIN", "PASO FRENO", "PASO SENSI", "ATRAS"},
  /* DEU */ {"ANTISPIN", "BRK SCHRITT", "SEN SCHRITT", "ZURUCK"},
  /* ITA */ {"ANTISPIN", "PASSO FRENO", "PASSO SENSI", "INDIETRO"},
  /* NLD */ {"ANTISPIN", "STAP REM", "STAP SENSI", "TERUG"},
  /* POR */ {"ANTISPIN", "PASSO FREIO", "PASSO SENSI", "VOLTAR"}
};

/* Race mode parameter labels: [language][param] */
const char* RACE_LABELS[][4] = {
  /* NOR */ {"BREMS", "SENSI", "ANTIS", "KURVE"},
  /* ENG */ {"BRAKE", "SENSI", "ANTIS", "CURVE"},
  /* CS  */ {"BRAKE", "ATTACK", "CHOKE2", "PROFIL"},
  /* ACD */ {"BRAKE", "SENSI", "ANTIS", "CURVE"},
  /* ESP */ {"FRENO", "SENSI", "ANTIS", "CURVA"},
  /* DEU */ {"BREMSE", "SENSI", "ANTIS", "KURVE"},
  /* ITA */ {"FRENO", "SENSI", "ANTIS", "CURVA"},
  /* NLD */ {"REM", "SENSI", "ANTIS", "CURVE"},
  /* POR */ {"TRAVAO", "SENSI", "ANTIS", "CURVA"}
};

/* Car menu option labels: [language][option] */
const char* CAR_MENU_OPTIONS[][5] = {
  /* NOR */ {"VELG", "NAVNGI", "RACESWP", "KOPIER", "NULLSTILL"},
  /* ENG */ {"SELECT", "RENAME", "RACESWP", "COPY", "RESET"},
  /* CS  */ {"SELECT", "RENAME", "RACESWP", "COPY", "RESET"},
  /* ACD */ {"SELECT", "RENAME", "RACESWP", "COPY", "RESET"},
  /* ESP */ {"ELEGIR", "RENOMB", "RACESWP", "COPIAR", "RESET"},
  /* DEU */ {"WAHL", "NAME", "RACESWP", "KOPIER", "RESET"},
  /* ITA */ {"SCEGLI", "RINOM", "RACESWP", "COPIA", "RESET"},
  /* NLD */ {"KIES", "HERNOEM", "RACESWP", "KOPIE", "RESET"},
  /* POR */ {"ESCOLH", "RENOME", "RACESWP", "COPIAR", "RESET"}
};

/* View mode value labels: [language][mode] */
const char* VIEW_MODE_LABELS[][3] = {
  /* NOR */ {"AV", "FULL", "ENKEL"},
  /* ENG */ {"OFF", "FULL", "SIMPLE"},
  /* CS  */ {"OFF", "FULL", "SIMPLE"},
  /* ACD */ {"OFF", "FULL", "SIMPLE"},
  /* ESP */ {"OFF", "TOTAL", "SIMPLE"},
  /* DEU */ {"AUS", "VOLL", "EINF"},
  /* ITA */ {"OFF", "PIENO", "SEMPL"},
  /* NLD */ {"UIT", "VOL", "EENV"},
  /* POR */ {"OFF", "TOTAL", "SIMP"}
};

/* Sound submenu item names: [language][item] - BOOT, RACE, BACK */
const char* SOUND_MENU_NAMES[][3] = {
  /* NOR */ {"OPPSTART", "RACEMODE", "TILBAKE"},
  /* ENG */ {"BOOT", "RACE", "BACK"},
  /* CS  */ {"BOOT", "RACE", "BACK"},
  /* ACD */ {"BOOT", "RACE", "BACK"},
  /* ESP */ {"INICIO", "CARRERA", "ATRAS"},
  /* DEU */ {"START", "RENNEN", "ZURUCK"},
  /* ITA */ {"AVVIO", "GARA", "INDIETRO"},
  /* NLD */ {"START", "RACE", "TERUG"},
  /* POR */ {"ARRANQUE", "CORRIDA", "VOLTAR"}
};

/* Lock submenu item names: [language][item] - MENU ITEM, SHORTCUT, BACK */
const char* LOCK_MENU_NAMES[][4] = {
  /* NOR */ {"MENYVALG", "SNARVEI", "BEKREFT", "TILBAKE"},
  /* ENG */ {"MENU ITEM", "SHORTCUT", "CONFIRM", "BACK"},
  /* CS  */ {"MENU ITEM", "SHORTCUT", "CONFIRM", "BACK"},
  /* ACD */ {"MENU ITEM", "SHORTCUT", "CONFIRM", "BACK"},
  /* ESP */ {"MENU", "ATAJO", "CONFIRM", "ATRAS"},
  /* DEU */ {"MENUPUNKT", "KUERZEL", "BESTAET", "ZURUCK"},
  /* ITA */ {"VOCE MENU", "SCORCIAT", "CONFIRM", "INDIETRO"},
  /* NLD */ {"MENU ITEM", "SNELKOP", "BEVESTIG", "TERUG"},
  /* POR */ {"ITEM MENU", "ATALHO", "CONFIRM", "VOLTAR"}
};

/* Lock submenu item names - Pascal Case: [language][item] */
const char* LOCK_MENU_NAMES_PASCAL[][4] = {
  /* NOR */ {"Menyvalg", "Snarvei", "Bekreft", "Tilbake"},
  /* ENG */ {"Menu Item", "Shortcut", "Confirm", "Back"},
  /* CS  */ {"Menu Item", "Shortcut", "Confirm", "Back"},
  /* ACD */ {"Menu Item", "Shortcut", "Confirm", "Back"},
  /* ESP */ {"Menu", "Atajo", "Confirm", "Atras"},
  /* DEU */ {"Menupunkt", "Kuerzel", "Bestaet", "Zuruck"},
  /* ITA */ {"Voce Menu", "Scorciat", "Confirm", "Indietro"},
  /* NLD */ {"Menu Item", "Snelkop", "Bevestig", "Terug"},
  /* POR */ {"Item Menu", "Atalho", "Confirm", "Voltar"}
};

/* Lock shortcut duration labels: [language][idx] - OFF=0, 1s=1, 2s=2, …, 10s=10 */
const char* LOCK_SHORTCUT_LABELS[][11] = {
  /* NOR */ {"AV", "1s", "2s", "3s", "4s", "5s", "6s", "7s", "8s", "9s", "10s"},
  /* ENG */ {"OFF", "1s", "2s", "3s", "4s", "5s", "6s", "7s", "8s", "9s", "10s"},
  /* CS  */ {"OFF", "1s", "2s", "3s", "4s", "5s", "6s", "7s", "8s", "9s", "10s"},
  /* ACD */ {"OFF", "1s", "2s", "3s", "4s", "5s", "6s", "7s", "8s", "9s", "10s"},
  /* ESP */ {"OFF", "1s", "2s", "3s", "4s", "5s", "6s", "7s", "8s", "9s", "10s"},
  /* DEU */ {"AUS", "1s", "2s", "3s", "4s", "5s", "6s", "7s", "8s", "9s", "10s"},
  /* ITA */ {"OFF", "1s", "2s", "3s", "4s", "5s", "6s", "7s", "8s", "9s", "10s"},
  /* NLD */ {"UIT", "1s", "2s", "3s", "4s", "5s", "6s", "7s", "8s", "9s", "10s"},
  /* POR */ {"OFF", "1s", "2s", "3s", "4s", "5s", "6s", "7s", "8s", "9s", "10s"}
};

/* ON/OFF labels: [language][state] */
const char* ON_OFF_LABELS[][2] = {
  /* NOR */ {"AV", "PA"},
  /* ENG */ {"OFF", "ON"},
  /* CS  */ {"OFF", "ON"},
  /* ACD */ {"OFF", "ON"},
  /* ESP */ {"OFF", "ON"},
  /* DEU */ {"AUS", "EIN"},
  /* ITA */ {"OFF", "ON"},
  /* NLD */ {"UIT", "AAN"},
  /* POR */ {"OFF", "ON"}
};

/* Release-brake mode labels: [language][mode] */
const char* RELEASE_BRAKE_MODE_LABELS[][3] = {
  /* NOR */ {"AV", "QCK", "DRG"},
  /* ENG */ {"OFF", "QCK", "DRG"},
  /* CS  */ {"OFF", "QCK", "DRG"},
  /* ACD */ {"OFF", "QCK", "DRG"},
  /* ESP */ {"OFF", "QCK", "DRG"},
  /* DEU */ {"AUS", "QCK", "DRG"},
  /* ITA */ {"OFF", "QCK", "DRG"},
  /* NLD */ {"UIT", "QCK", "DRG"},
  /* POR */ {"OFF", "QCK", "DRG"}
};

/* Language labels: [language_code] */
const char* LANG_LABELS[] = {"NOR", "ENG", "CS", "ACD", "ESP", "DEU", "ITA", "NLD", "POR"};

/* Text case style labels: [language][case_style] */
const char* TEXT_CASE_LABELS[][2] = {
  /* NOR */ {"UPPER", "Pascal"},
  /* ENG */ {"UPPER", "Pascal"},
  /* CS  */ {"UPPER", "Pascal"},
  /* ACD */ {"UPPER", "Pascal"},
  /* ESP */ {"MAYUS", "Pascal"},
  /* DEU */ {"GROSS", "Pascal"},
  /* ITA */ {"MAIUS", "Pascal"},
  /* NLD */ {"HOOFD", "Pascal"},
  /* POR */ {"MAIUS", "Pascal"}
};

/* Font size labels: [language][size] */
const char* FONT_SIZE_LABELS[][2] = {
  /* NOR */ {"STOR", "liten"},
  /* ENG */ {"LARGE", "small"},
  /* CS  */ {"LARGE", "small"},
  /* ACD */ {"LARGE", "small"},
  /* ESP */ {"GRAND", "peq."},
  /* DEU */ {"GROSS", "klein"},
  /* ITA */ {"GRAND", "picc."},
  /* NLD */ {"GROOT", "klein"},
  /* POR */ {"GRAND", "peq."}
};

/* Menu item names - Pascal Case: [language][item] */
const char* MENU_NAMES_PASCAL[][12] = {
  /* NOR */ {"Brems", "Sensi", "Antis", "Kurve", "Fade", "Pwm_F", "Brems+", "Strom", "Innstill", "Stats", "Las", "*Bil*"},
  /* ENG */ {"Brake", "Sensi", "Antis", "Curve", "Fade", "Pwm_F", "Brake+", "Limit", "Settings", "Stats", "Lock", "*Car*"},
  /* CS  */ {"Brake", "Attack", "Choke2", "Profil", "Fade", "Pwm_F", "Brake+", "Choke1", "Settings", "Stats", "Lock", "*Car*"},
  /* ACD */ {"Brake", "Sensi", "Antis", "Curve", "Fade", "Pwm_F", "Brake+", "Choke", "Settings", "Stats", "Lock", "*Car*"},
  /* ESP */ {"Freno", "Sensi", "Antis", "Curva", "Fade", "Pwm_F", "Freno+", "Limite", "Ajustes", "Stats", "Bloqueo", "*Auto*"},
  /* DEU */ {"Bremse", "Sensi", "Antis", "Kurve", "Fade", "Pwm_F", "Bremse+", "Limit", "Setup", "Stats", "Sperre", "*Auto*"},
  /* ITA */ {"Freno", "Sensi", "Antis", "Curva", "Fade", "Pwm_F", "Freno+", "Limite", "Setup", "Stats", "Blocco", "*Auto*"},
  /* NLD */ {"Rem", "Sensi", "Antis", "Curve", "Fade", "Pwm_F", "Rem+", "Limiet", "Instell", "Stats", "Vergr", "*Auto*"},
  /* POR */ {"Travao", "Sensi", "Antis", "Curva", "Fade", "Pwm_F", "Travao+", "Limite", "Ajustes", "Stats", "Bloq", "*Carro*"}
};

/* Settings menu item names - Pascal Case: [language][item] */
const char* SETTINGS_MENU_NAMES_PASCAL[][12] = {
  /* NOR */ {"Strom", "Skjerm", "Lyd", "Hardware", "Stats", "Wifi", "Logging", "Las", "Usb info", "Nullstill", "Info", "Tilbake"},
  /* ENG */ {"Power", "Display", "Sound", "Hardware", "Stats", "Wifi", "Logging", "Lock", "Usb info", "Reset", "About", "Back"},
  /* CS  */ {"Power", "Display", "Sound", "Hardware", "Stats", "Wifi", "Logging", "Lock", "Usb info", "Reset", "About", "Back"},
  /* ACD */ {"Power", "Display", "Sound", "Hardware", "Stats", "Wifi", "Logging", "Lock", "Usb info", "Reset", "About", "Back"},
  /* ESP */ {"Energia", "Pantalla", "Sonido", "Hardware", "Stats", "Wifi", "Logging", "Bloqueo", "Usb info", "Reset", "Acerca", "Atras"},
  /* DEU */ {"Strom", "Anzeige", "Ton", "Hardware", "Stats", "Wifi", "Logging", "Sperre", "Usb info", "Reset", "Info", "Zuruck"},
  /* ITA */ {"Potenza", "Schermo", "Suono", "Hardware", "Stats", "Wifi", "Logging", "Blocco", "Usb info", "Reset", "Info", "Indietro"},
  /* NLD */ {"Stroom", "Display", "Geluid", "Hardware", "Stats", "Wifi", "Logging", "Vergr", "Usb info", "Reset", "Info", "Terug"},
  /* POR */ {"Energia", "Ecra", "Som", "Hardware", "Stats", "Wifi", "Logging", "Bloq", "Usb info", "Reset", "Sobre", "Voltar"}
};

/* Power submenu item names - Pascal Case: [language][item] */
const char* POWER_MENU_NAMES_PASCAL[][6] = {
  /* NOR */ {"Skjsp", "Dvale", "Dyp Sov", "Oppstart", "Vin kal.", "Tilbake"},
  /* ENG */ {"Scrsv", "Sleep", "Deep Sleep", "Startup", "Vin Cal.", "Back"},
  /* CS  */ {"Scrsv", "Sleep", "Deep Sleep", "Startup", "Vin Cal.", "Back"},
  /* ACD */ {"Scrsv", "Sleep", "Deep Sleep", "Startup", "Vin Cal.", "Back"},
  /* ESP */ {"Scrsv", "Reposo", "Susp Prof", "Arranque", "Vin Cal.", "Atras"},
  /* DEU */ {"Scrsv", "Schlaf", "Tief Schl", "Start", "Vin Kal.", "Zuruck"},
  /* ITA */ {"Scrsv", "Riposo", "Sonno Prof", "Avvio", "Vin Cal.", "Indietro"},
  /* NLD */ {"Scrsv", "Slapen", "Diep Slp", "Start", "Vin kal.", "Terug"},
  /* POR */ {"Scrsv", "Dormir", "Sono Prof", "Arranque", "Vin Cal.", "Voltar"}
};

/* Display submenu item names - Pascal Case: [language][item] */
const char* DISPLAY_MENU_NAMES_PASCAL[][7] = {
  /* NOR */ {"Racemodus", "Sprak", "Styl", "Skriftstrl", "Steg", "Statuslinje", "Tilbake"},
  /* ENG */ {"Race Mode", "Language", "Case", "Font Size", "Steps", "Status bar", "Back"},
  /* CS  */ {"Race Mode", "Language", "Case", "Font Size", "Steps", "Status bar", "Back"},
  /* ACD */ {"Race Mode", "Language", "Case", "Font Size", "Steps", "Status bar", "Back"},
  /* ESP */ {"Modo Race", "Idioma", "Estilo", "Tam texto", "Pasos", "Barra est", "Atras"},
  /* DEU */ {"Race Modus", "Sprache", "Stil", "Schrift", "Schritte", "Statusleiste", "Zuruck"},
  /* ITA */ {"Modo Gara", "Lingua", "Stile", "Dim testo", "Passi", "Barra stato", "Indietro"},
  /* NLD */ {"Race Modus", "Taal", "Stijl", "Tekstgrt", "Stappen", "Statusbalk", "Terug"},
  /* POR */ {"Modo Race", "Idioma", "Estilo", "Tam texto", "Passos", "Barra est", "Voltar"}
};

/* Steps submenu item names - Pascal Case: [language][item] */
const char* STEPS_MENU_NAMES_PASCAL[][4] = {
  /* NOR */ {"Antispinn", "Bremssteg", "Sensisteg", "Tilbake"},
  /* ENG */ {"Antispin", "Brake step", "Sensi step", "Back"},
  /* CS  */ {"Antispin", "Brake step", "Sensi step", "Back"},
  /* ACD */ {"Antispin", "Brake step", "Sensi step", "Back"},
  /* ESP */ {"Antispin", "Paso freno", "Paso sensi", "Atras"},
  /* DEU */ {"Antispin", "Brk schritt", "Sen schritt", "Zuruck"},
  /* ITA */ {"Antispin", "Passo freno", "Passo sensi", "Indietro"},
  /* NLD */ {"Antispin", "Stap rem", "Stap sensi", "Terug"},
  /* POR */ {"Antispin", "Passo freio", "Passo sensi", "Voltar"}
};

/* Race mode parameter labels - Pascal Case: [language][param] */
const char* RACE_LABELS_PASCAL[][4] = {
  /* NOR */ {"Brems", "Sensi", "Antis", "Kurve"},
  /* ENG */ {"Brake", "Sensi", "Antis", "Curve"},
  /* CS  */ {"Brake", "Attack", "Choke2", "Profil"},
  /* ACD */ {"Brake", "Sensi", "Antis", "Curve"},
  /* ESP */ {"Freno", "Sensi", "Antis", "Curva"},
  /* DEU */ {"Bremse", "Sensi", "Antis", "Kurve"},
  /* ITA */ {"Freno", "Sensi", "Antis", "Curva"},
  /* NLD */ {"Rem", "Sensi", "Antis", "Curve"},
  /* POR */ {"Travao", "Sensi", "Antis", "Curva"}
};

/* Car menu option labels - Pascal Case: [language][option] */
const char* CAR_MENU_OPTIONS_PASCAL[][5] = {
  /* NOR */ {"Velg", "Navngi", "Raceswp", "Kopier", "Nullstill"},
  /* ENG */ {"Select", "Rename", "Raceswp", "Copy", "Reset"},
  /* CS  */ {"Select", "Rename", "Raceswp", "Copy", "Reset"},
  /* ACD */ {"Select", "Rename", "Raceswp", "Copy", "Reset"},
  /* ESP */ {"Elegir", "Renomb", "Raceswp", "Copiar", "Reset"},
  /* DEU */ {"Wahl", "Name", "Raceswp", "Kopier", "Reset"},
  /* ITA */ {"Scegli", "Rinom", "Raceswp", "Copia", "Reset"},
  /* NLD */ {"Kies", "Hernoem", "Raceswp", "Kopie", "Reset"},
  /* POR */ {"Escolh", "Renome", "Raceswp", "Copiar", "Reset"}
};

/* View mode value labels - Pascal Case: [language][mode] */
const char* VIEW_MODE_LABELS_PASCAL[][3] = {
  /* NOR */ {"Av", "Full", "Enkel"},
  /* ENG */ {"Off", "Full", "Simple"},
  /* CS  */ {"Off", "Full", "Simple"},
  /* ACD */ {"Off", "Full", "Simple"},
  /* ESP */ {"Off", "Total", "Simple"},
  /* DEU */ {"Aus", "Voll", "Einf"},
  /* ITA */ {"Off", "Pieno", "Sempl"},
  /* NLD */ {"Uit", "Vol", "Eenv"},
  /* POR */ {"Off", "Total", "Simp"}
};

/* Sound submenu item names - Pascal Case: [language][item] */
const char* SOUND_MENU_NAMES_PASCAL[][3] = {
  /* NOR */ {"Oppstart", "Racemode", "Tilbake"},
  /* ENG */ {"Boot", "Race", "Back"},
  /* CS  */ {"Boot", "Race", "Back"},
  /* ACD */ {"Boot", "Race", "Back"},
  /* ESP */ {"Inicio", "Carrera", "Atras"},
  /* DEU */ {"Start", "Rennen", "Zuruck"},
  /* ITA */ {"Avvio", "Gara", "Indietro"},
  /* NLD */ {"Start", "Race", "Terug"},
  /* POR */ {"Arranque", "Corrida", "Voltar"}
};

/* ON/OFF labels - Pascal Case: [language][state] */
const char* ON_OFF_LABELS_PASCAL[][2] = {
  /* NOR */ {"Av", "Pa"},
  /* ENG */ {"Off", "On"},
  /* CS  */ {"Off", "On"},
  /* ACD */ {"Off", "On"},
  /* ESP */ {"Off", "On"},
  /* DEU */ {"Aus", "Ein"},
  /* ITA */ {"Off", "On"},
  /* NLD */ {"Uit", "Aan"},
  /* POR */ {"Off", "On"}
};

/* Release-brake mode labels - Pascal Case: [language][mode] */
const char* RELEASE_BRAKE_MODE_LABELS_PASCAL[][3] = {
  /* NOR */ {"Av", "Qck", "Drg"},
  /* ENG */ {"Off", "Qck", "Drg"},
  /* CS  */ {"Off", "Qck", "Drg"},
  /* ACD */ {"Off", "Qck", "Drg"},
  /* ESP */ {"Off", "Qck", "Drg"},
  /* DEU */ {"Aus", "Qck", "Drg"},
  /* ITA */ {"Off", "Qck", "Drg"},
  /* NLD */ {"Uit", "Qck", "Drg"},
  /* POR */ {"Off", "Qck", "Drg"}
};

/* BACK button labels - UPPER CASE: [language] */
const char* BACK_LABELS[] = {
  /* NOR */ "TILBAKE",
  /* ENG */ "BACK",
  /* CS  */ "BACK",
  /* ACD */ "BACK",
  /* ESP */ "ATRAS",
  /* DEU */ "ZURUCK",
  /* ITA */ "INDIETRO",
  /* NLD */ "TERUG",
  /* POR */ "VOLTAR"
};

/* BACK button labels - Pascal Case: [language] */
const char* BACK_LABELS_PASCAL[] = {
  /* NOR */ "Tilbake",
  /* ENG */ "Back",
  /* CS  */ "Back",
  /* ACD */ "Back",
  /* ESP */ "Atras",
  /* DEU */ "Zuruck",
  /* ITA */ "Indietro",
  /* NLD */ "Terug",
  /* POR */ "Voltar"
};

/* UI strings for car selection/copy/rename/reset screens: [language] */
const char* STR_SELECT_CAR[] = {
  "-VELG BIL-", "-SELECT THE CAR-", "-SELECT THE CAR-", "-SELECT THE CAR-",
  "-ELEGIR AUTO-", "-AUTO WAEHLEN-", "-SCEGLI AUTO-", "-KIES AUTO-", "-ESCOLH CARRO-"
};
const char* STR_COPY_FROM[] = {
  "-KOPIER FRA:-", "-COPY FROM:-", "-COPY FROM:-", "-COPY FROM:-",
  "-COPIAR DE:-", "-KOPIER VON:-", "-COPIA DA:-", "-KOPIE VAN:-", "-COPIAR DE:-"
};
const char* STR_COPY_TO[] = {
  "-KOPIER TIL:-", "-COPY TO:-", "-COPY TO:-", "-COPY TO:-",
  "-COPIAR A:-", "-KOPIER ZU:-", "-COPIA A:-", "-KOPIE NAAR:-", "-COPIAR PARA:-"
};
const char* STR_ALL[] = {"ALLE", "ALL", "ALL", "ALL", "TODO", "ALLE", "TUTT", "ALLE", "TODOS"};
const char* STR_COPIED_ALL[] = {
  "KOPIERT!", "COPIED ALL", "COPIED ALL", "COPIED ALL",
  "TODO COP", "ALLE KOP", "TUTTE OK", "ALLES OK", "TODOS OK"
};
const char* STR_COPIED[] = {"KOPIERT!", "COPIED!", "COPIED!", "COPIED!", "COPIADO", "KOPIERT", "COPIATO", "KOPIE OK", "COPIADO"};
const char* STR_RENAME_CAR[] = {
  "-GI NYTT NAVN-", "-RENAME THE CAR-", "-RENAME THE CAR-", "-RENAME THE CAR-",
  "-RENOMB AUTO-", "-AUTO BENENN-", "-RINOM AUTO-", "-HERNOEM AUTO-", "-RENOMEAR CARRO-"
};
const char* STR_CONFIRM[] = {
  "-TRYKK OK FOR GODTA-", "-CLICK OK TO CONFIRM-", "-CLICK OK TO CONFIRM-", "-CLICK OK TO CONFIRM-",
  "-OK CONFIRMAR-", "-OK BESTAETIG-", "-OK CONFERMA-", "-DRUK OK BEVEST-", "-OK P/CONFIRMAR-"
};
const char* STR_LIMITER[] = {
  " - BEGRENS - ", " - LIMITER - ", " - CHOKE1 - ", " - CHOKE - ",
  " - LIMITE - ", " - LIMIT - ", " - LIMITE - ", " - LIMIET - ", " - LIMITE - "
};
const char* STR_CALIBRATION[] = {
  "KALIBRERING", "CALIBRATION", "CALIBRATION", "CALIBRATION",
  "CALIBRACION", "KALIBRATION", "CALIBRAZIONE", "KALIBRATIE", "CALIBRACAO"
};
const char* STR_PRESS_RELEASE[] = {
  "trykk/slipp gass", "press/release trg", "press/release trg", "press/release trg",
  "pulsa/suelta trg", "drueck/los trg", "premi/molla trg", "druk/los trg", "prima/solte trg"
};
const char* STR_PUSH_DONE[] = {
  " trykk nar ferdig ", " press when done ", " press when done ", " press when done ",
  " pulsa al final  ", " druck bei ende ", " premi a fine   ", " druk als klaar ", " prima no fim   "
};
