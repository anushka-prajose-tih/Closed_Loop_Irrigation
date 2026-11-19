#ifndef CORE_INC_I2C_H_
#define CORE_INC_I2C_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define SHT_I2C_ADDRESS 0x81
#define SCD40_I2C_ADDRESS 0x62
#define SHT85_Write_ADDR  0x88
#define SHT85_Read_ADDR   0x89
#define SHT45_ADDR (0x44 << 1)

float get_sht_temp(uint8_t *SHT_RxBuffer);
float get_sht_humid(uint8_t *SHT_RxBuffer);
void get_scd40_data(uint8_t *data_buffer);
float calc_temp(uint16_t raw_value);
float calc_humid(uint16_t raw_value);
float calc_temp_SHT85(uint16_t raw_value);
float calc_humid_SHT85(uint16_t raw_value);
void Get_SHT85_values(uint8_t *data_buffer, float *Temperature, float *Humidity);
float calc_temp_SHT45(uint16_t raw_value);
float calc_humid_SHT45(uint16_t raw_value);
void Get_SHT85_values(uint8_t *data_buffer, float *Temperature, float *Humidity);
HAL_StatusTypeDef Get_SHT45_values(uint8_t *data_buffer, uint16_t *t_raw, uint16_t *rh_raw);

#ifdef __cplusplus
}
#endif

#endif /* CORE_INC_I2C_H_ */