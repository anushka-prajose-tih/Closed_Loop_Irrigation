/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    RTC/RTC_LowPower_STANDBY/Src/main.c
 * @author  MCD Application Team
 * @brief   This sample code shows how to use STM32G0xx RTC HAL API to enter
 *          and exit the Standby mode using RTC.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2020 STMicroelectronics.
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

extern RTC_HandleTypeDef hrtc;
extern uint8_t time_interval;
void rtc_set_time(TIME_TypeDef timeObj)
{
	RTC_TimeTypeDef sTime = {0};
	RTC_DateTypeDef sDate = {0};

	/** Initialize RTC and set the Time and Date
	 */
	sTime.Hours = timeObj.hours;	 // 0x11;
	sTime.Minutes = timeObj.minutes; // 0x30;
	sTime.Seconds = timeObj.seconds; // 0x55;
	sTime.SubSeconds = 0x0;
	sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
	sTime.StoreOperation = RTC_STOREOPERATION_RESET;
	if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
	{
		printDisplay_level(35, "st");
		Error_Handler();
	}
	sDate.Month = timeObj.month;		// RTC_MONTH_DECEMBER;
	sDate.Date = timeObj.date;			// 0x06;
	sDate.Year = timeObj.year;			// 0x23;

	if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
	{
		printDisplay_level(35, "rsd");
		Error_Handler();
	}

	time_t ts = RTC_read_device_time();

	struct tm time_tm;
	localtime_r(&ts, &time_tm);
	if (time_tm.tm_wday == 0)
	{
		sDate.WeekDay = 7;
	}
	else
	{
		sDate.WeekDay = time_tm.tm_wday;
	}
	sDate.Month = timeObj.month; // RTC_MONTH_DECEMBER;
	sDate.Date = timeObj.date;	 // 0x06;
	sDate.Year = timeObj.year;	 // 0x23;

	if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
	{
		printDisplay_level(35, "rd2");
		Error_Handler();
	}
}

uint32_t RTC_read_device_time()
{
	RTC_DateTypeDef sdatestructureget;
	RTC_TimeTypeDef stimestructureget;

	/* Get the RTC current Time */
	HAL_RTC_GetTime(&hrtc, &stimestructureget, RTC_FORMAT_BIN);

	/* Get the RTC current Date */
	HAL_RTC_GetDate(&hrtc, &sdatestructureget, RTC_FORMAT_BIN);
	struct tm time_tm;
	time_tm.tm_sec = stimestructureget.Seconds;
	time_tm.tm_min = stimestructureget.Minutes;
	time_tm.tm_hour = stimestructureget.Hours;
	if (sdatestructureget.WeekDay == 0)
	{
		sdatestructureget.WeekDay = 7;
	}
	time_tm.tm_wday = sdatestructureget.WeekDay;
	time_tm.tm_mday = sdatestructureget.Date;
	time_tm.tm_mon = sdatestructureget.Month - 1;	// 0-11 - Note: The month is 1-12.
	time_tm.tm_year = sdatestructureget.Year + 100; // Years since 1900
	uint32_t readTime = mktime(&time_tm);			// GMT
	char print_time[50];
	sprintf(print_time, "Unix:  % ld\r\n", readTime);
	serial_print(print_time);
	return readTime;
}

void rtc_set_alarm()
{

	RTC_AlarmTypeDef sAlarm = {0};
	TIME_TypeDef timeObj;
	uint32_t device_time = RTC_read_device_time();
	RTC_set_first_wakeup(device_time, time_interval, &timeObj);
	char print_char[50];
	sprintf(print_char, "Alarm time = %d:%d\r\n", timeObj.hours, timeObj.minutes);
	serial_print(print_char);
	/** Enable the Alarm A
	 */
	sAlarm.AlarmTime.Hours = timeObj.hours;		// 0x0;
	sAlarm.AlarmTime.Minutes = timeObj.minutes; //  0x0;
	sAlarm.AlarmTime.Seconds = 0;
	sAlarm.AlarmTime.SubSeconds = 0;
	sAlarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
	sAlarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
	sAlarm.AlarmMask = RTC_ALARMMASK_DATEWEEKDAY;
	sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
	sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_WEEKDAY;
	if (timeObj.weekday == 0)
	{
		sAlarm.AlarmDateWeekDay = 7;
	}
	else
	{
		sAlarm.AlarmDateWeekDay = timeObj.weekday;
	}
	sAlarm.Alarm = RTC_ALARM_A;

	if (HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BIN) != HAL_OK)
	{
		printDisplay_level(35, "rsa");
		Error_Handler();
	}

	char str[20];
	sprintf(str,"%d %d:%d", timeObj.date, timeObj.hours, timeObj.minutes);
	printDisplay(str);

	serial_print("Alarm set\r\n");
	/* Enter the Standby mode */
