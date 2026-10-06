/*
 * w25q32jv.c
 *
 *  Created on: Sep 17, 2023
 *      Author: Dell
 */

#include "w25q32jv.h"

extern uint8_t flash_data_size;

/* Private variables-------------------------------*/
uint8_t dummy[] = {0xA5};

SPI_HandleTypeDef *hspi;
uint32_t flash_write_address = 0x000000;
uint32_t flash_read_address = 0x000000;
uint32_t flash_erase_address = 0x000000;
uint32_t flash_next_write_address = 0;
uint32_t flash_next_read_address = 0;

extern uint32_t uid_stm32;
extern uint8_t RSSI;
extern uint8_t time_interval;
extern uint8_t GSM_interval;
extern uint8_t flash_ready_flag;

/* Private user code ---------------------------------------------------------*/
uint32_t convert_to_32bit_data(uint8_t data_arr[], uint8_t *ix)
{
	if (data_arr[*ix] == 0xFF && data_arr[*ix + 1] == 0xFF && data_arr[*ix + 2] == 0xFF && data_arr[*ix + 3] == 0xFF)
	{
		*ix += 4;
		return 0;
	}
	union ByteToUint32 converter32;
	converter32.bytes[3] = data_arr[*ix]; // Most significant byte
	converter32.bytes[2] = data_arr[*ix + 1];
	converter32.bytes[1] = data_arr[*ix + 2];
	converter32.bytes[0] = data_arr[*ix + 3]; // Least significant byte
	*ix += 4;

	return converter32.value;
}
uint16_t convert_to_16bit_data(uint8_t *data_arr, uint8_t *ix)
{
	union ByteToUint16 converter16;

	converter16.bytes[1] = data_arr[*ix];
	converter16.bytes[0] = data_arr[*ix + 1]; // Least significant byte
	*ix += 2;
	if (converter16.value < 0)
	{
		converter16.value = 0;
	}
	return converter16.value;
}
float convert_to_float_data(uint8_t data_arr[], uint8_t *ix)
{
	union ByteToFloat converterFl;
	converterFl.bytes[0] = data_arr[*ix]; // Most significant byte
	converterFl.bytes[1] = data_arr[*ix + 1];
	converterFl.bytes[2] = data_arr[*ix + 2];
	converterFl.bytes[3] = data_arr[*ix + 3]; // Least significant byte
	*ix += 4;

	if (converterFl.value < 0.0f || converterFl.value > 10000.0f)
	{
		converterFl.value = 0.0f;
	}

	return converterFl.value;
}

