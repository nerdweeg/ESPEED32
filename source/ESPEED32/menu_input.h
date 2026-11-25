#ifndef MENU_INPUT_H_
#define MENU_INPUT_H_

#include <Arduino.h>

/**
 * @brief Debounced press-edge detector for the brake button used as BACK/cancel in menus.
 * @details pressed() returns true once per press, and only if the previous accepted
 *          press was more than BUTTON_SHORT_PRESS_DEBOUNCE_MS ago. Declare instances
 *          `static` inside a menu function to keep the latch across menu re-entries,
 *          like the hand-written latches this replaces.
 */
struct BrakeButtonEdge {
  bool latched = false;
  uint32_t lastPressMs = 0;
  bool pressed();
};

/** @brief Block (yielding) until the brake button is released. */
void waitForBrakeButtonRelease();

#endif  /* MENU_INPUT_H_ */
