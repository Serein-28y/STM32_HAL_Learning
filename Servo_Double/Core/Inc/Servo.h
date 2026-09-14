#ifndef SERVO_H
#define SERVO_H

#include "main.h"
#include "tim.h"

#define Servo_CH1 TIM_CHANNEL_2
#define Servo_CH2 TIM_CHANNEL_3

void Servo_Init(void);
void Servo_WritePWM(uint8_t index, uint16_t Angle);
void Servo_Updata(void);
void Servo_UpdataOne(uint8_t index);
void Servo_MoveTo(uint8_t index, uint16_t Angle, uint16_t Time_ms);

#endif