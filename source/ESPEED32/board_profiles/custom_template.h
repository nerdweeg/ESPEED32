#ifndef BOARD_PROFILE_CUSTOM_TEMPLATE_H_
#define BOARD_PROFILE_CUSTOM_TEMPLATE_H_

/* Copy this template locally and edit the values below to match your board
 * before building with:
 *   ./scripts/flash_all.sh --compile-only --board custom-template
 *
 * This profile is intended for manual/custom builds only.
 *
 * The defaults below are pre-filled for a split-box style custom controller
 * with AS5600 trigger I2C, a separate OLED I2C bus, one direct UI encoder,
 * and BTN99X0 motor control. Treat it as a starting point, not an official
 * hardware target. */

#define BOARD_PROFILE_LABEL "custom-template"

/* I2C Pins */
#define BOARD_SDA0_PIN    26
#define BOARD_SCL0_PIN    25
#define BOARD_SDA1_PIN    16
#define BOARD_SCL1_PIN    17

/* OLED Display Configuration */
#define BOARD_RESET_PIN   -1
#define BOARD_OLED_ADDR   -1
#define BOARD_FLIP180     0
#define BOARD_INVERT_DISP 0
#define BOARD_USE_HW_I2C  1

/* Analog Input Pins */
#define BOARD_AN_VIN_DIV   36
#define BOARD_EXT_POT1_PIN 35
#define BOARD_EXT_POT2_PIN 15

/* Rotary Encoder Pins */
#define BOARD_ENCODER_A_PIN      18
#define BOARD_ENCODER_B_PIN      19
#define BOARD_ENCODER_BUTTON_PIN 5
#define BOARD_ENCODER_VCC_PIN    -1
#define BOARD_ENCODER_STEPS      4

/* Motor Control Pins */
#define BOARD_AN_MOT_BEMF 34
#define BOARD_HB_AN_PIN   27
#define BOARD_HB_IN_PIN   14
#define BOARD_HB_INH_PIN  13
#define BOARD_LED_BUILTIN 2

/* Other Pins */
#define BOARD_BUTT_PIN 21
#define BOARD_BUZZ_PIN 23

#endif  /* BOARD_PROFILE_CUSTOM_TEMPLATE_H_ */
