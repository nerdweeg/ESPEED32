#ifndef BOARD_PROFILE_H_
#define BOARD_PROFILE_H_

#define ESPEED_BOARD_PROFILE_STANDARD         1
#define ESPEED_BOARD_PROFILE_CUSTOM_TEMPLATE  2

#ifndef ESPEED_BOARD_PROFILE
  #define ESPEED_BOARD_PROFILE ESPEED_BOARD_PROFILE_STANDARD
#endif

#if ESPEED_BOARD_PROFILE == ESPEED_BOARD_PROFILE_STANDARD
  #include "board_profiles/standard.h"
#elif ESPEED_BOARD_PROFILE == ESPEED_BOARD_PROFILE_CUSTOM_TEMPLATE
  #include "board_profiles/custom_template.h"
#else
  #error "Unsupported ESPEED board profile"
#endif

#endif  /* BOARD_PROFILE_H_ */
