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

bool scrollMenuFrame(uint16_t selector, uint8_t visibleLines, uint16_t& frameUpper, uint16_t& frameLower) {
  if (selector > frameLower) {
    frameLower = selector;
    frameUpper = frameLower - visibleLines + 1;
    return true;
  }
  if (selector < frameUpper) {
    frameUpper = selector;
    frameLower = frameUpper + visibleLines - 1;
    return true;
  }
  return false;
}
