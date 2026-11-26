#ifndef WIFI_QR_H_
#define WIFI_QR_H_

#include <stdint.h>

static const uint8_t WIFI_QR_SIZE = 29;  /* Modules per side for a version 3 QR code */

/** @brief Build the "WIFI:T:WPA;S:...;P:...;;" join payload. Returns false if it does not fit. */
bool buildWiFiQrPayload(const char* ssid, const char* password, char out[64]);

/** @brief Encode payload into a QR module matrix. Returns false if the payload is too long. */
bool buildWiFiQrMatrix(const char* payload, uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]);

/** @brief Draw the QR matrix centered on the OLED. */
void drawWiFiQrMatrix(const uint8_t modules[WIFI_QR_SIZE][WIFI_QR_SIZE]);

#endif  /* WIFI_QR_H_ */
