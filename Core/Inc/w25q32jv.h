/*
 * w25q32jv.h
 *
 *  Created on: Sep 17, 2023
 *      Author: Dell
 */

#ifndef INC_W25Q32JV_H_
#define INC_W25Q32JV_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "main.h"
#include "stm32g0xx_hal_spi.h"

#define W25Q32JV_MAX_RECORD_SIZE 128U
#define WRITE_ADDRESS_LOCATION 0x3FF000 /* FLASH_USER_START_ADDR */
#define READ_ADDRESS_LOCATION  0x3FF003 /* FLASH_USER_START_ADDR + 4 */
#define ERASE_ADDRESS_LOCATION 0x3FF006 /* FLASH_USER_START_ADDR + 8 */
#define INTERVAL_MINUTES      0x3FF009U
#define GSM_INTERVAL          0x3FF00AU

void w25q32jv_flash_init(SPI_HandleTypeDef *hspi);
uint32_t convert_to_32bit_data(uint8_t data_arr[], uint8_t *index);
uint16_t convert_to_16bit_data(uint8_t data_arr[], uint8_t *index);
float convert_to_float_data(uint8_t data_arr[], uint8_t *index);
void write_node_data(uint32_t address, uint8_t *data, uint16_t size, uint8_t node_start, uint8_t node_end);
void read_flash_create_json(uint32_t address, char *server_json);
void Check_busy_bit(void);
uint8_t read_device_id(void);
void flash_write_enable(void);
void Erase_sector(uint32_t address);
void Chip_erase(void);
void write_flash(uint32_t address, uint8_t *data, uint16_t size);
void read_flash(uint32_t address, uint8_t *data, uint16_t size);
void write_address_map(uint32_t write_address, uint32_t read_address, uint32_t erase_address, uint8_t interval, uint8_t gsm_interval);
void read_address_map(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_W25Q32JV_H_ */