void w25q32jv_flash_init(SPI_HandleTypeDef *_hspi)
{
	hspi = _hspi;
}
void write_node_data(uint32_t address, uint8_t *data, uint16_t size, uint8_t node_start, uint8_t node_end)
{
	uint8_t start_bit[] = {0xAA};
	uint8_t end_bit[] = {0xEE};
	uint8_t read_buf[20];
	char print_char[30];
	if (address % 0x1000 == 0)
	{
		sprintf(print_char, "Erasing Sector %06lX \r\n", address);
		serial_print(print_char);
		Erase_sector(address);
	}
	if (node_start == 1)
	{
		write_flash(address, start_bit, 1);
		read_flash(address, read_buf, 1);
		address += 1;
	}
	write_flash(address, data, size);

	if (node_end == 1)
	{
		address += 1;
		printf("write end bit\r\n");
		write_flash(address, end_bit, 1);
	}
}
void read_flash_create_json(uint32_t address, char *server_json)
{

	if (server_json == NULL)
	{
		return;
	}

	uint32_t ts = 0;
	uint8_t index = 0;
	uint8_t read_flash_size = flash_data_size;
	if ((read_flash_size == 0U) || (read_flash_size > W25Q32JV_MAX_RECORD_SIZE))
	{
		serial_print("Invalid flash data size\r\n");
		server_json[0] = '\0';
		return;
	}

	uint8_t data[W25Q32JV_MAX_RECORD_SIZE] = {0};
	float temperature = 0.0, humidity = 0.0, pH_value = 0.0, leaf_wet_1 = 0.0, leaf_wet_2 = 0.0, soil_moisture_1 = 0.0, soil_temp_1 = 0.0, soil_moisture_2 = 0.0, soil_temp_2 = 0.0, solar_radiation = 0.0, voltage = 0.0, battery_voltage = 0.0;
	float wind_speed = 0.0f, air_temp = 0.0f, air_rh = 0.0f, baro_pressure = 0.0f, rainfall = 0.0f, rainfall_acc = 0.0f, Air_solar = 0.0f;
	uint16_t co2 = 0, N1 = 0, P1 = 0, K1 = 0, N2 = 0, P2 = 0, K2 = 0, soil_EC_1 = 0, soil_EC_2 = 0, wind_dir = 0;
	uint32_t adc_value = 0;
	float PAR_data = 0.0;
	/* SMKB weather station / dedicated solar sensor (signed 16-bit registers) */
	int16_t solar_rad = 0, ultrasonic_wind_direction = 0, reg = 0;

	memset(data, 0, sizeof(data));
	read_flash(address, data, read_flash_size);
	if (data[0] == 0xAA)
	{
		index = 1;
		ts = convert_to_32bit_data(data, &index);
	}
	if (data[read_flash_size - 1] == 0xEE)
	{
		index = 5;
		// SCD41------------------------------------------------------------
		co2 = convert_to_16bit_data(data, &index);

		// SHT85--------------------------------------------------------
		//temperature = calc_temp_SHT85(convert_to_16bit_data(data, &index));
		//humidity = calc_humid_SHT85(convert_to_16bit_data(data, &index));
		temperature = calc_temp_SHT45(convert_to_16bit_data(data, &index));
		humidity = calc_humid_SHT45(convert_to_16bit_data(data, &index));
		//--------------------------------------------------------------------

		N1 = convert_to_16bit_data(data, &index);
		P1 = convert_to_16bit_data(data, &index);
		K1 = convert_to_16bit_data(data, &index);
		N2 = convert_to_16bit_data(data, &index);
		P2 = convert_to_16bit_data(data, &index);
		K2 = convert_to_16bit_data(data, &index);
		pH_value = ((float)convert_to_16bit_data(data, &index)) / 100.0f;
		leaf_wet_1 = ((float)convert_to_16bit_data(data, &index)) / 10.0f;
		leaf_wet_2 = ((float)convert_to_16bit_data(data, &index)) / 10.0f;
		soil_moisture_1 = ((float)convert_to_16bit_data(data, &index)) / 100.0f;
		soil_temp_1 = ((float)convert_to_16bit_data(data, &index)) / 100.0f;
		soil_EC_1 = convert_to_16bit_data(data, &index);
		soil_moisture_2 = ((float)convert_to_16bit_data(data, &index)) / 100.0f;
		soil_temp_2 = ((float)convert_to_16bit_data(data, &index)) / 100.0f;
		soil_EC_2 = convert_to_16bit_data(data, &index);

		/* ---- OLD weather station decode (IEEE-754 floats, slave 0x05) ----
		wind_dir = convert_to_16bit_data(data, &index);
		wind_speed = convert_to_float_data(data, &index);
		air_temp = convert_to_float_data(data, &index);
		air_rh = convert_to_float_data(data, &index);
		baro_pressure = convert_to_float_data(data, &index);
		rainfall = convert_to_float_data(data, &index);
		rainfall_acc = 4.0f * convert_to_float_data(data, &index);
		Air_solar = convert_to_float_data(data, &index);
		------------------------------------------------------------------- */

		/* ---- NEW: dedicated solar sensor + SMKB weather station ----
		   All registers are signed 16-bit; 0x7FFF means "no reading".
		   Order and scaling must mirror Read_data() in main.c exactly. */
		solar_rad = (int16_t)convert_to_16bit_data(data, &index);
		reg = (int16_t)convert_to_16bit_data(data, &index);
		air_temp = (reg == 0x7FFF) ? 0.0f : (reg / 10.0f);
		reg = (int16_t)convert_to_16bit_data(data, &index);
		air_rh = (reg == 0x7FFF) ? 0.0f : (reg / 10.0f);
		reg = (int16_t)convert_to_16bit_data(data, &index);
		baro_pressure = (reg == 0x7FFF) ? 0.0f : (reg / 10.0f);
		reg = (int16_t)convert_to_16bit_data(data, &index);
		rainfall = (reg == 0x7FFF) ? 0.0f : (reg * 0.03f);
		reg = (int16_t)convert_to_16bit_data(data, &index);
		rainfall_acc = (reg == 0x7FFF) ? 0.0f : (reg / 10.0f);
		reg = (int16_t)convert_to_16bit_data(data, &index);
		wind_speed = (reg == 0x7FFF) ? 0.0f : (reg / 100.0f);
		reg = (int16_t)convert_to_16bit_data(data, &index);
		wind_dir = (reg == 0x7FFF) ? 0 : (uint16_t)reg;
		reg = (int16_t)convert_to_16bit_data(data, &index);
		ultrasonic_wind_direction = (reg == 0x7FFF) ? 0 : reg;

		Air_solar = (float)solar_rad;
		/* ------------------------------------------------------------- */

		PAR_data = 3.33f * (float)convert_to_16bit_data(data, &index);
		adc_value = convert_to_32bit_data(data, &index);
		voltage = adc_value * 3.3f / 4095.0f;
		battery_voltage = voltage * 126.0f / 24.0f;
		adc_value = convert_to_32bit_data(data, &index);
		voltage = adc_value * 3.3f / 4095.0f;
		solar_radiation = 1500.0f * (voltage - 0.2f) / (3.3f - 0.2f);
		if ((voltage <= 0.2f) || (solar_radiation < 3.0f))
		{
			solar_radiation = 0.0f;
		}
	}
	else
	{
		serial_print("Invalid data...Sending 0\r\n");
	}
	sprintf(server_json, "{\"timestamp\":%lu,\"Device_ID\":\"%s_%08lX\",\"rssi\":%d,\"battery_Volt\": %0.2f,\"soil\":{\"SM1\":%0.2f,\"ST1\":%0.2f,\"SEC1\":%d,\"N1\":%d,\"P1\":%d,\"K1\":%d,\"pH1\":%0.2f,\"LW1\":%0.2f,\"SM2\":%0.2f,\"ST2\":%0.2f,\"SEC2\":%d,\"N2\":%d,\"P2\":%d,\"K2\":%d,\"pH2\":0,\"LW2\":%0.2f},\"weather\":{\"Tw\":%0.2f,\"RHw\":%0.2f,\"BP\":%0.2f,\"RF\":%0.2f,\"WS\":%0.2f,\"WD\":%d,\"UWD\":%d,\"SR\":%0.2f},\"canopy\":{\"Tc1\":%0.2f,\"RHc1\":%0.2f,\"CO2c1\":%d,\"Tc2\":0,\"RHc2\":0,\"CO2c2\":0,\"Tc3\":0,\"RHc3\":0,\"CO2c3\":0,\"O2c\":0,\"SR\":%0.2f,\"UV\":0,\"PAR\":%0.2f,\"Tb\":0,\"BD\":0,\"FD\":0,\"LAI\":0,\"NDVI\":%0.2f}}", ts, DEVICE_NAME, uid_stm32, RSSI, battery_voltage, soil_moisture_1, soil_temp_1, soil_EC_1, N1, P1, K1, pH_value, leaf_wet_1, soil_moisture_2, soil_temp_2, soil_EC_2, N2, P2, K2, leaf_wet_2, air_temp, air_rh, baro_pressure, rainfall_acc, wind_speed, wind_dir, ultrasonic_wind_direction, Air_solar, temperature, humidity, co2, solar_radiation, PAR_data, rainfall);
	serial_print(server_json);
}

