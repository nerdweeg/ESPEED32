#include "power_ui.h"
#include <Arduino.h>
#include "slot_ESC.h"
#include "connectivity_portal.h"

extern TaskHandle_t Task2;
extern StoredVar_type g_storedVar;
extern ESC_type g_escVar;
extern OBDISP g_obd;
extern AiEsp32RotaryEncoder g_rotaryEncoder;
extern uint32_t g_lastEncoderInteraction;

extern void stopTimedWiFiPortal();

/**
 * @brief Count live controller input as user activity for idle timers.
 * @details Any trigger movement above deadband, brake-button press, or encoder move
 *          resets the inactivity timer. If screensaver is already active, the same
 *          inputs wake it and let the caller redraw the current menu.
 */
bool refreshIdleInteractionFromControls(uint32_t* lastInteraction, bool* screensaverActive, uint16_t* lastEncoderPos) {
  if (lastInteraction == NULL || lastEncoderPos == NULL) return false;

  if (isOtaInProgress()) {
    uint32_t now = millis();
    *lastInteraction = now;
    g_lastEncoderInteraction = now;
    if (screensaverActive != NULL && *screensaverActive) {
      *screensaverActive = false;
      return true;
    }
    return false;
  }

  uint16_t currentEncoderPos = (uint16_t)readUiEncoder();
  bool encoderMoved = (currentEncoderPos != *lastEncoderPos);
  bool buttonPressed = (digitalRead(BUTT_PIN) == BUTTON_PRESSED);
  bool triggerActive = (g_escVar.trigger_norm > 0);

  *lastEncoderPos = currentEncoderPos;

  if (!encoderMoved && !buttonPressed && !triggerActive) {
    return false;
  }

  uint32_t now = millis();
  *lastInteraction = now;
  g_lastEncoderInteraction = now;

  if (screensaverActive != NULL && *screensaverActive) {
    *screensaverActive = false;
    return true;
  }

  return false;
}

bool serviceIdlePowerTransitions(uint32_t* lastInteraction, bool* screensaverActive) {
  if (lastInteraction == NULL) return false;

  if (g_storedVar.screensaverTimeout == 0) return false;

  uint32_t now = millis();

  /* OTA safety lock: do not enter sleep/deep sleep while upload is active. */
  if (isOtaInProgress()) {
    *lastInteraction = now;
    if (screensaverActive != NULL) *screensaverActive = false;
    return false;
  }

  uint32_t idleMs = now - *lastInteraction;
  uint32_t screensaverMs = (uint32_t)g_storedVar.screensaverTimeout * 1000UL;

  if (g_storedVar.deepSleepTimeout > 0 &&
      idleMs > screensaverMs + ((uint32_t)g_storedVar.deepSleepTimeout * 60000UL)) {
    showDeepSleep();  /* Never returns */
  }

  if (g_storedVar.powerSaveTimeout > 0 &&
      idleMs > screensaverMs + ((uint32_t)g_storedVar.powerSaveTimeout * 60000UL)) {
    showPowerSave(*lastInteraction);
    *lastInteraction = millis();
    if (screensaverActive != NULL) *screensaverActive = false;
    return true;
  }

  return false;
}

/**
 * @brief Consume wake-up input so it doesn't also trigger menu actions.
 * @details Prevents one wake press from being interpreted as BACK/OK in nested menus.
 * @param wakeTriggered True when current loop woke from screensaver.
 * @return true if caller should skip the rest of this iteration.
 */
bool consumeScreensaverWakeInput(bool wakeTriggered) {
  if (!wakeTriggered) return false;

  g_lastEncoderInteraction = millis();
  g_rotaryEncoder.isEncoderButtonClicked();  /* consume pending click edge */

  uint32_t guardStart = millis();
  while (digitalRead(BUTT_PIN) == BUTTON_PRESSED &&
         (uint32_t)(millis() - guardStart) < BUTTON_SHORT_PRESS_DEBOUNCE_MS) {
    vTaskDelay(1);
  }
  return true;
}

void showPowerSave() {
  showPowerSave(millis());
}

void showPowerSave(uint32_t inactivityStartMs) {
  if (isOtaInProgress()) {
    return;
  }

  uint16_t lang = g_storedVar.language;
  const char* sleepMsg[] = {
    "SOVER...", "SLEEPING...", "SLEEPING...", "SLEEPING...",
    "DURMIENDO...", "SCHLAFEN...", "RIPOSO...", "SLAPEN...", "DORMIR..."
  };

  /* Brief sleep message */
  obdFill(&g_obd, OBD_WHITE, 1);
  const char* msg = sleepMsg[lang];
  int tw = strlen(msg) * WIDTH8x8;
  obdWriteString(&g_obd, 0, (OLED_WIDTH - tw) / 2, 28, (char*)msg, FONT_8x8, OBD_BLACK, 1);
  delay(1200);

  /* Sleep mode policy: always disable WiFi/AP while sleeping. */
  stopTimedWiFiPortal();

  /* Safe: motor off */
  HalfBridge_SetPwmDrag(0, 0);
  /* Display off */
  obdPower(&g_obd, 0);
  /* Reduce CPU frequency to save power */
  setCpuFrequencyMhz(80);
  /* Suspend motor control task */
  vTaskSuspend(Task2);
  /* Task2 may have re-applied PWM from a live trigger reading in the brief
   * window before the suspend above took effect; force the motor off again
   * now that it's guaranteed to be stopped. */
  HalfBridge_SetPwmDrag(0, 0);

  /* Wait for encoder button press to wake.
   * While waiting, allow deep sleep timeout to take over. */
  uint32_t screensaverMs = (uint32_t)g_storedVar.screensaverTimeout * 1000UL;
  while (digitalRead(ENCODER_BUTTON_PIN) != BUTTON_PRESSED) {
    if (g_storedVar.screensaverTimeout > 0 && g_storedVar.deepSleepTimeout > 0 &&
        millis() - inactivityStartMs > screensaverMs + ((uint32_t)g_storedVar.deepSleepTimeout * 60000UL)) {
      showDeepSleep();  /* Never returns */
    }
    vTaskDelay(50);
  }

  /* Wake up: restore CPU, resume motor task, display on */
  setCpuFrequencyMhz(240);
  vTaskResume(Task2);
  obdPower(&g_obd, 1);
  obdFill(&g_obd, OBD_WHITE, 1);
  delay(200);  /* Debounce */
}

void showDeepSleep() {
  if (isOtaInProgress()) {
    return;
  }

  uint16_t lang = g_storedVar.language;
  const char* powerOffMsg[] = {
    "SLUKKER...", "POWER OFF...", "POWER OFF...", "POWER OFF...",
    "APAGANDO...", "AUSSCHALT.", "SPEGNENDO...", "UITSCHAK.", "DESLIGAR."
  };

  /* Brief power-off message */
  obdFill(&g_obd, OBD_WHITE, 1);
  const char* msg = powerOffMsg[lang];
  int tw = strlen(msg) * WIDTH8x8;
  obdWriteString(&g_obd, 0, (OLED_WIDTH - tw) / 2, 28, (char*)msg, FONT_8x8, OBD_BLACK, 1);
  delay(1200);

  /* Safe shutdown: motor off, display off */
  HalfBridge_SetPwmDrag(0, 0);
  obdPower(&g_obd, 0);

  /* Enter deep sleep - wakes only on power cycle */
  esp_deep_sleep_start();
}