//	HAL_PWR_EnterSTANDBYMode();

}
uint32_t RTC_set_first_wakeup(time_t device_time, uint8_t interval, TIME_TypeDef *timeObj)
{
	uint32_t next_wakeup = 0;
	// uint8_t interval_minutes = interval / 60;
	uint8_t datapoints = 60 / interval;
	char print_char[50];
	sprintf(print_char, " interval= %d	No of datapoints: %d\r\n", interval, datapoints);
	serial_print(print_char);
	uint8_t minutes_array[datapoints + 1];
	for (int i = 0; i < datapoints; i++)
	{
		minutes_array[i] = interval * (i + 1);
	}

	struct tm time_tm;
	// time_tm = *(localtime(&now));
	localtime_r(&device_time, &time_tm);
	uint8_t minutes = time_tm.tm_min;

	sprintf(print_char, "Local time = %d:%d weekday: %d\r\n", time_tm.tm_hour, minutes, time_tm.tm_wday);
	serial_print(print_char);
	for (int i = 0; i < datapoints; i++)
	{
		if ((i == 0 && (minutes < minutes_array[i])) || (i == datapoints - 1 && (minutes == minutes_array[i])))
		{
			serial_print("loop1\r\n");
			next_wakeup = minutes_array[i]; // - minutes);* 60 + 1;
			break;
		}
		else if (minutes >= minutes_array[i] && minutes < minutes_array[i + 1])
		{
			serial_print("loop2\r\n");
			next_wakeup = minutes_array[i + 1]; //- minutes);// * 60 + 1;
			break;
		}
	}
	if (next_wakeup == 60) // exact hour eg: 3:00, 4:00
	{
		if (time_tm.tm_hour == 23) // If day changes at midnight, set it to 00:00
		{
			timeObj->hours = 0;
			timeObj->date = time_tm.tm_mday + 1;
			if (time_tm.tm_wday == 7) // if sunday(7), change to Monday(1)
			{
				timeObj->weekday = 1;
			}
			else
			{
				timeObj->weekday = time_tm.tm_wday + 1;
			}
		}
		else
		{
			timeObj->hours = time_tm.tm_hour + 1;
			timeObj->weekday = time_tm.tm_wday;
			timeObj->date = time_tm.tm_mday;
		}
		timeObj->minutes = 0;
	}
	else
	{
		timeObj->hours = time_tm.tm_hour;
		timeObj->minutes = next_wakeup;
		timeObj->weekday = time_tm.tm_wday;
		timeObj->date = time_tm.tm_mday;
	}
	return next_wakeup;
}
uint8_t GSM_POST_time(time_t device_time, uint8_t gsm_interval)
{
	uint8_t next_wakeup = 0;
	uint8_t datapoints = 24 / gsm_interval;
	char print_char[50];
	sprintf(print_char, " interval= %d	No of datapoints: %d\r\n", gsm_interval, datapoints);
	serial_print(print_char);
	uint8_t hours_array[datapoints + 1];
	for (int i = 0; i < datapoints; i++)
	{
		hours_array[i] = gsm_interval * (i + 1);
	}

	struct tm time_tm;
	localtime_r(&device_time, &time_tm);
	uint8_t hour = time_tm.tm_hour;

	sprintf(print_char, "Local time hour = %d\r\n", time_tm.tm_hour);
	serial_print(print_char);
	for (int i = 0; i < datapoints; i++)
	{
		if ((i == 0 && (hour < hours_array[i])) || ((hour == hours_array[i]) && (time_tm.tm_min == 0)))
		{
			serial_print("loop1\r\n");
			next_wakeup = hours_array[i];
			break;
		}
		else if (hour >= hours_array[i] && hour < hours_array[i + 1])
		{
			serial_print("loop2\r\n");
			next_wakeup = hours_array[i + 1];
			break;
		}
	}

	if (next_wakeup == 24) // If day changes at midnight, set it to 00:00
		next_wakeup = 0;

	return next_wakeup;
}
