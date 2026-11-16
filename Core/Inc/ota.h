/**
 ******************************************************************************
 * @file    ota.h
 * @brief   Firmware update over the air from ThingsBoard (device HTTP API),
 *          using the STM32G0B1's two 256 KB flash banks (see ota.c)
 ******************************************************************************
 */

#ifndef OTA_H_
#define OTA_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ThingsBoard server: scheme and host, no trailing slash */
#define OTA_SERVER_URL          "https://eu.thingsboard.cloud"

/* The device's ThingsBoard access token comes from ota_credentials.h, which
   is git-ignored (it is a credential, and differs per device). Without it
   OTA is disabled. */
#if defined(__has_include)
#if __has_include("ota_credentials.h")
#include "ota_credentials.h"
#endif
#endif
#ifndef OTA_ACCESS_TOKEN
#define OTA_ACCESS_TOKEN        ""
#endif

/* A new image that hasn't confirmed itself (by reaching the server) after
   this many boots is rolled back to the previous one */
#define OTA_MAX_TRIAL_BOOTS     3U

/* Call once at boot, after MX_RTC_Init() and MX_USART6_UART_Init(): handles
   trial boots of a new image and rolls back when needed (may reset). */
void OTA_BootCheck(void);

/* Call once the modem is up: asks ThingsBoard which firmware is assigned,
   confirms or reports the running one, and downloads and installs a
   different one (resets into it). Runs once per MCU boot. */
void OTA_Poll(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_H_ */
