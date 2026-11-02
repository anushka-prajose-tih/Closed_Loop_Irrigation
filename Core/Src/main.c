/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DISPLAY_DELAY 800
#define BATTERY_VOLTAGE_CHANNEL ADC_CHANNEL_0
#define SOLAR_RAD_CHANNEL ADC_CHANNEL_1
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c2;
I2C_HandleTypeDef hi2c3;

RTC_HandleTypeDef hrtc;

SPI_HandleTypeDef hspi2;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart3;
UART_HandleTypeDef huart4;
UART_HandleTypeDef huart6;

/* USER CODE BEGIN PV */
/* Device identifiers and timing configuration */
uint32_t uid_stm32 = 0;
uint8_t time_interval = 15;   // Data collection interval in minutes
uint8_t GSM_interval = 1;     // Hourly GSM posting interval
// uint8_t flash_data_size = 82; // Old record size (float-encoded weather station)
uint8_t flash_data_size = 70; // Bytes stored per flash record
uint8_t backup_array[100] = {0};
uint8_t backup_array_size = 0;
uint8_t RSSI = 0;

/* Modbus sensor request packets */
uint8_t Leaf_wetness_1_query[] = {0x04, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00};
uint8_t Leaf_wetness_2_query[] = {0x08, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00};
uint8_t Soil_moisture_1_query[] = {0x02, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00};
uint8_t Soil_moisture_2_query[] = {0x06, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00};
uint8_t pH_query[] = {0x0B, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00};
uint8_t NPK1_query[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00};
uint8_t NPK2_query[] = {0x07, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00};
uint8_t CO2_cmd[] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
/* Old weather station (slave 0x05, three separate reads) - replaced by
   Weather_SMKB_modbus_query below. Kept for reference / rollback. */
// uint8_t Weather_Wi_T_RH_Pr[] = {0x05, 0x03, 0x00, 0x01, 0x00, 0x09, 0x00, 0x00};
// uint8_t Weather_rain[] = {0x05, 0x03, 0x00, 0x0C, 0x00, 0x04, 0x00, 0x00};
// uint8_t Weather_SR[] = {0x05, 0x03, 0x00, 0x21, 0x00, 0x02, 0x00, 0x00};
/* Old solar radiation query (slave 0x0B) - was never issued; replaced by
   solar_rad_modbus_query below. */
// uint8_t Solar_radiation_query[] = {0x0B, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
uint8_t PAR_query[] = {0x0A, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};

uint8_t Weather_SMKB_modbus_query[] = { 
    0x05,       // Slave address
    0x03,       // Function code
    0x00, 0x00, // Start register = 0x0000
    0x00, 0x0E, // Number of registers = 14
    0x00, 0x00  // CRC filled by Add_CRC()
};

uint8_t solar_rad_modbus_query[] = { 
    0x03,       // Slave address
    0x03,       // Function code
    0x00, 0x00, // Start register = 0x0000
    0x00, 0x01, // read 1 register
    0x85, 0xE8  
};

/* Sensor measurements and computed values */
float temperature, humidity, pH_value, soil_temp_pH, leaf_wet_1, leaf_wet_2, soil_moisture_1, soil_temp_1, soil_moisture_2, soil_temp_2, solar_radiation, voltage, battery_voltage;
float wind_speed, air_temp, air_rh, baro_pressure, rainfall, rainfall_acc, Air_solar;
uint16_t co2 = 0, N1 = 0, P1 = 0, K1 = 0, N2 = 0, P2 = 0, K2 = 0, soil_EC_1, soil_EC_2, wind_dir;

/* SMKB weather station + dedicated solar radiation sensor (Modbus) */
int16_t solar_rad = 0;                 // W/m2, from solar_rad_modbus_query
int16_t actual_wind_direction = 0;     // degrees, mechanical vane
int16_t ultrasonic_wind_direction = 0; // degrees, ultrasonic
float pressure = 0.0f;                 // hPa
float rainfall_intensity = 0.0f;       // mm/min
float accumulated_rainfall = 0.0f;     // mm
char print_char[200] = {0};
float PAR_data = 0.0;

/* Application flags */
uint8_t GSM_connected_flag = 0;
uint8_t flash_ready_flag = 0;
uint8_t Rotary_button_pressed = 0;
uint8_t wakeup_from_alarm = 0;
uint8_t bootup_flag = 0;
uint8_t RTC_alarm_flag = 0;

#if (USE_HTTP_UPLOAD == 0)
/* MQTT link (always connected) and the simulated motor - see MQTT_app_poll() */
char mqtt_device_id[24] = {0};  // "SAMBHAV_<uid>", same as the JSON Device_ID
uint8_t mqtt_connected = 0;
uint8_t motor_running = 0;
uint32_t motor_start_tick = 0;
uint32_t motor_duration_ms = 0;
uint32_t mqtt_last_check_tick = 0;
#endif

