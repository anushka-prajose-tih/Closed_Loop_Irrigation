/*
 * rotary_encoder.c
 *
 *  Created on: Sep 18, 2023
 *      Author: Dell
 */

#include "rotary_encoder.h"
char enteredKey[2] = "..";
char rotBuff[] = "0123456789";

extern uint32_t flash_write_address;
extern uint32_t flash_read_address;
extern uint32_t flash_erase_address;
extern uint8_t time_interval;
extern uint8_t GSM_interval;
extern UART_HandleTypeDef huart2;
extern TIM_HandleTypeDef htim1;
extern uint8_t Rotary_button_pressed;

static const uint8_t validate_data_interval[] = {1, 2, 5, 10, 15, 30, 60};
static const uint8_t validate_gsm_interval[] = {1, 2, 3, 4, 6, 12, 24};

CALLBACKS_RegisterTypeDef callBacks;
uint8_t keyPoss = 0;
uint8_t setting_index = 0;

void ROT_ENC_init(CALLBACKS_RegisterTypeDef *CBobj)
{
	callBacks.callBackPoss = CBobj->callBackPoss;
	callBacks.callBackBTNpress = CBobj->callBackBTNpress;
	TIM1->CNT = 0;
	enteredKey[0] = time_interval / 10 + 48;
	enteredKey[1] = time_interval % 10 + 48;
	enteredKey[2] = '\0';
}

void ROT_ENC_handler(void)
{
	// rotary line handler part
	static uint8_t oldPoss = 0;
	uint8_t data[1];
	uint8_t valid_input = 0;
	uint32_t get_poss = ROT_ENC_getPoss();
	if (oldPoss != get_poss)
	{
		oldPoss = ROT_ENC_getPoss();
		(callBacks.callBackPoss)(oldPoss);
	}

	// button handler part
	static uint32_t startTime = 0;
	static uint8_t timeRecFlag = RESET;
	if (HAL_GPIO_ReadPin(Rotary_button_GPIO_Port, Rotary_button_Pin) == GPIO_PIN_RESET)
	{
		if (timeRecFlag == RESET)
		{
			timeRecFlag = SET;
			startTime = HAL_GetTick();
		}
		else
		{
			if (HAL_GetTick() - startTime > 50)
			{
				while (HAL_GPIO_ReadPin(Rotary_button_GPIO_Port, Rotary_button_Pin) == GPIO_PIN_RESET)
					;
				timeRecFlag = RESET;
				SSD1306_Clear();
				if (keyPoss == 1)
				{

					Rotary_button_pressed = 0;

					if (setting_index == 0)
					{
						for (int i = 0; i < sizeof(validate_data_interval); i++)
						{
							if (atoi(enteredKey) == validate_data_interval[i])
								valid_input = 1;
						}
						if (valid_input == 1)
						{
							printDisplay("Success");
							valid_input = 0;
							time_interval = atoi(enteredKey);
							write_address_map(flash_write_address, flash_read_address, flash_erase_address, time_interval, GSM_interval);

							read_flash(INTERVAL_MINUTES, data, 1);
							setting_index = 1;
							enteredKey[0] = GSM_interval / 10 + 48;
							enteredKey[1] = GSM_interval % 10 + 48;
						}
						else
						{
							printDisplay("Invalid");
							HAL_Delay(1000);
						}
					}
					else
					{
						for (int i = 0; i < sizeof(validate_gsm_interval); i++)
						{
							if (atoi(enteredKey) == validate_gsm_interval[i])
								valid_input = 1;
						}
						if (valid_input == 1)
						{
							printDisplay("Success");
							GSM_interval = atoi(enteredKey);
							write_address_map(flash_write_address, flash_read_address, flash_erase_address, time_interval, GSM_interval);

							read_flash(GSM_INTERVAL, data, 1);
							HAL_NVIC_SystemReset();
						}
						else
						{
							printDisplay("Invalid");
							HAL_Delay(1000);
						}
					}
				}
				(callBacks.callBackBTNpress)();
			}
		}
	}
	else
	{
		timeRecFlag = RESET;
	}
}

uint32_t ROT_ENC_getPoss(void)
{
	return ((TIM1->CNT) / 4);
}

void ROT_ENC_resetPoss(void)
{
	TIM1->CNT = 0;
}

/**
 * _TIMx_Init: this function use for configure timer in encoder mode
 * with "selBuffLen".
 * here, "selBuffLen" is length of assigned rotary buffer for selection.
 */

void ROT_ENC_setSelection(uint8_t len)
{
	TIM1->CNT = 0;
}
void rotaryPossCallBack(uint8_t poss)
{
	enteredKey[keyPoss] = rotBuff[poss];
}

void btnPressCallBack(void)
{
	if (ROT_ENC_getPoss() < (sizeof(rotBuff) - 1))
	{
		keyPoss++;
		if (keyPoss > 1)
		{
			keyPoss = 0;
		}
	}
	else if (keyPoss > 0)
	{
		keyPoss--;
	}

	ROT_ENC_resetPoss();
}
