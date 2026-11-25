#include "wifi_qr.h"
#include "app_globals.h"

/* QR code (version 3, ECC level L, byte mode) for the WiFi join payload, drawn on the OLED. */

static const uint8_t WIFI_QR_VERSION = 3;
static const uint8_t WIFI_QR_DATA_CODEWORDS = 55;
static const uint8_t WIFI_QR_ECC_CODEWORDS = 15;
static const uint8_t WIFI_QR_SCALE = 2;
static const uint8_t WIFI_QR_QUIET_ZONE = 1;

static inline bool qrGetBit(uint16_t value, uint8_t bit) {
  return ((value >> bit) & 1U) != 0;
}

static void qrSetFunctionModule(uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                                uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                                int row, int col, bool isBlack) {
  if (row < 0 || row >= WIFI_QR_SIZE || col < 0 || col >= WIFI_QR_SIZE) {
    return;
  }
  modules[row][col] = isBlack ? 1 : 0;
  functionModules[row][col] = 1;
}

static void qrReserveModule(uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                            int row, int col) {
  if (row < 0 || row >= WIFI_QR_SIZE || col < 0 || col >= WIFI_QR_SIZE) {
    return;
  }
  functionModules[row][col] = 1;
}

static void qrDrawFinderPattern(uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                                uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                                int top, int left) {
  for (int dy = -1; dy <= 7; dy++) {
    for (int dx = -1; dx <= 7; dx++) {
      int row = top + dy;
      int col = left + dx;
      if (row < 0 || row >= WIFI_QR_SIZE || col < 0 || col >= WIFI_QR_SIZE) {
        continue;
      }
      bool isBlack = (dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6 &&
                      (dx == 0 || dx == 6 || dy == 0 || dy == 6 ||
                       (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4)));
      qrSetFunctionModule(modules, functionModules, row, col, isBlack);
    }
  }
}

static void qrDrawAlignmentPattern(uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                                   uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                                   int centerRow, int centerCol) {
  for (int dy = -2; dy <= 2; dy++) {
    for (int dx = -2; dx <= 2; dx++) {
      bool isBlack = max(abs(dx), abs(dy)) != 1;
      qrSetFunctionModule(modules, functionModules, centerRow + dy, centerCol + dx, isBlack);
    }
  }
}

static void qrDrawFunctionPatterns(uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                                   uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  memset(modules, 0, WIFI_QR_SIZE * WIFI_QR_SIZE);
  memset(functionModules, 0, WIFI_QR_SIZE * WIFI_QR_SIZE);

  qrDrawFinderPattern(modules, functionModules, 0, 0);
  qrDrawFinderPattern(modules, functionModules, 0, WIFI_QR_SIZE - 7);
  qrDrawFinderPattern(modules, functionModules, WIFI_QR_SIZE - 7, 0);
  qrDrawAlignmentPattern(modules, functionModules, 22, 22);

  for (int i = 8; i < WIFI_QR_SIZE - 8; i++) {
    bool isBlack = (i % 2) == 0;
    qrSetFunctionModule(modules, functionModules, 6, i, isBlack);
    qrSetFunctionModule(modules, functionModules, i, 6, isBlack);
  }

  for (int i = 0; i < 9; i++) {
    qrReserveModule(functionModules, 8, i);
    qrReserveModule(functionModules, i, 8);
  }
  for (int i = 0; i < 8; i++) {
    qrReserveModule(functionModules, 8, WIFI_QR_SIZE - 1 - i);
  }
  for (int i = 0; i < 7; i++) {
    qrReserveModule(functionModules, WIFI_QR_SIZE - 1 - i, 8);
  }

  qrSetFunctionModule(modules, functionModules, WIFI_QR_SIZE - 8, 8, true);
}

static bool qrAppendBits(uint8_t out[WIFI_QR_DATA_CODEWORDS], int* bitLen,
                         uint32_t value, uint8_t bitCount) {
  if (bitLen == nullptr) {
    return false;
  }
  if ((*bitLen + bitCount) > (WIFI_QR_DATA_CODEWORDS * 8)) {
    return false;
  }
  for (int i = bitCount - 1; i >= 0; i--) {
    uint8_t bit = (uint8_t)((value >> i) & 1U);
    out[*bitLen >> 3] |= bit << (7 - (*bitLen & 7));
    (*bitLen)++;
  }
  return true;
}

static uint8_t qrGfMultiply(uint8_t x, uint8_t y) {
  uint16_t a = x;
  uint16_t b = y;
  uint16_t z = 0;
  while (b != 0) {
    if (b & 1U) {
      z ^= a;
    }
    b >>= 1;
    a <<= 1;
    if (a & 0x100U) {
      a ^= 0x11DU;
    }
  }
  return (uint8_t)z;
}

