/*
 * rotary_encoder.h
 *
 *  Created on: Sep 18, 2023
 *      Author: Dell
 */

#ifndef INC_ROTARY_ENCODER_H_
#define INC_ROTARY_ENCODER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef struct
{
    void (*callBackPoss)(uint8_t);
    void (*callBackBTNpress)(void);
} CALLBACKS_RegisterTypeDef;

void ROT_ENC_init(CALLBACKS_RegisterTypeDef *CBobj);
void ROT_ENC_handler(void);
uint32_t ROT_ENC_getPoss(void);
void ROT_ENC_resetPoss(void);
void ROT_ENC_setSelection(uint8_t length);

void rotaryPossCallBack(uint8_t poss);
void btnPressCallBack(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_ROTARY_ENCODER_H_ */
