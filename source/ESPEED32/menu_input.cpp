#include "menu_input.h"
#include "HAL.h"
#include "slot_ESC.h"

bool BrakeButtonEdge::pressed() {
  if (digitalRead(BUTT_PIN) != BUTTON_PRESSED) {
    latched = false;
    return false;
  }
  if (latched || millis() - lastPressMs <= BUTTON_SHORT_PRESS_DEBOUNCE_MS) {
    return false;
  }
  latched = true;
  lastPressMs = millis();
  return true;
}

void waitForBrakeButtonRelease() {
  while (digitalRead(BUTT_PIN) == BUTTON_PRESSED) {
    vTaskDelay(5);
  }
}