extern uint32_t flash_write_address;
extern uint32_t flash_read_address;
extern uint32_t flash_erase_address;
extern uint32_t flash_next_write_address;
extern uint32_t flash_next_read_address;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C2_Init(void);
static void MX_TIM1_Init(void);
static void MX_RTC_Init(void);
static void MX_USART4_UART_Init(void);
static void MX_USART6_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_SPI2_Init(void);
static void MX_I2C3_Init(void);
static void MX_ADC1_Init(void);
/* USER CODE BEGIN PFP */
#if (USE_HTTP_UPLOAD == 0)
uint8_t MQTT_app_connect(void);
void MQTT_app_poll(void);
#endif
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  HAL_SYSCFG_StrobeDBattpinsConfig(SYSCFG_CFGR1_UCPD1_STROBE);
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C2_Init();
  MX_TIM1_Init();
  MX_RTC_Init();
  MX_USART4_UART_Init();
  MX_USART6_UART_Init();
  MX_USART3_UART_Init();
  MX_SPI2_Init();
  MX_I2C3_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */
  GSM_init(&huart4);
  w25q32jv_flash_init(&hspi2);

  SSD1306_Init();
  SSD1306_Clear();

  CALLBACKS_RegisterTypeDef callBacks;
  callBacks.callBackPoss = rotaryPossCallBack;
  callBacks.callBackBTNpress = btnPressCallBack;
  ROT_ENC_init(&callBacks);

  bootup_flag = 1;
  printDisplay("Booting....");
  serial_print("-----------------------BOOTUP------------------\r\n\r\n\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (bootup_flag == SET || RTC_alarm_flag == SET)
    {
      /* Perform the periodic data collection and GSM upload sequence */
      App_operation();
      RTC_alarm_flag = RESET;
    }

#if (USE_HTTP_UPLOAD == 0)
    /* Between cycles: handle motor_cmd/cmd/ota, stop the motor on time */
    MQTT_app_poll();
#endif

    if (Rotary_button_pressed == 1)
    {
      /* Enter menu mode when the rotary encoder button is pressed */
      while (1)
      {
        ROT_ENC_handler();
        printHandler();
      }
    }
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
   */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Configure LSE Drive Capability
   */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief ADC1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
   */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.LowPowerAutoPowerOff = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_79CYCLES_5;
  hadc1.Init.SamplingTimeCommon2 = ADC_SAMPLETIME_3CYCLES_5;
  hadc1.Init.OversamplingMode = DISABLE;
  hadc1.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
   */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */
}

/**
 * @brief I2C2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x0010061A;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
   */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
   */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */
}

/**
 * @brief I2C3 Initialization Function
 * @param None
 * @retval None
 */
static void MX_I2C3_Init(void)
{

  /* USER CODE BEGIN I2C3_Init 0 */

  /* USER CODE END I2C3_Init 0 */

  /* USER CODE BEGIN I2C3_Init 1 */

  /* USER CODE END I2C3_Init 1 */
  hi2c3.Instance = I2C3;
  hi2c3.Init.Timing = 0x00303D5B;
  hi2c3.Init.OwnAddress1 = 0;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0;
  hi2c3.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
   */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c3, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
   */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c3, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C3_Init 2 */

  /* USER CODE END I2C3_Init 2 */
}

/**
 * @brief RTC Initialization Function
 * @param None
 * @retval None
 */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */
  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
   */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  hrtc.Init.OutPutPullUp = RTC_OUTPUT_PULLUP_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */

  /* USER CODE END Check_RTC_BKUP */

  /* USER CODE BEGIN RTC_Init 2 */
  /* USER CODE END RTC_Init 2 */
}

/**
 * @brief SPI2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7;
  hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */
}

/**
 * @brief TIM1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 39;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */
  HAL_TIM_Encoder_Start(&htim1, TIM_CHANNEL_ALL);
  /* USER CODE END TIM1_Init 2 */
}

/**
 * @brief USART3 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 9600;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */
  /* USER CODE END USART3_Init 2 */
}

/**
 * @brief USART4 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART4_UART_Init(void)
{

  /* USER CODE BEGIN USART4_Init 0 */

  /* USER CODE END USART4_Init 0 */

  /* USER CODE BEGIN USART4_Init 1 */

  /* USER CODE END USART4_Init 1 */
  huart4.Instance = USART4;
  huart4.Init.BaudRate = 115200;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART4_Init 2 */

  /* USER CODE END USART4_Init 2 */
}

/**
 * @brief USART6 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART6_UART_Init(void)
{

  /* USER CODE BEGIN USART6_Init 0 */

  /* USER CODE END USART6_Init 0 */

  /* USER CODE BEGIN USART6_Init 1 */

  /* USER CODE END USART6_Init 1 */
  huart6.Instance = USART6;
  huart6.Init.BaudRate = 115200;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  huart6.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart6.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart6.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart6) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART6_Init 2 */

  /* USER CODE END USART6_Init 2 */
}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GSM_KEY_GPIO_Port, GSM_KEY_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, I2C_Cut_Pin | RS485_Cut_Pin | FLASH_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : GSM_KEY_Pin */
  GPIO_InitStruct.Pin = GSM_KEY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GSM_KEY_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : I2C_Cut_Pin RS485_Cut_Pin */
  GPIO_InitStruct.Pin = I2C_Cut_Pin | RS485_Cut_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : Rotary_button_Pin */
  GPIO_InitStruct.Pin = Rotary_button_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(Rotary_button_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : FLASH_CS_Pin */
  GPIO_InitStruct.Pin = FLASH_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(FLASH_CS_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
  RTC_alarm_flag = SET;
}

void serial_print(char *str)
{
  HAL_UART_Transmit(&huart6, (uint8_t *)str, strlen(str), 1000);
  HAL_UART_Transmit(&huart6, (uint8_t *)"\r\n", 2, 1000);
}

void serial_print_inline(char *str) // Print on same line
{
  HAL_UART_Transmit(&huart6, (uint8_t *)str, strlen(str), 1000);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *UartHandle)
{
  GSM_UART_TxCpltCallback();
}

/**
 * @brief  Rx Transfer completed callback
 * @param  UartHandle: UART handle
 * @note   This example shows a simple way to report end of IT Rx transfer, and
 *         you can add your own implementation.
 * @retval None
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *UartHandle)
{
  GSM_UART_RxCpltCallback();
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == Rotary_button_Pin)
  {
    HAL_NVIC_DisableIRQ(EXTI4_15_IRQn);

    if (HAL_GPIO_ReadPin(Rotary_button_GPIO_Port, Rotary_button_Pin) == GPIO_PIN_RESET)
    {
      uint32_t getTick = HAL_GetTick();
      while (HAL_GPIO_ReadPin(Rotary_button_GPIO_Port, Rotary_button_Pin) == GPIO_PIN_RESET)
        ;

      /* Detect a long press for entering menu mode */
      if ((HAL_GetTick() - getTick) > 3000)
      {
        Rotary_button_pressed = 1;
      }
      else
      {
        HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
      }
    }
    else
    {
      HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
    }
  }
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
}
/**
 * @brief  UART error callbacks
 * @param  UartHandle: UART handle
 * @note   This example shows a simple way to report transfer error, and you can
 *         add your own implementation.
 * @retval None
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *UartHandle)
{
  printDisplay_level(35, "uec");
  // Error_Handler();
}

void printDisplay(char *data)
{
  SSD1306_Clear();
  SSD1306_GotoXY(0, 8);
  SSD1306_Puts(data, &Font_11x18, 1);
  SSD1306_UpdateScreen(); // display
}

void printDisplay_level(uint16_t x_coord, char *data)
{
  /* Draw text at the requested X coordinate without clearing the screen */
  SSD1306_GotoXY(x_coord, 8);
  SSD1306_Puts(data, &Font_11x18, 1);
  SSD1306_UpdateScreen();
}