static void qrBuildGenerator(uint8_t out[WIFI_QR_ECC_CODEWORDS]) {
  memset(out, 0, WIFI_QR_ECC_CODEWORDS);
  out[WIFI_QR_ECC_CODEWORDS - 1] = 1;
  uint8_t root = 1;
  for (uint8_t i = 0; i < WIFI_QR_ECC_CODEWORDS; i++) {
    for (uint8_t j = 0; j < WIFI_QR_ECC_CODEWORDS; j++) {
      out[j] = qrGfMultiply(out[j], root);
      if (j + 1 < WIFI_QR_ECC_CODEWORDS) {
        out[j] ^= out[j + 1];
      }
    }
    root = qrGfMultiply(root, 0x02);
  }
}

static void qrComputeRemainder(const uint8_t data[WIFI_QR_DATA_CODEWORDS],
                               const uint8_t generator[WIFI_QR_ECC_CODEWORDS],
                               uint8_t remainder[WIFI_QR_ECC_CODEWORDS]) {
  memset(remainder, 0, WIFI_QR_ECC_CODEWORDS);
  for (uint8_t i = 0; i < WIFI_QR_DATA_CODEWORDS; i++) {
    uint8_t factor = data[i] ^ remainder[0];
    for (uint8_t j = 0; j < WIFI_QR_ECC_CODEWORDS - 1; j++) {
      remainder[j] = remainder[j + 1] ^ qrGfMultiply(generator[j], factor);
    }
    remainder[WIFI_QR_ECC_CODEWORDS - 1] =
      qrGfMultiply(generator[WIFI_QR_ECC_CODEWORDS - 1], factor);
  }
}

static void qrPlaceCodewords(uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                             const uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                             const uint8_t codewords[WIFI_QR_DATA_CODEWORDS + WIFI_QR_ECC_CODEWORDS]) {
  int bitIndex = 0;
  bool upwards = true;

  for (int right = WIFI_QR_SIZE - 1; right >= 1; right -= 2) {
    if (right == 6) {
      right--;
    }
    for (int i = 0; i < WIFI_QR_SIZE; i++) {
      int row = upwards ? (WIFI_QR_SIZE - 1 - i) : i;
      for (int j = 0; j < 2; j++) {
        int col = right - j;
        if (functionModules[row][col]) {
          continue;
        }
        bool bit = false;
        if (bitIndex < ((WIFI_QR_DATA_CODEWORDS + WIFI_QR_ECC_CODEWORDS) * 8)) {
          bit = ((codewords[bitIndex >> 3] >> (7 - (bitIndex & 7))) & 1U) != 0;
        }
        modules[row][col] = bit ? 1 : 0;
        bitIndex++;
      }
    }
    upwards = !upwards;
  }
}

static bool qrMaskApplies(uint8_t mask, int row, int col) {
  switch (mask) {
    case 0: return ((row + col) % 2) == 0;
    case 1: return (row % 2) == 0;
    case 2: return (col % 3) == 0;
    case 3: return ((row + col) % 3) == 0;
    case 4: return (((row / 2) + (col / 3)) % 2) == 0;
    case 5: return (((row * col) % 2) + ((row * col) % 3)) == 0;
    case 6: return ((((row * col) % 2) + ((row * col) % 3)) % 2) == 0;
    case 7: return ((((row + col) % 2) + ((row * col) % 3)) % 2) == 0;
    default: return false;
  }
}

static void qrApplyMask(uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                        const uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE],
                        uint8_t mask) {
  for (int row = 0; row < WIFI_QR_SIZE; row++) {
    for (int col = 0; col < WIFI_QR_SIZE; col++) {
      if (!functionModules[row][col] && qrMaskApplies(mask, row, col)) {
        modules[row][col] ^= 1;
      }
    }
  }
}

static uint16_t qrGetFormatBits(uint8_t mask) {
  uint16_t data = (1U << 3) | mask;  /* ECC level L = 01 */
  uint16_t rem = data << 10;
  for (int bit = 14; bit >= 10; bit--) {
    if (((rem >> bit) & 1U) != 0) {
      rem ^= (uint16_t)(0x537U << (bit - 10));
    }
  }
  return (uint16_t)(((data << 10) | rem) ^ 0x5412U);
}

