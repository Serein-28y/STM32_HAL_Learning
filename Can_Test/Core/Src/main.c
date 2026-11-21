/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "can.h"
#include "i2c.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "OLED.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/*
 * CAN 滤波器"配置字"（uint8_t）位布局：
 *   bit7..3  滤波器编号 (0~13)
 *   bit2     接收队列   (0=FIFO0, 1=FIFO1)
 *   bit1     帧格式     (0=标准帧, 1=扩展帧)   对应 IDE
 *   bit0     帧类型     (0=数据帧, 1=遥控帧)   对应 RTR
 * 用法：CAN_FILTER(0) | CAN_FIFO_0 | CAN_STDID | CAN_DATA_TYPE
 */

// F103C8T6 只有 1 个 CAN，共 14 组滤波器（编号 0~13）
#define CAN_FILTER_NUM_MAX 14

// 滤波器编号
#define CAN_FILTER(x) ((x) << 3)//将滤波器编号放在左边第4位

// 接收队列
#define CAN_FIFO_0 (0 << 2)//将0放在左边第三位
#define CAN_FIFO_1 (1 << 2)//将1放在左边第三位

// 标准帧或扩展帧
#define CAN_STDID (0 << 1)//将0放在左边第二位
#define CAN_EXTID (1 << 1)//将1放在左边第二位

// 数据帧或遥控帧
#define CAN_DATA_TYPE (0 << 0)//将0放在左边第一位
#define CAN_REMOTE_TYPE (1 << 0)//将1放在左边第一位

// 从配置字中取回各字段（滤波器配置函数里使用）
#define CAN_PARSE_FILTER(m) (((m) >> 3) & 0x1F)//取出FILTER
#define CAN_PARSE_FIFO(m)   (((m) >> 2) & 0x01)
#define CAN_PARSE_IDE(m)    (((m) >> 1) & 0x01)
#define CAN_PARSE_RTR(m)    (((m) >> 0) & 0x01)

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void CAN_Init(CAN_HandleTypeDef *hcan)
{
  HAL_CAN_Start(hcan);
  __HAL_CAN_ENABLE_IT(hcan, CAN_IT_RX_FIFO0_MSG_PENDING);
  __HAL_CAN_ENABLE_IT(hcan, CAN_IT_RX_FIFO1_MSG_PENDING);
}
/**
 * @brief 配置CAN的滤波器
 *
 * @param hcan CAN编号
 * @param Object_Para 编号[3:] | FIFOx[2:2] | ID类型[1:1] | 帧类型[0:0]
 * @param ID ID
 * @param Mask_ID 屏蔽位(0x7ff, 0x1fffffff)掩码ID
 */
void CAN_Filter_Mask_Config(CAN_HandleTypeDef *hcan, uint8_t Object_Para, uint32_t ID, uint32_t Mask_ID)
{
  CAN_FilterTypeDef can_filter_init_structure;

  // 看第0位ID, 判断是数据帧还是遥控帧
  // 遥控帧暂不处理
  if (Object_Para & 0x01)
  {
    return;
  }

  // 看第1位ID, 判断是标准帧还是扩展帧
  // 扩展帧暂不处理
  if ((Object_Para & 0x02) >> 1)
  {
    return;
  }

  // 标准帧

  // ID配置, 标准帧的ID是11bit, 按规定放在高16bit中的[15:5]位
  // 掩码后ID的高16bit
  can_filter_init_structure.FilterIdHigh = (ID & 0x7FF) << 5;//保留11位然后左移5格放在5-15位
  // 掩码后ID的低16bit
  can_filter_init_structure.FilterIdLow = 0x0000;
  // 掩码后屏蔽位的高16bit
  can_filter_init_structure.FilterMaskIdHigh = (Mask_ID & 0x7FF) << 5;//掩码一样
  // 掩码后屏蔽位的低16bit
  can_filter_init_structure.FilterMaskIdLow = 0x0000;

  // 滤波器配置
  // 滤波器序号, 0-13
  can_filter_init_structure.FilterBank = (Object_Para >> 3) & 0x1F;
  // 滤波器模式, 设置ID掩码模式
  can_filter_init_structure.FilterMode = CAN_FILTERMODE_IDMASK;
  // 32位滤波
  can_filter_init_structure.FilterScale = CAN_FILTERSCALE_32BIT;
  // 使能滤波器
  can_filter_init_structure.FilterActivation = ENABLE;
  
  // 从机模式配置
  // 从机模式选择开始单元, 一般均分14个单元给CAN1和CAN2
  can_filter_init_structure.SlaveStartFilterBank = 14;

  // 滤波器绑定FIFOx, 只能绑定一个
  can_filter_init_structure.FilterFIFOAssignment = (Object_Para >> 2) & 0x01;

  HAL_CAN_ConfigFilter(hcan, &can_filter_init_structure);
}


/**
 * @brief 发送数据帧
 *
 * @param hcan CAN编号
 * @param ID ID
 * @param Data 被发送的数据指针
 * @param Length 长度
 * @return uint8_t 执行状态
 */
uint8_t CAN_Send_Data(CAN_HandleTypeDef *hcan, uint16_t ID, uint8_t *Data, uint16_t Length)
{
  CAN_TxHeaderTypeDef tx_header;
  uint32_t used_mailbox;

  // 检测关键传参
  assert_param(hcan != NULL);

  tx_header.StdId = ID;
  tx_header.ExtId = 0;
  tx_header.IDE = 0;
  tx_header.RTR = 0;
  tx_header.DLC = Length;

  return (HAL_CAN_AddTxMessage(hcan, &tx_header, Data, &used_mailbox));
}

void LED_Control(uint8_t data)
{
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, ((data&1)==0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, ((data&2)==0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, ((data&4)==0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  

}
//接收报文中断回调函数--接收can总线上数据，并且发现数据符合滤波器的匹配规则，发起接收中断进入函数内部
void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  CAN_RxHeaderTypeDef header;//接收之前发送的
  uint8_t data;

  HAL_CAN_GetRxMessage(hcan, CAN_FILTER_FIFO1, &header, &data);
  
  LED_Control(data);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{


  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_CAN_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  OLED_Init();
  uint8_t Send_Data = 0;
  CAN_Init(&hcan);
  CAN_Filter_Mask_Config(&hcan, CAN_FILTER(13) | CAN_FIFO_1 | CAN_STDID | CAN_DATA_TYPE, 0x114, 0x7ff);
  

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    Send_Data++;
    CAN_Send_Data(&hcan,0x114,&Send_Data, 1);
    HAL_Delay(250);
    if(Send_Data>=4)
    {
      Send_Data=0;
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
