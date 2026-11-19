#include "i2c.h"

extern I2C_HandleTypeDef hi2c3;

//-------------SHT21-----------
uint8_t SHT_Temp[] = {0xE3};
uint8_t SHT_RH[] = {0xE5};
//----------------------------

float get_sht_temp(uint8_t *SHT_RxBuffer)
{
	float temp;
	do
	{
		HAL_I2C_Master_Transmit(&hi2c3, (uint16_t)SHT_I2C_ADDRESS, (uint8_t *)SHT_Temp, 1, 100);

		while (HAL_I2C_GetState(&hi2c3) != HAL_I2C_STATE_READY)
		{
		}

		/* When Acknowledge failure occurs (Slave don't acknowledge it's address)
		   Master restarts communication */
	} while (HAL_I2C_GetError(&hi2c3) == HAL_I2C_ERROR_AF);

	/*##- Put I2C peripheral in reception process ###########################*/
	do
	{
		HAL_I2C_Master_Receive(&hi2c3, (uint16_t)SHT_I2C_ADDRESS, (uint8_t *)SHT_RxBuffer, 3, 100);

		while (HAL_I2C_GetState(&hi2c3) != HAL_I2C_STATE_READY)
		{
		}
		temp = -46.85 + 175.72 * (float)((SHT_RxBuffer[0] << 8) + SHT_RxBuffer[1]) / 65536;
		printf("Done");
		/* When Acknowledge failure occurs (Slave don't acknowledge it's address)
		   Master restarts communication */
	} while (HAL_I2C_GetError(&hi2c3) == HAL_I2C_ERROR_AF);
	return temp;
}
float get_sht_humid(uint8_t *SHT_RxBuffer)
{
	float humid;
	do
	{
		HAL_I2C_Master_Transmit(&hi2c3, (uint16_t)SHT_I2C_ADDRESS, (uint8_t *)SHT_RH, 1, 100);

		while (HAL_I2C_GetState(&hi2c3) != HAL_I2C_STATE_READY)
		{
		}

		/* When Acknowledge failure occurs (Slave don't acknowledge it's address)
		   Master restarts communication */
	} while (HAL_I2C_GetError(&hi2c3) == HAL_I2C_ERROR_AF);

	/*##- Put I2C peripheral in reception process ###########################*/
	do
	{
		HAL_I2C_Master_Receive(&hi2c3, (uint16_t)SHT_I2C_ADDRESS, (uint8_t *)SHT_RxBuffer, 3, 100);

		while (HAL_I2C_GetState(&hi2c3) != HAL_I2C_STATE_READY)
		{
		}
		humid = -6 + 125 * (float)((SHT_RxBuffer[0] << 8) + SHT_RxBuffer[1]) / 65536;
		printf("Done");
		/* When Acknowledge failure occurs (Slave don't acknowledge it's address)
		   Master restarts communication */
	} while (HAL_I2C_GetError(&hi2c3) == HAL_I2C_ERROR_AF);
	return humid;
}