void Check_busy_bit()
{
	HAL_Delay(1);
	// Read Status Register
	uint8_t busy_reg[1] = {0x05};
	uint8_t busy_bit[1] = {0x00};
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(hspi, busy_reg, 1, HAL_MAX_DELAY);
	HAL_SPI_Receive(hspi, (uint8_t *)busy_bit, 1, HAL_MAX_DELAY);
	printf("Busy bit : %02X\r\n", busy_bit[0]);
	do
	{
		HAL_SPI_Receive(hspi, (uint8_t *)busy_bit, 1, HAL_MAX_DELAY);
		HAL_Delay(1);
	} while ((busy_bit[0] & 0x01) == 0x01);
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
	HAL_Delay(5);
}
uint8_t read_device_id()
{
	HAL_Delay(10);
	printf("Start flash read\r\n");
	uint8_t read_dev_id[] = {0x9F};
	uint8_t aRxBuffer[3] = {0x00, 0x00, 0x00};
	// Transmit read command
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(hspi, (uint8_t *)read_dev_id, 1, HAL_MAX_DELAY);
	HAL_SPI_Receive(hspi, (uint8_t *)aRxBuffer, 3, HAL_MAX_DELAY);
	HAL_Delay(1000);
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);

	printf("Device id: %02X  %02X  %02X \r\n ", aRxBuffer[0], aRxBuffer[1], aRxBuffer[2]);

	return aRxBuffer[2];
}

void flash_write_enable()
{
	uint8_t write_enable[] = {0x06};
	Check_busy_bit();

	// write enable
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(hspi, write_enable, 1, HAL_MAX_DELAY);
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
	HAL_Delay(1);
}

void Erase_sector(uint32_t address) // 4kB eraseB
{
	uint8_t addr_byte[3];
	addr_byte[0] = ((address >> 16) & 0xFF);
	addr_byte[1] = ((address >> 8) & 0xFF);
	addr_byte[2] = (address & 0xFF);

	uint8_t instr[1] = {0x20}; // erase sector

	flash_write_enable();
	// block erase function
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(hspi, (uint8_t *)instr, 1, HAL_MAX_DELAY);
	HAL_SPI_Transmit(hspi, (uint8_t *)addr_byte, 3, HAL_MAX_DELAY);
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
	HAL_Delay(100);
}

void Chip_erase(void) // entire memory erase
{
	uint8_t instr[1] = {0xC7}; // erase sector

	flash_write_enable();
	// block erase function
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(hspi, (uint8_t *)instr, 1, HAL_MAX_DELAY);
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
	HAL_Delay(100);
}

