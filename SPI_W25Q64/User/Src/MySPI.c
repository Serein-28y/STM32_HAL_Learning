#include "main.h"
#include "spi.h"
#include "MySPI.h"

/**
  * 函    数：SPI写SS引脚电平，SS仍由软件控制
  * 参    数：BitValue，0为低电平，1为高电平
  * 返 回 值：无
  */
void MySPI_W_SS(uint8_t BitValue)
{
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin,
                      (GPIO_PinState)BitValue);
}

/**
  * 函    数：SPI模块初始化
  * 参    数：无
  * 返 回 值：无
  * 注意事项：先在主程序中完成MX_GPIO_Init和MX_SPI1_Init。
  *           硬件初始化由CubeMX生成的代码完成，这里只设置SS默认电平。
  */
void MySPI_Init(void)
{
    MySPI_W_SS(1);
}

/**
  * 函    数：SPI起始
  * 参    数：无
  * 返 回 值：无
  */
void MySPI_Start(void)
{
    MySPI_W_SS(0);
}

/**
  * 函    数：SPI终止
  * 参    数：无
  * 返 回 值：无
  */
void MySPI_Stop(void)
{
    MySPI_W_SS(1);
}

/**
  * 函    数：硬件SPI交换一个字节
  * 参    数：ByteSend，要发送的字节
  * 返 回 值：接收到的字节
  * 注意事项：不改变SS；使用阻塞收发，保持江协原版的调用方式。
  *           为保留原接口，此函数不向上层返回HAL错误状态。
  */
uint8_t MySPI_SwapByte(uint8_t ByteSend)
{
    uint8_t ByteReceive = 0;

    HAL_SPI_TransmitReceive(&hspi1, &ByteSend, &ByteReceive,
                            1, HAL_MAX_DELAY);

    return ByteReceive;
}