void check_wakeup(void)
{
  if (__HAL_PWR_GET_FLAG(PWR_FLAG_SB) != RESET)
  {
    /* Clear Standby flag after waking from low-power standby */
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
    serial_print("-----------------------WAKEUP------------------\r\n\r\n\r\n");

    /* After waking up from low-power mode (Stop or Standby), RSF must be cleared by software.
      The software must then wait until it is set again before reading the RTC_SSR, RTC_TR and
      RTC_DR registers. */
    __HAL_RTC_WRITEPROTECTION_DISABLE(&hrtc);
    HAL_RTC_WaitForSynchro(&hrtc);
    __HAL_RTC_WRITEPROTECTION_ENABLE(&hrtc);
    __HAL_RTC_ALARM_CLEAR_FLAG(&hrtc, RTC_FLAG_ALRAF);

    wakeup_from_alarm = 1;
  }
  else
  {
    printDisplay("Booting....");
    serial_print("-----------------------BOOTUP------------------\r\n\r\n\r\n");
    wakeup_from_alarm = 0;
    bootup_flag = 1;
  }
}

void App_operation()
{
  struct tm time_tm;
  time_t ts = 0;
  uint8_t type = 0;

  /* Prepare RS485 bus and flash state */
  HAL_GPIO_WritePin(RS485_Cut_GPIO_Port, RS485_Cut_Pin, GPIO_PIN_SET);

  uid_stm32 = HAL_GetUIDw0();
  type = read_device_id();
  if (type == 0x16)
  {
    flash_ready_flag = 1;
    read_address_map();
  }
  sprintf(print_char, "Device ID %08lX \r\n", uid_stm32);
  serial_print(print_char);

  if (bootup_flag == 1)
  {
    /* The modem now stays on, so after an MCU reset it may already be running;
       pulsing PWRKEY then would switch it off. */
    // GSM_power_up();
    if (!GSM_isAlive())
      GSM_power_up();
    GSM_connected_flag = GSM_time_sync(TIME_SYNC_URL);
#if (USE_HTTP_UPLOAD == 0)
    MQTT_app_connect();
#endif
  }

  ts = RTC_read_device_time();
  localtime_r(&ts, &time_tm);

  if (time_tm.tm_hour == 3 && time_tm.tm_min == 15 && bootup_flag == 0)
  {
    write_modbus_register(0x05, 0x00F2, 0x0000);    // Reset the weather station to clear any errors
    /* Scheduled daily restart at 03:15am GMT ( 8:45am IST ) */
    printDisplay("Restart");
    serial_print("Scheduled Restart");
    HAL_NVIC_SystemReset();
  }

  if (time_tm.tm_year - 100 >= 24)
  {
    Read_data(ts);
    Oled_Data_display();
    GSM_post_sequence(ts);
    printDisplay("Sleep");
    HAL_Delay(500);
  }

  rtc_set_alarm(); /* Enter standby mode after completing the cycle */
}
void Read_data(time_t ts)
{
  HAL_StatusTypeDef ret;
  /* Must hold the largest Modbus response in use: the SMKB weather station
     returns 33 bytes (14 registers + addr/func/len/CRC). Was [24]. */
  uint8_t modbus_RX[40] = {0};
  uint8_t SCD40_RxBuffer[9] = {0};
  uint8_t SHT85_Buffer[4] = {0};
  uint8_t data[sizeof(backup_array)] = {0}; /* Temporary buffer for flash reads; sized to hold a full record read-back */
  int mb_index = 0;
  // struct tm time_tm; /* only used by the old weather-station RESET block */
  uint32_t adc_data = 0;
  uint8_t retry = 0;

  memset(backup_array, 0, sizeof(backup_array));
  backup_array_size = 0;

  /* Store timestamp header in backup buffer */
  backup_array[backup_array_size++] = 0xAA;
  backup_array[backup_array_size++] = (ts >> 24) & 0xFF;
  backup_array[backup_array_size++] = (ts >> 16) & 0xFF;
  backup_array[backup_array_size++] = (ts >> 8) & 0xFF;
  backup_array[backup_array_size++] = ts & 0xFF;

  serial_print("Reading data from sensors");
  printDisplay("Read data");

  /* Read CO2 from SCD41 over I2C, retry once if unavailable */
  while (retry < 2)
  {
    retry++;
    ret = HAL_I2C_IsDeviceReady(&hi2c3, (uint16_t)SCD40_I2C_ADDRESS << 1, 3, 5); // Check whether I2C sensor is working
    if (ret == HAL_OK)
    {
      get_scd40_data(SCD40_RxBuffer);
      break;
    }
    HAL_Delay(500);
  }

  co2 = (SCD40_RxBuffer[0] << 8) + SCD40_RxBuffer[1];
  backup_array[backup_array_size++] = SCD40_RxBuffer[0];
  backup_array[backup_array_size++] = SCD40_RxBuffer[1];

  //-------------------------------SHT45-----------------------------------------------------------------
  retry = 0;
  uint16_t temp_raw_avg = 0;
  uint16_t humid_raw_avg = 0;
  while (retry < 2)
  {
    retry++;
    ret = HAL_I2C_IsDeviceReady(&hi2c3, (uint16_t)SHT45_ADDR, 3, 5); // Check whether I2C sensor is working
    if (ret == HAL_OK)
    {
      uint16_t temp_samples[5];
      uint16_t hum_samples[5];
      uint16_t temp_raw = 0;
      uint16_t hum_raw = 0;

      // Collect 5 readings
      for (int i = 0; i < 5; i++)
      {
        Get_SHT45_values(SHT85_Buffer, &temp_raw, &hum_raw);
        temp_samples[i] = temp_raw;
        hum_samples[i] = hum_raw;
        HAL_Delay(20);
      }
      // Simple bubble sort for 5 elements
      for (int i = 0; i < 5 - 1; i++)
      {
        for (int j = 0; j < 5 - i - 1; j++)
        {
          if (temp_samples[j] > temp_samples[j + 1])
          {
            uint16_t temp = temp_samples[j];
            temp_samples[j] = temp_samples[j + 1];
            temp_samples[j + 1] = temp;
          }

          if (hum_samples[j] > hum_samples[j + 1])
          {
            uint16_t temp = hum_samples[j];
            hum_samples[j] = hum_samples[j + 1];
            hum_samples[j + 1] = temp;
          }
        }
      }

      // Median = middle value after sorting
      temp_raw_avg = temp_samples[2];
      humid_raw_avg = hum_samples[2];

      break;
    }
    HAL_Delay(500);
  }

  backup_array[backup_array_size++] = (temp_raw_avg >> 8) & 0xFF;
  backup_array[backup_array_size++] = temp_raw_avg & 0xFF;
  backup_array[backup_array_size++] = (humid_raw_avg >> 8) & 0xFF;
  backup_array[backup_array_size++] = humid_raw_avg & 0xFF;
  temperature = calc_temp_SHT45(temp_raw_avg);
  humidity = calc_humid_SHT45(humid_raw_avg);
  sprintf(print_char, "Final Median Temp= = %0.2f; RH = %0.2f\r\n", temperature, humidity);
  serial_print(print_char);

  // modbus
  HAL_Delay(500);
  // NPK
  get_modbus_data(NPK1_query, modbus_RX, 11);
  N1 = ((modbus_RX[3] << 8) + modbus_RX[4]);
  P1 = ((modbus_RX[5] << 8) + modbus_RX[6]);
  K1 = ((modbus_RX[7] << 8) + modbus_RX[8]);

  mb_index = 3;
  for (int i = backup_array_size; i < backup_array_size + 6; i++)
  {
    backup_array[i] = modbus_RX[mb_index];
    mb_index++;
  }
  backup_array_size += 6;

  HAL_Delay(500);
  // NPK2
  get_modbus_data(NPK2_query, modbus_RX, 11);
  N2 = ((modbus_RX[3] << 8) + modbus_RX[4]);
  P2 = ((modbus_RX[5] << 8) + modbus_RX[6]);
  K2 = ((modbus_RX[7] << 8) + modbus_RX[8]);

  mb_index = 3;
  for (int i = backup_array_size; i < backup_array_size + 6; i++)
  {
    backup_array[i] = modbus_RX[mb_index];
    mb_index++;
  }
  backup_array_size += 6;

  // pH
  get_modbus_data(pH_query, modbus_RX, 11);
  pH_value = ((float)((modbus_RX[3] << 8) + modbus_RX[4])) / 100;
  backup_array[backup_array_size++] = modbus_RX[3];
  backup_array[backup_array_size++] = modbus_RX[4];

  // Leaf Wetness 1
  get_modbus_data(Leaf_wetness_1_query, modbus_RX, 9);
  leaf_wet_1 = ((float)((modbus_RX[3] << 8) + modbus_RX[4])) / 10;
  backup_array[backup_array_size++] = modbus_RX[3];
  backup_array[backup_array_size++] = modbus_RX[4];

  // Leaf Wetness 2
  get_modbus_data(Leaf_wetness_2_query, modbus_RX, 9);
  leaf_wet_2 = ((float)((modbus_RX[3] << 8) + modbus_RX[4])) / 10;
  backup_array[backup_array_size++] = modbus_RX[3];
  backup_array[backup_array_size++] = modbus_RX[4];

  // SMTEC 1
  get_modbus_data(Soil_moisture_1_query, modbus_RX, 11);
  soil_moisture_1 = ((float)((modbus_RX[3] << 8) + modbus_RX[4])) / 100;
  soil_temp_1 = ((float)((modbus_RX[5] << 8) + modbus_RX[6])) / 100;
  soil_EC_1 = ((modbus_RX[7] << 8) + modbus_RX[8]);
  mb_index = 3;
  for (int i = backup_array_size; i < backup_array_size + 6; i++)
  {
    backup_array[i] = modbus_RX[mb_index];
    mb_index++;
  }
  backup_array_size += 6;

  // SMTEC 2
  get_modbus_data(Soil_moisture_2_query, modbus_RX, 11);
  soil_moisture_2 = ((float)((modbus_RX[3] << 8) + modbus_RX[4])) / 100;
  soil_temp_2 = ((float)((modbus_RX[5] << 8) + modbus_RX[6])) / 100;
  soil_EC_2 = ((modbus_RX[7] << 8) + modbus_RX[8]);

  //  HAL_GPIO_WritePin(RS485_Cut_GPIO_Port, RS485_Cut_Pin, GPIO_PIN_RESET);

  mb_index = 3;
  for (int i = backup_array_size; i < backup_array_size + 6; i++)
  {
    backup_array[i] = modbus_RX[mb_index];
    mb_index++;
  }
  backup_array_size += 6;
  /* ==================================================================
     OLD weather station read (slave 0x05, three requests, IEEE-754
     floats stored mid-little-endian). Replaced by the SMKB block below.
     ==================================================================
  // Weather Sensor
  uint8_t mb_data_sort[4];
  uint8_t index = 0;
  uint8_t wr_data_index = backup_array_size;
  get_modbus_data(Weather_Wi_T_RH_Pr, modbus_RX, 23);
  backup_array[backup_array_size++] = modbus_RX[3];
  backup_array[backup_array_size++] = modbus_RX[4];
  mb_index = 5;
  for (int ix = 0; ix < 4; ix++)
  {
    setMidLitEnd(modbus_RX, mb_data_sort, mb_index);
    index = 0;
    for (int i = backup_array_size; i < backup_array_size + 4; i++)
    {
      backup_array[i] = mb_data_sort[index];
      index++;
    }
    backup_array_size += 4;
    mb_index += 4;
  }

  // Rain
  get_modbus_data(Weather_rain, modbus_RX, 13);
  index = 0;
  mb_index = 3;
  for (int ix = 0; ix < 2; ix++)
  {
    setMidLitEnd(modbus_RX, mb_data_sort, mb_index);
    index = 0;
    for (int i = backup_array_size; i < backup_array_size + 4; i++)
    {
      backup_array[i] = mb_data_sort[index];
      index++;
    }
    backup_array_size += 4;
    mb_index += 4;
  }

  // Weather SR
  get_modbus_data(Weather_SR, modbus_RX, 9);
  setMidLitEnd(modbus_RX, mb_data_sort, 3);
  index = 0;
  for (int i = backup_array_size; i < backup_array_size + 4; i++)
  {
    backup_array[i] = mb_data_sort[index];
    index++;
  }
  backup_array_size += 4;

  wind_dir = convert_to_16bit_data(backup_array, &wr_data_index);
  wind_speed = convert_to_float_data(backup_array, &wr_data_index);
  air_temp = convert_to_float_data(backup_array, &wr_data_index);
  air_rh = convert_to_float_data(backup_array, &wr_data_index);
  baro_pressure = convert_to_float_data(backup_array, &wr_data_index);
  rainfall = convert_to_float_data(backup_array, &wr_data_index);
  rainfall_acc = 4 * convert_to_float_data(backup_array, &wr_data_index);
  Air_solar = convert_to_float_data(backup_array, &wr_data_index);

  // for weather station RESET
  ts = ts + 19800;
  localtime_r(&ts, &time_tm);

  if (time_tm.tm_hour == 8 && time_tm.tm_min == 30)
  {
    uint8_t Weather_reset_query[] = {0x3E, 0x52, 0x45, 0x53, 0x45, 0x54, 0x0D, 0x0A};
    if (HAL_UART_Transmit(&huart3, (uint8_t *)Weather_reset_query, 8, 200) != HAL_OK)
    {
      Error_Handler();
    }
    HAL_UART_Receive(&huart3, (uint8_t *)modbus_RX, 18, 1000);
  }
     ================================================================== */

  // Reading Solar Radiation
  // printDisplay("SR");
  get_modbus_data(solar_rad_modbus_query, modbus_RX, 7);

  serial_print("\r\n");
  int16_t buf1[2];
  buf1[0] = modbus_RX[3];
  buf1[1] = modbus_RX[4];
  solar_rad = (int16_t)((buf1[0] << 8) | buf1[1]);
  backup_array[backup_array_size++] = (buf1[0] & 0xFF);
  backup_array[backup_array_size++] = (buf1[1] & 0xFF);

  sprintf(print_char, "Solar Radiation: %d W/m2", solar_rad);
  serial_print(print_char);

  // Reading Weather Station Data
  // printDisplay("WS");

  get_modbus_data(Weather_SMKB_modbus_query, modbus_RX, 33);

  serial_print("\r\n");

  /* Extract registers */
  int16_t reg_temp = (int16_t)((modbus_RX[3] << 8) | modbus_RX[4]);
  int16_t reg_hum = (int16_t)((modbus_RX[5] << 8) | modbus_RX[6]);
  int16_t reg_pressure = (int16_t)((modbus_RX[15] << 8) | modbus_RX[16]);
  int16_t reg_rain_int = (int16_t)((modbus_RX[21] << 8) | modbus_RX[22]);
  int16_t reg_rain_total = (int16_t)((modbus_RX[23] << 8) | modbus_RX[24]);
  int16_t reg_wind_speed = (int16_t)((modbus_RX[25] << 8) | modbus_RX[26]);
  int16_t reg_act_dir = (int16_t)((modbus_RX[27] << 8) | modbus_RX[28]);
  int16_t reg_ultra_dir = (int16_t)((modbus_RX[29] << 8) | modbus_RX[30]);

  /* Store raw bytes into backup array */

  // Temperature
  backup_array[backup_array_size++] = modbus_RX[3];
  backup_array[backup_array_size++] = modbus_RX[4];

  // Humidity
  backup_array[backup_array_size++] = modbus_RX[5];
  backup_array[backup_array_size++] = modbus_RX[6];

  // Pressure
  backup_array[backup_array_size++] = modbus_RX[15];
  backup_array[backup_array_size++] = modbus_RX[16];

  // Rain Intensity
  backup_array[backup_array_size++] = modbus_RX[21];
  backup_array[backup_array_size++] = modbus_RX[22];

  // Accumulated Rainfall
  backup_array[backup_array_size++] = modbus_RX[23];
  backup_array[backup_array_size++] = modbus_RX[24];

  // Wind Speed
  backup_array[backup_array_size++] = modbus_RX[25];
  backup_array[backup_array_size++] = modbus_RX[26];

  // Actual Wind Direction
  backup_array[backup_array_size++] = modbus_RX[27];
  backup_array[backup_array_size++] = modbus_RX[28];

  // Ultrasonic Wind Direction
  backup_array[backup_array_size++] = modbus_RX[29];
  backup_array[backup_array_size++] = modbus_RX[30];

  /* Handle invalid values */

  if (reg_temp == 0x7FFF)
    reg_temp = 0;
  if (reg_hum == 0x7FFF)
    reg_hum = 0;
  if (reg_pressure == 0x7FFF)
    reg_pressure = 0;
  if (reg_rain_int == 0x7FFF)
    reg_rain_int = 0;
  if (reg_rain_total == 0x7FFF)
    reg_rain_total = 0;
  if (reg_wind_speed == 0x7FFF)
    reg_wind_speed = 0;
  if (reg_act_dir == 0x7FFF)
    reg_act_dir = 0;
  if (reg_ultra_dir == 0x7FFF)
    reg_ultra_dir = 0;

  /* Convert to engineering units.
     NOTE: the weather station's air temperature/humidity go to air_temp/air_rh
     (JSON "Tw"/"RHw"). The globals `temperature`/`humidity` belong to the SHT45
     canopy sensor read earlier in this function (JSON "Tc1"/"RHc1") and must
     not be overwritten here. */

  air_temp = reg_temp / 10.0f;
  air_rh = reg_hum / 10.0f;
  pressure = reg_pressure / 10.0f;
  baro_pressure = pressure; /* keep the existing JSON "BP" source in sync */
  rainfall_intensity = reg_rain_int * 0.03f;
  accumulated_rainfall = reg_rain_total / 10.0f;
  wind_speed = reg_wind_speed / 100.0f;
  actual_wind_direction = reg_act_dir;
  ultrasonic_wind_direction = reg_ultra_dir;

  /* Mirror into the legacy globals the OLED and JSON already use */
  wind_dir = (uint16_t)actual_wind_direction;
  rainfall = rainfall_intensity;
  rainfall_acc = accumulated_rainfall;
  Air_solar = (float)solar_rad;

  /* Print values */

  sprintf(print_char, "Temperature : %.1f C", air_temp);
  serial_print(print_char);

  sprintf(print_char, "Humidity : %.1f %%RH", air_rh);
  serial_print(print_char);

  sprintf(print_char, "Pressure : %.1f hPa", pressure);
  serial_print(print_char);

  sprintf(print_char, "Wind Speed : %.2f m/s", wind_speed);
  serial_print(print_char);

  sprintf(print_char, "Actual Wind Direction : %d deg", actual_wind_direction);
  serial_print(print_char);

  sprintf(print_char, "Ultrasonic Direction : %d deg", ultrasonic_wind_direction);
  serial_print(print_char);

  sprintf(print_char, "Rain Intensity : %.2f mm/min", rainfall_intensity);
  serial_print(print_char);

  sprintf(print_char, "Accumulated Rainfall : %.1f mm", accumulated_rainfall);
  serial_print(print_char);

  HAL_Delay(5000);

  // Reading PAR
  get_modbus_data(PAR_query, modbus_RX, 9);
  PAR_data = 3.33 * ((modbus_RX[3] << 8) + modbus_RX[4]);
  backup_array[backup_array_size++] = modbus_RX[3];
  backup_array[backup_array_size++] = modbus_RX[4];

  HAL_GPIO_WritePin(RS485_Cut_GPIO_Port, RS485_Cut_Pin, GPIO_PIN_RESET);

  // Reading Battery Voltage
  adc_data = ADC_read_voltage(BATTERY_VOLTAGE_CHANNEL);
  voltage = adc_data * 3.3 / 4095;
  battery_voltage = voltage * (126.0f) / 24.0f;
  backup_array[backup_array_size++] = (adc_data >> 24) & 0xFF;
  backup_array[backup_array_size++] = (adc_data >> 16) & 0xFF;
  backup_array[backup_array_size++] = (adc_data >> 8) & 0xFF;
  backup_array[backup_array_size++] = adc_data & 0xFF;

  // Reading Solar Radiation EMCON (ADC)
  adc_data = ADC_read_voltage(SOLAR_RAD_CHANNEL);
  voltage = adc_data * 3.3 / 4095;
  solar_radiation = 1500 * (voltage - 0.2) / (3.3 - 0.2);
  if (voltage <= 0.2 || solar_radiation < 3)
    solar_radiation = 0;

  backup_array[backup_array_size++] = (adc_data >> 24) & 0xFF;
  backup_array[backup_array_size++] = (adc_data >> 16) & 0xFF;
  backup_array[backup_array_size++] = (adc_data >> 8) & 0xFF;
  backup_array[backup_array_size++] = adc_data & 0xFF;

  // End of the data array
  backup_array[backup_array_size++] = 0xEE;
  if (flash_ready_flag == 1 && flash_write_address < WRITE_ADDRESS_LOCATION)
  {
    serial_print("Storing data in flash");
    write_node_data(flash_write_address, backup_array, backup_array_size, 0, 0);
    read_flash(flash_write_address, data, backup_array_size);
    flash_write_address += backup_array_size;
    write_address_map(flash_write_address, flash_read_address, flash_erase_address, time_interval, GSM_interval);
    read_flash(WRITE_ADDRESS_LOCATION, data, 3);
    backup_array_size = 0;

    // if (flash_next_write_address - flash_write_address - 1 >= flash_data_size && flash_next_write_address - flash_write_address - 1 < 2 * flash_data_size)
    if (flash_next_write_address - flash_write_address - 1 <= flash_data_size) // && flash_next_write_address - flash_write_address - 1 < 2 * flash_data_size)
    {
      flash_write_address = flash_next_write_address;
      flash_next_write_address += 256;
      write_address_map(flash_write_address, flash_read_address, flash_erase_address, time_interval, GSM_interval);
      read_flash(WRITE_ADDRESS_LOCATION, data, 3);
    }
  }
}

