#ifndef CORE_INC_MODBUS_RTU_H_
#define CORE_INC_MODBUS_RTU_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void modbus_UART_Init(void);
void Add_CRC(uint8_t *query);
uint16_t get_CO2_data(uint8_t query[], uint8_t *rx_buf, uint8_t rx_len);
void get_modbus_data(uint8_t *query, uint8_t *rx_buf, uint8_t rx_len);
void write_modbus_register(uint8_t slave_addr, uint16_t reg_addr, uint16_t value);
void setMidLitEnd(uint8_t *arr, uint8_t *arr_sort, uint8_t start_ix);

#ifdef __cplusplus
}
#endif

#endif /* CORE_INC_MODBUS_RTU_H_ */