void get_scd40_data(uint8_t *data_buffer)
{
	HAL_StatusTypeDef ret;

	uint8_t Read_data[] = {0xec, 0x05};

	// SCD41 only
	uint8_t Wake_up[] = {0x36, 0xf6};
	uint8_t measure_single_shot[] = {0x21, 0x9d};
	uint8_t Power_down[] = {0x36, 0xe0};

	//------------------------------------------------------SCD40-----------------------------------------------
	/*
	uint8_t Read_start[] = {0x21, 0xac};
	uint8_t Read_end[] = {0x3f, 0x86};
	ret = HAL_I2C_Master_Transmit(&hi2c3, (uint16_t)SCD40_I2C_ADDRESS << 1, (uint8_t *)Read_start, 2, 2000);
	printf("Start Measurement %02X\r\n", ret);
	HAL_Delay(5000);

	ret = HAL_I2C_Master_Transmit(&hi2c3, (uint16_t)SCD40_I2C_ADDRESS << 1, (uint8_t *)Read_data, 2, 2000);
	printf("Read measurement %02X\r\n", ret);
	HAL_Delay(100);

	// Receive data
	ret = HAL_I2C_Master_Receive(&hi2c3, (uint16_t)SCD40_I2C_ADDRESS << 1, (uint8_t *)data_buffer, 9, 2000);

	// End measurement
	ret = HAL_I2C_Master_Transmit(&hi2c3, (uint16_t)SCD40_I2C_ADDRESS << 1, (uint8_t *)Read_end, 2, 2000);
	*/
	//-----------------------------------------------------------------------------------------------------------------------------

	//----------------------------------------------------------SCD41--------------------------------------------------------------
	ret = HAL_I2C_Master_Transmit(&hi2c3, (uint16_t)SCD40_I2C_ADDRESS << 1, (uint8_t *)Wake_up, 2, 2000);
	printf("Start Measurement %02X\r\n", ret);
	HAL_Delay(100);

	ret = HAL_I2C_Master_Transmit(&hi2c3, (uint16_t)SCD40_I2C_ADDRESS << 1, (uint8_t *)measure_single_shot, 2, 2000);
	printf("Start Measurement %02X\r\n", ret);
	HAL_Delay(5000);

	ret = HAL_I2C_Master_Transmit(&hi2c3, (uint16_t)SCD40_I2C_ADDRESS << 1, (uint8_t *)Read_data, 2, 2000);
	printf("Read measurement %02X\r\n", ret);
	HAL_Delay(100);

	// Receive data
	ret = HAL_I2C_Master_Receive(&hi2c3, (uint16_t)SCD40_I2C_ADDRESS << 1, (uint8_t *)data_buffer, 9, 2000);

	// End measurement
	ret = HAL_I2C_Master_Transmit(&hi2c3, (uint16_t)SCD40_I2C_ADDRESS << 1, (uint8_t *)Power_down, 2, 2000);
	//-----------------------------------------------------------------------------------------------------------------------------
}

float calc_temp(uint16_t raw_value)
{
	if (raw_value == 0)
		return raw_value;
	else
	{
		float temp = -46.85 + (175.72 * (float)raw_value / 65536);

		return temp;
	}
}
float calc_humid(uint16_t raw_value)
{
	if (raw_value == 0)
		return raw_value;
	else
	{
		float humid = -6 + (125 * (float)raw_value / 65536);
		return humid;
	}
}

float calc_temp_SHT85(uint16_t raw_value)
{
	if (raw_value == 0)
		return raw_value;
	else
	{
		float temp = -45.0f + (175.0f * (float)raw_value / 65535.0f);
		if (temp < 0.0f)
			return 0.0f;
		else
			return temp;
	}
}
float calc_humid_SHT85(uint16_t raw_value)
{
	if (raw_value == 0)
		return raw_value;
	else
	{
		float humid = (100.0f * (float)raw_value / 65535.0f);
		if (humid < 0.0f)
			return 0.0f;
		else if (humid > 100.0f)
			return 100.0f;
		else
			return humid;
	}
}

void Get_SHT85_values(uint8_t *data_buffer, float *Temperature, float *Humidity)
{
	uint8_t CMD[2];
	uint8_t DATA[10];
	uint16_t raw_temp = 0;
	uint16_t raw_humidity = 0;
	char print_char[200] = {0};

	CMD[0] = 0x24;
	CMD[1] = 0x00;
	HAL_I2C_Master_Transmit(&hi2c3, SHT85_Write_ADDR, (uint8_t *)CMD, 2, 1000);
	HAL_Delay(50);

	HAL_I2C_Master_Receive(&hi2c3, SHT85_Read_ADDR, (uint8_t *)DATA, sizeof(DATA), 1000);

	data_buffer[0] = DATA[0];
	data_buffer[1] = DATA[1];
	data_buffer[2] = DATA[3];
	data_buffer[3] = DATA[4];
	raw_temp = (DATA[0] << 8) + DATA[1];
	raw_humidity = (DATA[3] << 8) + DATA[4];

	*Temperature = raw_temp * (175.0 / 65535) - 45; //  °C
	*Humidity = raw_humidity * (100.0 / 65535);		//  %
	sprintf(print_char, "Temp= = %0.2f; RH = %0.2f\r\n", *Temperature, *Humidity);
	serial_print(print_char);

	HAL_Delay(100);
}