uint32_t ADC_read_voltage(uint32_t channel)
{
  uint32_t adc_value = 0;
  ADC_ChannelConfTypeDef sConfig = {0};
  sConfig.Channel = channel;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_ADCEx_Calibration_Start(&hadc1);
  HAL_Delay(500);
  HAL_ADC_Start(&hadc1);
  HAL_ADC_PollForConversion(&hadc1, 100);
  adc_value = HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);
  HAL_Delay(500);
  return adc_value;
}

void Oled_Data_display()
{
  if (RTC_alarm_flag == 0 || bootup_flag == 1)
  {
    serial_print("Displaying data on OLED");
    char str[20];
    sprintf(str, "BV : %0.2f", battery_voltage);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "pH : %0.2f", pH_value);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "LW1 : %0.2f", leaf_wet_1);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "LW2 : %0.2f", leaf_wet_2);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "SM1 : %0.2f", soil_moisture_1);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "ST1 : %0.2f", soil_temp_1);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "SEC1 : %d", soil_EC_1);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "SM2 : %0.2f", soil_moisture_2);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "ST2 : %0.2f", soil_temp_2);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "SEC2 : %d", soil_EC_2);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "N1 : %d", N1);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "P1 : %d", P1);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "K1 : %d", K1);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "N2 : %d", N2);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "P2 : %d", P2);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "K2 : %d", K2);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "AT : %0.2f", temperature);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "RH : %0.2f", humidity);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "CO2 : %d", co2);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "SR : %0.2f", solar_radiation);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "PAR : %0.2f", PAR_data);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "WAT : %0.2f", air_temp);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "WRH : %0.2f", air_rh);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "BP : %0.2f", baro_pressure);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "WS : %0.2f", wind_speed);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "WD : %d", wind_dir);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "RF : %0.2f", rainfall_acc);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "WSR : %d", solar_rad);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "UWD : %d", ultrasonic_wind_direction);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);

    sprintf(str, "RI : %0.2f", rainfall_intensity);
    printDisplay(str);
    HAL_Delay(DISPLAY_DELAY);
  }
}

