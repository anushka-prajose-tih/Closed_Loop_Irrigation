/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "ssd1306.h"
#include "fonts.h"
#include "w25q32jv.h"
#include "rotary_encoder.h"
#include "GSM_EC200U.h"
#include "modbus_rtu.h"
#include "i2c.h"
#include "rtc_sync.h"

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
void serial_print(char* str);
void serial_print_inline(char* str);
void printDisplay(char *data);
void printDisplay_level(uint16_t x_coord, char *data);
void check_wakeup(void);
void App_operation(void);
void Read_data(time_t ts);
uint32_t ADC_read_voltage(uint32_t channel);
void Oled_Data_display();
void GSM_post_sequence(time_t ts);

union ByteToUint32 {
  uint8_t bytes[4];
  uint32_t value;
};

union ByteToUint16 {
  uint8_t bytes[2];
  uint16_t value;
};

union ByteToFloat
{
  uint8_t bytes[4];
  float value;
};

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define GSM_KEY_Pin GPIO_PIN_13
#define GSM_KEY_GPIO_Port GPIOC
#define MCO_Pin GPIO_PIN_0
#define MCO_GPIO_Port GPIOF
#define I2C_Cut_Pin GPIO_PIN_14
#define I2C_Cut_GPIO_Port GPIOB
#define RS485_Cut_Pin GPIO_PIN_15
#define RS485_Cut_GPIO_Port GPIOB
#define Rotary_button_Pin GPIO_PIN_6
#define Rotary_button_GPIO_Port GPIOC
#define Rotary_button_EXTI_IRQn EXTI4_15_IRQn
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define FLASH_CS_Pin GPIO_PIN_9
#define FLASH_CS_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define DEVICE_NAME "SAMBHAV"
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