void write_flash(uint32_t address, uint8_t *data, uint16_t size)
{
	uint8_t addr_byte[3];
	addr_byte[0] = ((address >> 16) & 0xFF);
	addr_byte[1] = ((address >> 8) & 0xFF);
	addr_byte[2] = (address & 0xFF);

	uint8_t instr[1] = {0x02}; // write flash
	flash_write_enable();
	// Transmit write data
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);

	HAL_SPI_Transmit(hspi, (uint8_t *)instr, 1, HAL_MAX_DELAY);
	HAL_SPI_Transmit(hspi, (uint8_t *)addr_byte, 3, HAL_MAX_DELAY);
	HAL_SPI_Transmit(hspi, (uint8_t *)data, size, HAL_MAX_DELAY);

	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
	HAL_Delay(100);
}

void read_flash(uint32_t address, uint8_t *data, uint16_t size)
{
	char print_char[100];
	uint8_t addr_byte[3];
	addr_byte[0] = ((address >> 16) & 0xFF);
	addr_byte[1] = ((address >> 8) & 0xFF);
	addr_byte[2] = (address & 0xFF);

	uint8_t instr[1] = {0x0B};

	Check_busy_bit();

	// read data
	uint8_t dummy_zero = 0;
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(hspi, (uint8_t *)instr, 1, HAL_MAX_DELAY);
	HAL_SPI_Transmit(hspi, (uint8_t *)addr_byte, 3, HAL_MAX_DELAY);
	HAL_SPI_Transmit(hspi, &dummy_zero, 1, HAL_MAX_DELAY);
	HAL_SPI_TransmitReceive(hspi, dummy, (uint8_t *)data, size, HAL_MAX_DELAY);
	HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
	serial_print("Read flash data: \r\n ");
	memset(print_char, 0, sizeof(print_char));
	for (int i = 0; i < size; i++)
	{
		sprintf(print_char, "%02X ", data[i]);
		serial_print_inline(print_char);
	}

	serial_print("\r\n ");
}

void write_address_map(uint32_t write_address, uint32_t read_address, uint32_t erase_address, uint8_t interval, uint8_t gsm_interval)
{
	uint8_t addr_byte[3];
	uint8_t interval_array[1];
	addr_byte[0] = ((write_address >> 16) & 0xFF);
	addr_byte[1] = ((write_address >> 8) & 0xFF);
	addr_byte[2] = (write_address & 0xFF);

	//	Erasing Sector 
	Erase_sector(WRITE_ADDRESS_LOCATION);
	HAL_Delay(1000);

	//	Writing at WRITE_ADDRESS_LOCATION
	write_flash(WRITE_ADDRESS_LOCATION, addr_byte, 3);

	addr_byte[0] = ((read_address >> 16) & 0xFF);
	addr_byte[1] = ((read_address >> 8) & 0xFF);
	addr_byte[2] = (read_address & 0xFF);
	write_flash(READ_ADDRESS_LOCATION, addr_byte, 3);

	addr_byte[0] = ((erase_address >> 16) & 0xFF);
	addr_byte[1] = ((erase_address >> 8) & 0xFF);
	addr_byte[2] = (erase_address & 0xFF);
	write_flash(ERASE_ADDRESS_LOCATION, addr_byte, 3);

	interval_array[0] = interval;
	write_flash(INTERVAL_MINUTES, interval_array, 1);

	interval_array[0] = gsm_interval;
	write_flash(GSM_INTERVAL, interval_array, 1);

}

void read_address_map()
{
	printf("Reading the address pointers of read/write functions\r\n");
	uint8_t data[3];
	read_flash(WRITE_ADDRESS_LOCATION, data, 3);
	if (data[0] != 0xFF || data[1] != 0xFF || data[2] != 0xFF)
		flash_write_address = (data[0] << 16) + (data[1] << 8) + data[2];

	read_flash(READ_ADDRESS_LOCATION, data, 3);
	if (data[0] != 0xFF || data[1] != 0xFF || data[2] != 0xFF)
		flash_read_address = (data[0] << 16) + (data[1] << 8) + data[2];

	read_flash(ERASE_ADDRESS_LOCATION, data, 3);
	if (data[0] != 0xFF || data[1] != 0xFF || data[2] != 0xFF)
		flash_erase_address = (data[0] << 16) + (data[1] << 8) + data[2];

	read_flash(INTERVAL_MINUTES, data, 1);
	if (data[0] != 0xFF)
		time_interval = data[0];

	read_flash(GSM_INTERVAL, data, 1);
	if (data[0] != 0xFF)
		GSM_interval = data[0];

	flash_next_write_address = ((flash_write_address / 256) * 256) + 256;
	flash_next_read_address = ((flash_read_address / 256) * 256) + 256;
}