void GSM_post_sequence(time_t ts)
{
  struct tm time_tm;
  char server_json_backup[800] = {0};
  uint8_t backup_post_retry = 0;
  uint8_t data[30] = {0}; // for read flash
  uint8_t next_GSM_post = 0;
  uint8_t GSM_Publish = 0;

  ts = ts + 19800;
  localtime_r(&ts, &time_tm);
  next_GSM_post = GSM_POST_time(ts, GSM_interval);

  if ((RTC_alarm_flag == 1 && time_tm.tm_hour == next_GSM_post && time_tm.tm_min == 00) || bootup_flag == 1)
  {
    if (bootup_flag == 1)
    {
      bootup_flag = 0;
    }
    else
    {
      // GSM_power_up();  /* Modem stays on for the MQTT link */
#if (USE_HTTP_UPLOAD == 0)
      if (!GSM_MQTT_isConnected())
        MQTT_app_connect();
#endif
      if (time_tm.tm_hour == 0)
        GSM_connected_flag = GSM_time_sync(TIME_SYNC_URL);
    }
    printDisplay("Data Post");
    serial_print("Posting data to server");
    // read data from flash
#if (USE_HTTP_UPLOAD == 1)
    while (flash_ready_flag == 1 && flash_read_address < flash_write_address)
#else
    while (flash_ready_flag == 1 && mqtt_connected == 1 && flash_read_address < flash_write_address)
#endif
    {

      memset(server_json_backup, 0, sizeof(server_json_backup));
      read_flash_create_json(flash_read_address, server_json_backup);
      HAL_Delay(100);
#if (USE_HTTP_UPLOAD == 1)
      GSM_Publish = GSM_HTTP_sendDataToServer(server_json_backup);
#else
      GSM_Publish = GSM_MQTT_publish(MQTT_TOPIC_DATA, server_json_backup, 1, 0);
#endif
      if (GSM_Publish == 1)
      {
        memset(server_json_backup, 0, sizeof(server_json_backup));
        backup_post_retry = 0;
        flash_read_address += flash_data_size; // only if gsm publish successful
        // if (flash_next_read_address - flash_read_address - 1 >= flash_data_size && flash_next_read_address - flash_read_address - 1 < 2 * flash_data_size)
        if (flash_next_read_address - flash_read_address - 1 <= flash_data_size) // && flash_next_read_address - flash_read_address - 1 < 2 * flash_data_size)
        {
          flash_read_address = flash_next_read_address;
          flash_next_read_address += 256;
        }
        if (flash_write_address == WRITE_ADDRESS_LOCATION)
        {
          flash_write_address = 0x000000;
        }
        if (flash_read_address == WRITE_ADDRESS_LOCATION)
        {
          flash_read_address = 0x000000;
        }
        write_address_map(flash_write_address, flash_read_address, flash_erase_address, time_interval, GSM_interval);
        read_flash(WRITE_ADDRESS_LOCATION, data, 3);
        read_flash(READ_ADDRESS_LOCATION, data, 3);
      }
      else
      {
        serial_print("Backup data Post: failed\r\n");
        backup_post_retry++;
        HAL_Delay(2000);
        if (backup_post_retry > 4)
          break;
      }
      HAL_Delay(500);
    }
    if (flash_read_address > flash_write_address)
    {
      serial_print("Flash Read greater than flash write.....Erasing flash\r\n");
      flash_write_address = 0x000000;
      flash_read_address = 0x000000;
      write_address_map(flash_write_address, flash_read_address, flash_erase_address, time_interval, GSM_interval);
      read_flash(WRITE_ADDRESS_LOCATION, data, 3);
      read_flash(READ_ADDRESS_LOCATION, data, 3);
    }
    // GSM_power_down();  /* Modem stays on for the MQTT link */
  }
}

