#ifndef BOARD_PROFILE_STANDARD_H_
#define BOARD_PROFILE_STANDARD_H_

#define BOARD_PROFILE_LABEL "standard"

/* I2C Pins */
#define BOARD_SDA0_PIN    21
#define BOARD_SCL0_PIN    22
#define BOARD_SDA1_PIN    33
#define BOARD_SCL1_PIN    32

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
#define BOARD_ENCODER_A_PIN      16
#define BOARD_ENCODER_B_PIN      17
#define BOARD_ENCODER_BUTTON_PIN 4
#define BOARD_ENCODER_VCC_PIN    -1
#define BOARD_ENCODER_STEPS      4

/* Motor Control Pins */
#define BOARD_AN_MOT_BEMF 14
#define BOARD_HB_AN_PIN   25
#define BOARD_HB_IN_PIN   26
#define BOARD_HB_INH_PIN  27
#define BOARD_LED_BUILTIN 2

/* Other Pins */
#define BOARD_BUTT_PIN 13
#define BOARD_BUZZ_PIN 18

#endif  /* BOARD_PROFILE_STANDARD_H_ */