/* CRC-8 over each 16-bit word, per SHT4x datasheet section 4.4: polynomial
   0x31, init 0xFF, no input/output reflection, no final XOR. CRC(0xBEEF) = 0x92. */
static uint8_t sht4x_crc8(const uint8_t *data, uint8_t len)
{
	uint8_t crc = 0xFF;

	for (uint8_t i = 0; i < len; i++)
	{
		crc ^= data[i];
		for (uint8_t bit = 0; bit < 8; bit++)
		{
			crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
		}
	}
	return crc;
}

/* Returns HAL_OK only when both words arrived and both checksums matched; the
   outputs are zeroed on every failure path. The sensor NACKs the read header
   while it is still measuring (datasheet section 4.1), so a failed read is an
   expected event and must never be decoded as if it were a reading. */
HAL_StatusTypeDef Get_SHT45_values(uint8_t *data_buffer, uint16_t *t_raw, uint16_t *rh_raw)
{
	uint8_t cmd = 0xFD; // High precision measurement
	uint8_t DATA[6] = {0};
	char print_char[200] = {0};
	float Temperature = 0.0f, Humidity = 0.0f;
	HAL_StatusTypeDef status;

	data_buffer[0] = 0;
	data_buffer[1] = 0;
	data_buffer[2] = 0;
	data_buffer[3] = 0;
	*t_raw = 0;
	*rh_raw = 0;

	status = HAL_I2C_Master_Transmit(&hi2c3, SHT45_ADDR, &cmd, 1, HAL_MAX_DELAY);
	if (status != HAL_OK)
	{
		serial_print("SHT45: measure command not acknowledged");
		return status;
	}

	HAL_Delay(10); /* tMEAS,h is 8.3 ms max */

	status = HAL_I2C_Master_Receive(&hi2c3, SHT45_ADDR, DATA, 6, HAL_MAX_DELAY);
	if (status != HAL_OK)
	{
		serial_print("SHT45: no response to read header");
		return status;
	}

	if (sht4x_crc8(&DATA[0], 2) != DATA[2] || sht4x_crc8(&DATA[3], 2) != DATA[5])
	{
		serial_print("SHT45: CRC mismatch, sample discarded");
		return HAL_ERROR;
	}

	data_buffer[0] = DATA[0];
	data_buffer[1] = DATA[1];
	data_buffer[2] = DATA[3];
	data_buffer[3] = DATA[4];

	*t_raw = (DATA[0] << 8) + DATA[1];
	*rh_raw = (DATA[3] << 8) + DATA[4];

	Temperature = -45.0f + 175.0f * ((float)*t_raw / 65535.0f); //  °C
	Humidity = -6.0f + 125.0f * ((float)*rh_raw / 65535.0f);	//  %
	sprintf(print_char, "Temp= = %0.2f; RH = %0.2f\r\n", Temperature, Humidity);
	serial_print(print_char);

	HAL_Delay(100);
	return HAL_OK;
}

float calc_temp_SHT45(uint16_t raw_value)
{
	if (raw_value == 0)
		return raw_value;
	else
	{
		float temp = -45.0f + (175.0f * (float)raw_value / 65535.0f);
		if (temp < 0.0f)
			return 0.0f;
		else
			return temp;
	}
}
float calc_humid_SHT45(uint16_t raw_value)
{
	if (raw_value == 0)
		return raw_value;
	else
	{
		float humid = -6.0f + (125.0f * (float)raw_value / 65535.0f);
		if (humid < 0.0f)
			return 0.0f;
		else if (humid > 100.0f)
			return 100.0f;
		else
			return humid;
	}
}