#if (USE_HTTP_UPLOAD == 0)
/* ---------------------------------------------------------------------------
 * MQTT device behaviour, mirroring device_simulator.py:
 *   publishes readings on "data" (from GSM_post_sequence), subscribes to
 *   "motor_cmd", "ota" and "cmd", and reports "on"/"off" on "motor_status"
 *   (retained). Messages whose Device_ID isn't ours are ignored.
 * There is no motor hardware yet: the motor is a software timer.
 * ------------------------------------------------------------------------- */

#define MQTT_CHECK_INTERVAL_MS 30000U // Fallback poll / link check between URCs

/* Copy the value of "key" from a flat JSON object into out (quoted or bare).
   Returns 1 if the key was found. */
static uint8_t json_get_value(const char *json, const char *key, char *out, uint16_t out_size)
{
  char pattern[24];
  const char *p;
  uint16_t len = 0;

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  p = strstr(json, pattern);
  if (p == NULL)
    return 0;
  p += strlen(pattern);
  while (*p == ' ' || *p == ':')
    p++;

  if (*p == '"')
  {
    p++;
    while (p[len] != '"' && p[len] != 0 && len < out_size - 1)
      len++;
  }
  else
  {
    while (p[len] != ',' && p[len] != '}' && p[len] != 0 && len < out_size - 1)
      len++;
  }
  memcpy(out, p, len);
  out[len] = 0;
  return 1;
}

