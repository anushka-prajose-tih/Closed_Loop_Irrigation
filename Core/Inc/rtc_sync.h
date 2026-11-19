#ifndef CORE_INC_RTC_SYNC_H_
#define CORE_INC_RTC_SYNC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <time.h>

typedef struct
{
	uint8_t hours;
	uint8_t minutes;
	uint8_t seconds;
	uint8_t weekday;
	uint8_t month;
	uint8_t date;
	uint8_t year;
} TIME_TypeDef;

void rtc_set_alarm(void);
uint32_t RTC_read_device_time(void);
void rtc_set_time(TIME_TypeDef timeObj);
uint32_t RTC_set_first_wakeup(time_t device_time, uint8_t interval, TIME_TypeDef *timeObj);
uint8_t GSM_POST_time(time_t device_time, uint8_t gsm_interval);

#ifdef __cplusplus
}
#endif

#endif /* CORE_INC_RTC_SYNC_H_ */