static void qrDrawFormatBits(uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE], uint8_t mask) {
  uint16_t formatBits = qrGetFormatBits(mask);

  for (uint8_t i = 0; i <= 5; i++) {
    modules[i][8] = qrGetBit(formatBits, i) ? 1 : 0;
  }
  modules[7][8] = qrGetBit(formatBits, 6) ? 1 : 0;
  modules[8][8] = qrGetBit(formatBits, 7) ? 1 : 0;
  modules[8][7] = qrGetBit(formatBits, 8) ? 1 : 0;
  for (uint8_t i = 9; i < 15; i++) {
    modules[8][14 - i] = qrGetBit(formatBits, i) ? 1 : 0;
  }

  for (uint8_t i = 0; i < 8; i++) {
    modules[8][WIFI_QR_SIZE - 1 - i] = qrGetBit(formatBits, i) ? 1 : 0;
  }
  for (uint8_t i = 8; i < 15; i++) {
    modules[WIFI_QR_SIZE - 15 + i][8] = qrGetBit(formatBits, i) ? 1 : 0;
  }
  modules[WIFI_QR_SIZE - 8][8] = 1;
}

static int qrPenaltyRule1(const uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  int penalty = 0;

  for (int row = 0; row < WIFI_QR_SIZE; row++) {
    int runLength = 1;
    uint8_t runColor = modules[row][0];
    for (int col = 1; col < WIFI_QR_SIZE; col++) {
      if (modules[row][col] == runColor) {
        runLength++;
      } else {
        if (runLength >= 5) {
          penalty += runLength - 2;
        }
        runColor = modules[row][col];
        runLength = 1;
      }
    }
    if (runLength >= 5) {
      penalty += runLength - 2;
    }
  }

  for (int col = 0; col < WIFI_QR_SIZE; col++) {
    int runLength = 1;
    uint8_t runColor = modules[0][col];
    for (int row = 1; row < WIFI_QR_SIZE; row++) {
      if (modules[row][col] == runColor) {
        runLength++;
      } else {
        if (runLength >= 5) {
          penalty += runLength - 2;
        }
        runColor = modules[row][col];
        runLength = 1;
      }
    }
    if (runLength >= 5) {
      penalty += runLength - 2;
    }
  }

  return penalty;
}

static int qrPenaltyRule2(const uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  int penalty = 0;
  for (int row = 0; row < WIFI_QR_SIZE - 1; row++) {
    for (int col = 0; col < WIFI_QR_SIZE - 1; col++) {
      uint8_t color = modules[row][col];
      if (color == modules[row][col + 1] &&
          color == modules[row + 1][col] &&
          color == modules[row + 1][col + 1]) {
        penalty += 3;
      }
    }
  }
  return penalty;
}