static void MQTT_publish_motor_status(void)
{
  char message[96];

  snprintf(message, sizeof(message), "{\"timestamp\":%lu,\"Device_ID\":\"%s\",\"status\":\"%s\"}",
           RTC_read_device_time(), mqtt_device_id, motor_running ? "on" : "off");
  GSM_MQTT_publish(MQTT_TOPIC_MOTOR_STATUS, message, 1, 1);
  sprintf(print_char, "Motor %s", motor_running ? "on" : "off");
  serial_print(print_char);
}

static void motor_start(uint8_t tti)
{
  motor_duration_ms = (uint32_t)tti * MQTT_TTI_UNIT_SECONDS * 1000U;
  motor_start_tick = HAL_GetTick();
  motor_running = 1;
  MQTT_publish_motor_status();
  sprintf(print_char, "Running for TTI %u (%lus)", tti, motor_duration_ms / 1000U);
  serial_print(print_char);
}

static void motor_stop(void)
{
  motor_running = 0;
  MQTT_publish_motor_status();
}

static void MQTT_handle_message(const char *topic, const char *payload)
{
  char value[160];

  if (!json_get_value(payload, "Device_ID", value, sizeof(value)) || strcmp(value, mqtt_device_id) != 0)
    return;

  if (strcmp(topic, MQTT_TOPIC_MOTOR_CMD) == 0)
  {
    if (!json_get_value(payload, "cmd", value, sizeof(value)))
      return;
    if (strcmp(value, "start") == 0)
    {
      char *start = value;
      char *end;
      long tti;

      /* Like int(str(tti).strip()): accepts 5, "5" or " 5 ", rejects anything else */
      if (!json_get_value(payload, "tti", value, sizeof(value)))
        value[0] = 0;
      while (*start == ' ')
        start++;
      tti = strtol(start, &end, 10);
      while (*end == ' ')
        end++;
      if (end == start || *end != 0)
      {
        sprintf(print_char, "Bad tti in motor_cmd: %s", value);
        serial_print(print_char);
        return;
      }
      if (tti < 0)
        tti = 0;
      if (tti > 255)
        tti = 255;
      motor_start((uint8_t)tti);
    }
    else if (strcmp(value, "stop") == 0)
    {
      motor_stop();
    }
  }
  else if (strcmp(topic, MQTT_TOPIC_CMD) == 0)
  {
    json_get_value(payload, "cmd", value, sizeof(value));
    sprintf(print_char, "Received cmd: %s", value);
    serial_print(print_char);
    if (strcmp(value, "reset") == 0)
      motor_stop();
  }
  else if (strcmp(topic, MQTT_TOPIC_OTA) == 0)
  {
    /* Logged only, as in the simulator - no OTA update is performed */
    serial_print("Received OTA:");
    serial_print((char *)payload);
  }
}

