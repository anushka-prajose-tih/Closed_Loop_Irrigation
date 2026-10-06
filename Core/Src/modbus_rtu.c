#include "modbus_rtu.h"

/* Private variables ---------------------------------------------------------*/
extern UART_HandleTypeDef huart3;

void modbus_UART_Init(void)
{

	/* USER CODE BEGIN USART1_Init 0 */

	/* USER CODE END USART1_Init 0 */

	/* USER CODE BEGIN USART1_Init 1 */

	/* USER CODE END USART1_Init 1 */
	huart3.Instance = USART3;
	huart3.Init.BaudRate = 115200;
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
	/* USER CODE BEGIN USART1_Init 2 */

	/* USER CODE END USART1_Init 2 */
}
uint16_t get_CO2_data(uint8_t query[], uint8_t *rx_buf, uint8_t rx_len)
{
	if (HAL_UART_Transmit(&huart3, (uint8_t *)query, 9, 1000) != HAL_OK)
	{
		Error_Handler();
	}

	HAL_UART_Receive(&huart3, (uint8_t *)rx_buf, rx_len, 2000);
	uint16_t co2 = (rx_buf[2] << 8) + rx_buf[3];
	return co2;
}
void get_modbus_data(uint8_t *query, uint8_t *rx_buf, uint8_t rx_len)
{
	if (query == NULL || rx_buf == NULL || rx_len == 0)
	{
		return;
	}

	uint8_t retry = 0;
	memset(rx_buf, 0, rx_len);
	Add_CRC(query);

	while (retry < 2)
	{
		HAL_UART_Abort(&huart3);
		HAL_Delay(500);
		HAL_UART_Transmit(&huart3, query, 8, 2000);
		HAL_UART_Receive(&huart3, rx_buf, rx_len, 4000);
		HAL_Delay(500);

		if (rx_buf[0] == query[0])
		{
			return;
		}

		memset(rx_buf, 0, rx_len);
		retry++;
	}
}

void write_modbus_register(uint8_t slave_addr, uint16_t reg_addr, uint16_t value)
{
	uint8_t tx_buf[8];
	uint8_t rx_buf[8];

	memset(rx_buf, 0, sizeof(rx_buf));

	tx_buf[0] = slave_addr;			// Slave Address
	tx_buf[1] = 0x06;				// Write Single Register
	tx_buf[2] = (reg_addr >> 8);	// Register Address High
	tx_buf[3] = (reg_addr & 0xFF);	// Register Address Low
	tx_buf[4] = (value >> 8);		// Data High
	tx_buf[5] = (value & 0xFF);		// Data Low

	Add_CRC(tx_buf);				// Fill tx_buf[6] and tx_buf[7]

	HAL_UART_Abort(&huart3);
	HAL_Delay(500);					// Warm-up: RS485 bus may have just been powered on

	HAL_UART_Transmit(&huart3, tx_buf, 8, 2000);
	HAL_UART_Receive(&huart3, rx_buf, 8, 2000);

	// Verify response
	if ((rx_buf[0] == slave_addr) &&
		(rx_buf[1] == 0x06) &&
		(rx_buf[2] == tx_buf[2]) &&
		(rx_buf[3] == tx_buf[3]))
	{
		serial_print("Modbus register write OK");
	}
	else
	{
		serial_print("Modbus register write FAILED");
	}
}

void setMidLitEnd(uint8_t *arr, uint8_t *arr_sort, uint8_t start_ix)
{
	arr_sort[1] = arr[start_ix];
	arr_sort[0] = arr[start_ix + 1];
	arr_sort[3] = arr[start_ix + 2];
	arr_sort[2] = arr[start_ix + 3];
}

void Add_CRC(uint8_t *query) //++ Funtion to Add CRC Check for every query
{
	uint16_t crc = 0xFFFF;

	for (int pos = 0; pos < 6; pos++)
	{
		crc ^= (uint16_t)query[pos]; // XOR byte into least sig. byte of crc

		for (int i = 8; i != 0; i--)
		{
			// Loop over each bit
			if ((crc & 0x0001) != 0)
			{
				// If the LSB is set
				crc >>= 1; // Shift right and XOR 0xA001
				crc ^= 0xA001;
			}
			else		   // Else LSB is not set
				crc >>= 1; // Just shift right
		}
	}
	// Note, this number has low and high bytes swapped, so use it accordingly (or swap bytes)
	query[6] = *((char *)&crc + 0); //&crc+0; //*((char*)&crc+0);
	query[7] = *((char *)&crc + 1); //&crc+1; //*((char*)&crc+1);
}