static bool qrFinderPenaltyPattern(const uint8_t line[11]) {
  static const uint8_t PATTERN_A[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
  static const uint8_t PATTERN_B[11] = {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1};
  bool matchA = true;
  bool matchB = true;
  for (uint8_t i = 0; i < 11; i++) {
    if (line[i] != PATTERN_A[i]) {
      matchA = false;
    }
    if (line[i] != PATTERN_B[i]) {
      matchB = false;
    }
  }
  return matchA || matchB;
}

static int qrPenaltyRule3(const uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  int penalty = 0;
  uint8_t line[11];

  for (int row = 0; row < WIFI_QR_SIZE; row++) {
    for (int col = 0; col <= WIFI_QR_SIZE - 11; col++) {
      for (uint8_t i = 0; i < 11; i++) {
        line[i] = modules[row][col + i];
      }
      if (qrFinderPenaltyPattern(line)) {
        penalty += 40;
      }
    }
  }

  for (int col = 0; col < WIFI_QR_SIZE; col++) {
    for (int row = 0; row <= WIFI_QR_SIZE - 11; row++) {
      for (uint8_t i = 0; i < 11; i++) {
        line[i] = modules[row + i][col];
      }
      if (qrFinderPenaltyPattern(line)) {
        penalty += 40;
      }
    }
  }

  return penalty;
}

static int qrPenaltyRule4(const uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  int darkCount = 0;
  for (int row = 0; row < WIFI_QR_SIZE; row++) {
    for (int col = 0; col < WIFI_QR_SIZE; col++) {
      darkCount += modules[row][col] ? 1 : 0;
    }
  }

  const int totalModules = WIFI_QR_SIZE * WIFI_QR_SIZE;
  int k = abs(darkCount * 20 - totalModules * 10) / totalModules;
  return k * 10;
}

static int qrGetPenaltyScore(const uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  return qrPenaltyRule1(modules) +
         qrPenaltyRule2(modules) +
         qrPenaltyRule3(modules) +
         qrPenaltyRule4(modules);
}

bool buildWiFiQrMatrix(const char* payload,
                       uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  if (payload == nullptr) {
    return false;
  }

  size_t payloadLen = strlen(payload);
  if (payloadLen == 0 || payloadLen > 53) {
    return false;
  }

  uint8_t dataCodewords[WIFI_QR_DATA_CODEWORDS];
  uint8_t generator[WIFI_QR_ECC_CODEWORDS];
  uint8_t remainder[WIFI_QR_ECC_CODEWORDS];
  uint8_t allCodewords[WIFI_QR_DATA_CODEWORDS + WIFI_QR_ECC_CODEWORDS];
  uint8_t functionModules[WIFI_QR_SIZE][WIFI_QR_SIZE];

  memset(dataCodewords, 0, sizeof(dataCodewords));
  int bitLen = 0;
  if (!qrAppendBits(dataCodewords, &bitLen, 0x4, 4)) {
    return false;
  }
  if (!qrAppendBits(dataCodewords, &bitLen, (uint32_t)payloadLen, 8)) {
    return false;
  }
  for (size_t i = 0; i < payloadLen; i++) {
    if (!qrAppendBits(dataCodewords, &bitLen, (uint8_t)payload[i], 8)) {
      return false;
    }
  }

  int capacityBits = WIFI_QR_DATA_CODEWORDS * 8;
  int terminatorBits = min(4, capacityBits - bitLen);
  if (terminatorBits > 0 && !qrAppendBits(dataCodewords, &bitLen, 0, (uint8_t)terminatorBits)) {
    return false;
  }

  int zeroPadBits = (8 - (bitLen & 7)) & 7;
  if (zeroPadBits > 0 && !qrAppendBits(dataCodewords, &bitLen, 0, (uint8_t)zeroPadBits)) {
    return false;
  }

  bool useAltPad = false;
  while (bitLen < capacityBits) {
    dataCodewords[bitLen >> 3] = useAltPad ? 0x11 : 0xEC;
    useAltPad = !useAltPad;
    bitLen += 8;
  }

  qrBuildGenerator(generator);
  qrComputeRemainder(dataCodewords, generator, remainder);

  memcpy(allCodewords, dataCodewords, WIFI_QR_DATA_CODEWORDS);
  memcpy(allCodewords + WIFI_QR_DATA_CODEWORDS, remainder, WIFI_QR_ECC_CODEWORDS);

  qrDrawFunctionPatterns(modules, functionModules);
  qrPlaceCodewords(modules, functionModules, allCodewords);

  uint8_t bestMask = 0;
  int bestPenalty = 0x7FFFFFFF;
  uint8_t trial[WIFI_QR_SIZE][WIFI_QR_SIZE];
  for (uint8_t mask = 0; mask < 8; mask++) {
    memcpy(trial, modules, sizeof(trial));
    qrApplyMask(trial, functionModules, mask);
    qrDrawFormatBits(trial, mask);
    int penalty = qrGetPenaltyScore(trial);
    if (penalty < bestPenalty) {
      bestPenalty = penalty;
      bestMask = mask;
    }
  }

  qrApplyMask(modules, functionModules, bestMask);
  qrDrawFormatBits(modules, bestMask);
  return true;
}

bool buildWiFiQrPayload(const char* ssid, const char* password,
                        char out[64]) {
  if (ssid == nullptr || password == nullptr || out == nullptr) {
    return false;
  }
  int written = snprintf(out, 64, "WIFI:T:WPA;S:%s;P:%s;;", ssid, password);
  return written > 0 && written < 64;
}

void drawWiFiQrMatrix(const uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]) {
  const int totalPixels = (WIFI_QR_SIZE + (2 * WIFI_QR_QUIET_ZONE)) * WIFI_QR_SCALE;
  const int originX = (OLED_WIDTH - totalPixels) / 2;
  const int originY = (OLED_HEIGHT - totalPixels) / 2;

  obdFill(&g_obd, OBD_WHITE, 1);
  for (int row = 0; row < WIFI_QR_SIZE; row++) {
    for (int col = 0; col < WIFI_QR_SIZE; col++) {
      if (!modules[row][col]) {
        continue;
      }
      int x0 = originX + (WIFI_QR_QUIET_ZONE + col) * WIFI_QR_SCALE;
      int y0 = originY + (WIFI_QR_QUIET_ZONE + row) * WIFI_QR_SCALE;
      for (uint8_t dy = 0; dy < WIFI_QR_SCALE; dy++) {
        obdDrawLine(&g_obd, x0, y0 + dy, x0 + WIFI_QR_SCALE - 1, y0 + dy, OBD_BLACK, 1);
      }
    }
  }
}