/* Connect, subscribe, and announce the motor state (retained). */
uint8_t MQTT_app_connect(void)
{
  snprintf(mqtt_device_id, sizeof(mqtt_device_id), "%s_%08lX", DEVICE_NAME, uid_stm32);
  mqtt_connected = GSM_MQTT_connect(mqtt_device_id);
  mqtt_last_check_tick = HAL_GetTick();
  if (mqtt_connected)
    MQTT_publish_motor_status();
  return mqtt_connected;
}

void MQTT_app_poll(void)
{
  char topic[32];
  char payload[512];
  uint8_t events = GSM_MQTT_eventPending();

  if (motor_running && HAL_GetTick() - motor_start_tick >= motor_duration_ms)
    motor_stop();

  /* Act at once on a URC, otherwise only every MQTT_CHECK_INTERVAL_MS, since
     each AT command costs time and a line on the serial log. */
  if (events == 0 && HAL_GetTick() - mqtt_last_check_tick < MQTT_CHECK_INTERVAL_MS)
    return;
  mqtt_last_check_tick = HAL_GetTick();

  if (!mqtt_connected || (events & MQTT_EVT_STAT) || events == 0)
  {
    if (!GSM_MQTT_isConnected())
    {
      serial_print("MQTT link down, reconnecting");
      MQTT_app_connect();
    }
    else
    {
      mqtt_connected = 1;
    }
  }

  if (mqtt_connected)
  {
    /* The modem holds at most 5 messages */
    for (uint8_t i = 0; i < 5 && GSM_MQTT_readMessage(topic, sizeof(topic), payload, sizeof(payload)); i++)
    {
      sprintf(print_char, "MQTT rx on '%s'", topic);
      serial_print(print_char);
      MQTT_handle_message(topic, payload);
    }
  }

  /* Clear the buffer so only URCs arriving from now on count as events */
  GSM_resetRxBuff();
}
#endif /* USE_HTTP_UPLOAD == 0 */

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  printDisplay_level(70, "Err");
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
