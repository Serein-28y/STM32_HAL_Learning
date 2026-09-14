#include"main.h"
#include"tim.h"
#include"servo.h"


#define Servo_CH1 TIM_CHANNEL_2
#define Servo_CH2 TIM_CHANNEL_3

typedef struct
{
	uint32_t Channel;
	uint16_t Current_Angle;
	uint16_t Start_Angle;
	uint16_t Target_Angle;
	uint32_t Start_Tick;
	uint16_t Time_Ms;
	uint8_t Moving;
}Servo_State;
//舵机的通道，当前角，起始角，目标角，开始时间，移动时间，是否在移动
Servo_State servo_state[2]=
{
	{Servo_CH1,0,0,0,0,0,0},
	{Servo_CH2,0,0,0,0,0,0}
};



//双舵机初始化
void Servo_Init(void)
{
	HAL_TIM_PWM_Start(&htim1,TIM_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim1,TIM_CHANNEL_3);
	__HAL_TIM_SET_COMPARE(&htim1,TIM_CHANNEL_2,500);
	__HAL_TIM_SET_COMPARE(&htim1,TIM_CHANNEL_3,500);
}

//按照目标角度执行舵机角度的偏转
void Servo_WritePWM(uint8_t index,uint16_t Angle)
{
	if(Angle>=180)
	{
		Angle=180;
	}
	__HAL_TIM_SET_COMPARE(&htim1,servo_state[index].Channel,500+(Angle)*2000/180);
}

//执行某一个舵机在单位时间内的角度微小偏转
 void Servo_UpdataOne(uint8_t index)
{
    int32_t elapsed_ms;//从开始到现在已经经过的时间
    int32_t start_angle;//舵机开始角度
    int32_t target_angle;//舵机目标角度
    int32_t delta_angle;//舵机角度变化量

    if(servo_state[index].Moving==0)
    {
        return; // 如果舵机没有在移动，直接返回
    }
    elapsed_ms = HAL_GetTick() - servo_state[index].Start_Tick;//计算舵机从上一次微小地转动到下一次微小的转动所经过的时间
    if(elapsed_ms < 0)
    {
        elapsed_ms = 0; // 防止溢出
    }
    if(elapsed_ms >= servo_state[index].Time_Ms)
    {
        // 如果已经超过了设定的时间，直接设置为目标角度
        servo_state[index].Current_Angle = servo_state[index].Target_Angle;
        Servo_WritePWM(index, servo_state[index].Current_Angle);
        servo_state[index].Moving = 0; // 停止移动
        return;
    }
    start_angle = servo_state[index].Start_Angle;
    target_angle = servo_state[index].Target_Angle;
    delta_angle = target_angle - start_angle;//总共需要转动的角度
    target_angle = start_angle + (delta_angle * elapsed_ms) / (int32_t)servo_state[index].Time_Ms;//初始角度+总共需要转动的角度*时间走的比例

    servo_state[index].Current_Angle = target_angle;
    Servo_WritePWM(index, servo_state[index].Current_Angle);
}

//uint8_t Servo_Ready=1;
void Servo_Updata(void)
{
	//检查舵机是否准备好，若为准备好则直接返回，避免访问尚未准备好的PWM
//	if(Servo_Ready==0)
//	{
//		return;
//	}
	//在主循环中不断更新两个舵机的运动状态
	Servo_UpdataOne(0);
	Servo_UpdataOne(1);
}

//设置哪个舵机运动以及舵机的目标角度和运动时间
void Servo_MoveTo(uint8_t index,uint16_t Angle,uint16_t Time_ms)
{
	
	if((index!=0)&&(index!=1))
	{
		return;
	}
	if(Angle>180)
	{
		Angle=180;
	}
	servo_state[index].Start_Angle=servo_state[index].Current_Angle;
	servo_state[index].Target_Angle=Angle;
    servo_state[index].Start_Tick=HAL_GetTick();
	servo_state[index].Time_Ms=Time_ms;
	servo_state[index].Moving=1;

     if (Time_ms == 0)
    {
        servo_state[index].Current_Angle = Angle;
        servo_state[index].Moving = 0;
        Servo_WritePWM(index, Angle);
    }
}
