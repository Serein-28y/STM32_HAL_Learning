
/***************************************************************************************
  * 本程序由江协科技创建并免费开源共享
  * 你可以任意查看、使用和修改，并应用到自己的项目之中
  * 程序版权归江协科技所有，任何人或组织不得将其据为己有
  * 
  * 程序名称：				0.96寸OLED显示屏驱动程序（4针脚I2C接口）
  * 程序创建时间：			2023.10.24
  * 当前程序版本：			V1.2
  * 当前版本发布时间：		2024.4.24
  * 
  * 江协科技官方网站：		jiangxiekeji.com
  * 江协科技官方淘宝店：	jiangxiekeji.taobao.com
  * 程序介绍及更新动态：	jiangxiekeji.com/tutorial/oled.html
  * 
  * 如果你发现程序中的漏洞或者笔误，可通过邮件向我们反馈：feedback@jiangxiekeji.com
  * 发送邮件之前，你可以先到更新动态页面查看最新程序，如果此问题已经修改，则无需再发邮件
  ***************************************************************************************
  */


/***************************************************************************************
 * HAL 硬件 I2C 移植与使用说明（2026-11-17 补充）
 * 本说明针对当前目录内的改版代码；原始作者及版权声明见上方。
 * 本次仅补充注释，未修复下列已知问题，也未完成目标工程编译或上板验证。
 *
 * 一、适用范围
 * 1. 默认用于 SSD1306、128 x 64、4 针 I2C OLED，7 位地址为 0x3C。
 *    OLED_I2C_ADDR 使用 HAL 要求的左移格式 (0x3C << 1)；0x3D 模块需改地址。
 * 2. 当前依赖 i2c.h 和 hi2c1，底层使用 HAL 硬件 I2C，不是软件模拟 I2C。
 * 3. 换为 128 x 32、SH1106 或其他控制器时，须核对初始化命令、显存尺寸、
 *    扫描参数和列偏移，不能只根据屏幕尺寸判断兼容性。
 *    SH1106 常见模块需要列偏移；DMA 路径独立生成列地址，必须同步修改，
 *    只在 OLED_SetCursor() 中启用 X += 2 不会改变 DMA 刷新的列地址。
 *
 * 二、移植前检查（除 IO 引脚外）
 * 1. 将 OLED.c、OLED_Data.c 加入编译，并配置 OLED.h、OLED_Data.h 的头文件路径。
 *    核对 HAL I2C 模块已启用；如改用 I2C2，需统一替换所有 hi2c1 引用及回调判断。
 * 2. 按模块规格配置 I2C 时钟、开漏及上拉，确认总线上拉电压适合 MCU 和模块。
 *    建议先用 100 kHz 调通，再根据模块规格、上拉及走线条件使用 400 kHz。
 * 3. 先完成 HAL_Init()、系统时钟及 CubeMX 外设初始化，再调用 OLED_Init()。
 *    使用 DMA 时按目标工程生成的顺序初始化，通常为 MX_DMA_Init() 在
 *    MX_I2C1_Init() 之前，OLED_Init() 在二者之后；HAL 时基必须正常运行。
 * 4. 当前源码为 UTF-8，但 OLED_CHN_CHAR_WIDTH 为 2。若编译器执行字符集也为
 *    UTF-8，常用汉字占 3 字节，应将该宏改为 3，使 Index 还能容纳结尾的空字符。
 *    只有明确采用 GB2312 等相应双字节编码时才保留 2；字库和调用处须保持一致。
 *    OLED_ShowChinese() 只接受字库中定义的汉字或全角字符，不用于混合 ASCII。
 * 5. 图形功能使用数学函数；按工具链需要配置数学库链接。OLED_Printf() 若要
 *    使用 %f，还需核对所选 C 库的浮点格式化支持，例如 newlib-nano 的相关选项。
 *
 * 三、阻塞刷新使用方式
 * 1. OLED_Init() 内含约 100 ms 上电等待及一次阻塞清屏刷新。
 * 2. OLED_Clear()、OLED_ShowString()、绘图等函数只修改 RAM 显存；完成绘制后
 *    调用 OLED_Update() 全屏刷新，或在修复边界问题后使用 OLED_UpdateArea()。
 * 3. OLED_Update() 会等待 I2C 传输完成。不要在中断回调中调用这些阻塞通信函数，
 *    也不要在中断中调用含 HAL_Delay() 的 OLED_Init()。
 *
 * 四、DMA 刷新使用方式
 * 1. 配置 I2C TX DMA：内存到外设、字节宽度、内存地址递增、普通模式；按目标
 *    芯片和 HAL 版本配置 DMA/I2C 中断，并在中断处理函数中转发给 HAL。
 *    仅调用阻塞刷新接口时不需要启动 DMA；当前文件仍保留 DMA 相关编译依赖。
 * 2. 主循环绘制一帧后调用 OLED_Update_DMA()，返回 1 仅表示接受请求并保存快照，
 *    不代表整帧已发送。返回 0 表示当前未接受请求，调用者可稍后重试。
 * 3. 主循环每次循环都应调用 OLED_Task()，由它启动并推进各页的命令和数据发送。
 *    不要只调用 OLED_Update_DMA() 而遗漏 OLED_Task()，也不要在中断里推进状态机。
 * 4. 当前整帧超时为 OLED_DMA_FRAME_TIMEOUT_MS（250 ms），包含主循环调度间隔。
 *    避免长时间 HAL_Delay() 或其他阻塞；需要时根据总线速度及调度周期调整超时。
 * 5. DMA 使用独立快照，发送期间可在主循环继续绘图；新的刷新请求需等待空闲。
 *    OLED_IsBusy() 为 1 时，阻塞刷新和底层阻塞写入会直接返回，不会排队等待。
 * 6. 本文件已定义 HAL_I2C_MasterTxCpltCallback() 和 HAL_I2C_ErrorCallback()。
 *    工程中已有同名回调时必须合并；采用动态回调注册时也须正确接入这些处理逻辑。
 * 7. 当前按单主循环调用设计，不提供 RTOS 互斥和共享总线仲裁。多个任务、其他
 *    I2C 设备或中断访问同一 I2C 外设时，应由上层统一调度，避免交叉操作。
 * 8. 显存约占 1024 字节，DMA 快照另占 1032 字节，还需为发送和格式化局部缓冲区
 *    预留栈空间。DMA 可减少 CPU 等待，但不会减少整帧在 I2C 总线上的传输时间。
 *
 * 五、当前已知问题与使用限制（以下为现状说明，尚未修复）
 * 1. OLED_UpdateArea() 没有完整裁剪屏幕边界。起点有效也不能保证读取安全，
 *    例如 (120, 56, 16, 8) 会读出显存末尾。修复前只传入完全位于屏幕内、
 *    宽高大于 0 的矩形；后续应先裁剪矩形再计算页范围和发送长度。
 * 2. OLED_Printf() 使用 vsprintf()，输出超过 String[256] 容量会破坏栈内存。
 *    应改为 vsnprintf(String, sizeof(String), format, arg)，并检查格式化结果。
 * 3. 阻塞发送结果仅保存最近一次 OLED_I2C_Status；初始化或刷新中途失败后仍继续，
 *    后续成功可能覆盖之前的错误。建议让底层和上层返回状态，遇到失败及时退出。
 * 4. DMA 失败或超时后进入 OLED_DMA_FAULT，OLED_IsBusy() 将持续为 1，停止刷新。
 *    再次调用 OLED_Init() 不会清除该状态；当前恢复方式是复位 MCU 并重新初始化。
 *    若增加恢复接口，必须先停止并确认 DMA/I2C 不再传输，必要时恢复总线或重新
 *    初始化外设，再清理状态。不能只强制改为 IDLE 后复用仍被 DMA 读取的缓冲区。
 * 5. OLED_ShowChar() 未校验字模索引，OLED_ShowString() 不处理换行等控制字符。
 *    仅传入字库支持的可见 ASCII；当前字符串遍历索引为 uint8_t，避免长字符串。
 *    OLED_ShowSignedNum() 对 INT32_MIN 直接取负存在有符号溢出，使用时应避开，
 *    后续应改为不会发生有符号溢出的无符号幅值计算。
 * 6. 调试时可观察 OLED_I2C_Status 和 OLED_DMA_ErrorCode；仅看 Busy 无法区分
 *    正在传输与故障锁定。建议先调通阻塞刷新，再验证 DMA、错误处理及调度周期。
 ***************************************************************************************/

#include "OLED.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include "i2c.h"

#define OLED_I2C_ADDR        (0x3CU << 1)
#define OLED_I2C_TIMEOUT_MS  100U

/* 最近一次发送结果，可在调试器中观察 */
volatile HAL_StatusTypeDef OLED_I2C_Status = HAL_OK;

/**
  * 数据存储格式：
  * 纵向8点，高位在下，先从左到右，再从上到下
  * 每一个Bit对应一个像素点
  * 
  *      B0 B0                  B0 B0
  *      B1 B1                  B1 B1
  *      B2 B2                  B2 B2
  *      B3 B3  ------------->  B3 B3 --
  *      B4 B4                  B4 B4  |
  *      B5 B5                  B5 B5  |
  *      B6 B6                  B6 B6  |
  *      B7 B7                  B7 B7  |
  *                                    |
  *  -----------------------------------
  *  |   
  *  |   B0 B0                  B0 B0
  *  |   B1 B1                  B1 B1
  *  |   B2 B2                  B2 B2
  *  --> B3 B3  ------------->  B3 B3
  *      B4 B4                  B4 B4
  *      B5 B5                  B5 B5
  *      B6 B6                  B6 B6
  *      B7 B7                  B7 B7
  * 
  * 坐标轴定义：
  * 左上角为(0, 0)点
  * 横向向右为X轴，取值范围：0~127
  * 纵向向下为Y轴，取值范围：0~63
  * 
  *       0             X轴           127
  *      .------------------------------->
  *    0 |
  *      |
  *      |
  *      |
  *  Y轴 |
  *      |
  *      |
  *      |
  *   63 |
  *      v
  * 
  */


/*全局变量*********************/

/**
  * OLED显存数组
  * 所有的显示函数，都只是对此显存数组进行读写
  * 随后调用OLED_Update函数或OLED_UpdateArea函数
  * 才会将显存数组的数据发送到OLED硬件，进行显示
  */
uint8_t OLED_DisplayBuf[8][128];

/* 整帧超时包含主循环调度间隔，故障后保留缓冲区直到复位 */
#define OLED_DMA_FRAME_TIMEOUT_MS  250U

typedef enum
{
    OLED_DMA_IDLE = 0,
    OLED_DMA_SEND_CMD,
    OLED_DMA_WAIT_CMD,
    OLED_DMA_SEND_DATA,
    OLED_DMA_WAIT_DATA,
    OLED_DMA_FAULT
} OLED_DMA_State_t;

static volatile OLED_DMA_State_t OLED_DMA_State = OLED_DMA_IDLE;
static volatile uint8_t OLED_DMA_Done = 0;
static volatile uint8_t OLED_DMA_Error = 0;

/* 调试器中查看故障原因；发送失败后停止刷新，不自动重试 */
volatile uint32_t OLED_DMA_ErrorCode = HAL_I2C_ERROR_NONE;

/* DMA读取静态快照，避免绘图覆盖正在传输的内容 */
static uint8_t OLED_DMA_Frame[8][129];
static uint8_t OLED_DMA_Command[4];
static uint8_t OLED_DMA_Page = 0;
static uint32_t OLED_DMA_StartTick = 0;

/*********************全局变量*/


/*通信协议*********************/

/**
  * 函    数：OLED写命令
  * 参    数：Command 要写入的命令值，范围：0x00~0xFF
  * 返 回 值：无
  */
void OLED_WriteCommand(uint8_t Command)
{
    if (OLED_IsBusy())
    {
        return;
    }

    uint8_t TxBuffer[2] = {0x00, Command};

    OLED_I2C_Status = HAL_I2C_Master_Transmit(
        &hi2c1,
        OLED_I2C_ADDR,
        TxBuffer,
        sizeof(TxBuffer),
        OLED_I2C_TIMEOUT_MS
    );
}

/**
  * 函    数：OLED写数据
  * 参    数：Data 要写入数据的起始地址
  * 参    数：Count 要写入数据的数量，范围：1~128
  * 返 回 值：无
  */
void OLED_WriteData(uint8_t *Data, uint8_t Count)
{
    if (OLED_IsBusy())
    {
        return;
    }

    uint8_t TxBuffer[129];

    if (Data == NULL || Count == 0U || Count > 128U)
    {
        return;
    }

    TxBuffer[0] = 0x40;       // 控制字节：后面是显示数据
    memcpy(&TxBuffer[1], Data, Count);

    OLED_I2C_Status = HAL_I2C_Master_Transmit(
        &hi2c1,
        OLED_I2C_ADDR,
        TxBuffer,
        (uint16_t)Count + 1U,
        OLED_I2C_TIMEOUT_MS
    );
}

/*********************通信协议*/


/*硬件配置*********************/

/**
  * 函    数：OLED初始化
  * 参    数：无
  * 返 回 值：无
  * 说    明：使用前，需要调用此初始化函数
  */
void OLED_Init(void)
{
	HAL_Delay(100);			//等待OLED上电稳定，引脚已由MX_I2C1_Init初始化
	
	/*写入一系列的命令，对OLED进行初始化配置*/
	OLED_WriteCommand(0xAE);	//设置显示开启/关闭，0xAE关闭，0xAF开启
	
	OLED_WriteCommand(0xD5);	//设置显示时钟分频比/振荡器频率
	OLED_WriteCommand(0x80);	//0x00~0xFF
	
	OLED_WriteCommand(0xA8);	//设置多路复用率
	OLED_WriteCommand(0x3F);	//0x0E~0x3F
	
	OLED_WriteCommand(0xD3);	//设置显示偏移
	OLED_WriteCommand(0x00);	//0x00~0x7F
	
	OLED_WriteCommand(0x40);	//设置显示开始行，0x40~0x7F
	
	OLED_WriteCommand(0xA1);	//设置左右方向，0xA1正常，0xA0左右反置
	
	OLED_WriteCommand(0xC8);	//设置上下方向，0xC8正常，0xC0上下反置

	OLED_WriteCommand(0xDA);	//设置COM引脚硬件配置
	OLED_WriteCommand(0x12);
	
	OLED_WriteCommand(0x81);	//设置对比度
	OLED_WriteCommand(0xCF);	//0x00~0xFF

	OLED_WriteCommand(0xD9);	//设置预充电周期
	OLED_WriteCommand(0xF1);

	OLED_WriteCommand(0xDB);	//设置VCOMH取消选择级别
	OLED_WriteCommand(0x30);

	OLED_WriteCommand(0xA4);	//设置整个显示打开/关闭

	OLED_WriteCommand(0xA6);	//设置正常/反色显示，0xA6正常，0xA7反色

	OLED_WriteCommand(0x8D);	//设置充电泵
	OLED_WriteCommand(0x14);

	OLED_WriteCommand(0xAF);	//开启显示
	
	OLED_Clear();				//清空显存数组
	OLED_Update();				//更新显示，清屏，防止初始化后未显示内容时花屏
}

/**
  * 函    数：OLED设置显示光标位置
  * 参    数：Page 指定光标所在的页，范围：0~7
  * 参    数：X 指定光标所在的X轴坐标，范围：0~127
  * 返 回 值：无
  * 说    明：OLED默认的Y轴，只能8个Bit为一组写入，即1页等于8个Y轴坐标
  */
void OLED_SetCursor(uint8_t Page, uint8_t X)
{
	/*如果使用此程序驱动1.3寸的OLED显示屏，则需要解除此注释*/
	/*因为1.3寸的OLED驱动芯片（SH1106）有132列*/
	/*屏幕的起始列接在了第2列，而不是第0列*/
	/*所以需要将X加2，才能正常显示*/
//	X += 2;
	
	/*通过指令设置页地址和列地址*/
	OLED_WriteCommand(0xB0 | Page);					//设置页位置
	OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));	//设置X位置高4位
	OLED_WriteCommand(0x00 | (X & 0x0F));			//设置X位置低4位
}

/*********************硬件配置*/


/*工具函数*********************/

/*工具函数仅供内部部分函数使用*/

/**
  * 函    数：次方函数
  * 参    数：X 底数
  * 参    数：Y 指数
  * 返 回 值：等于X的Y次方
  */
uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;	//结果默认为1
	while (Y --)			//累乘Y次
	{
		Result *= X;		//每次把X累乘到结果上
	}
	return Result;
}

/**
  * 函    数：判断指定点是否在指定多边形内部
  * 参    数：nvert 多边形的顶点数
  * 参    数：vertx verty 包含多边形顶点的x和y坐标的数组
  * 参    数：testx testy 测试点的X和y坐标
  * 返 回 值：指定点是否在指定多边形内部，1：在内部，0：不在内部
  */
uint8_t OLED_pnpoly(uint8_t nvert, int16_t *vertx, int16_t *verty, int16_t testx, int16_t testy)
{
	int16_t i, j, c = 0;
	
	/*此算法由W. Randolph Franklin提出*/
	/*参考链接：https://wrfranklin.org/Research/Short_Notes/pnpoly.html*/
	for (i = 0, j = nvert - 1; i < nvert; j = i++)
	{
		if (((verty[i] > testy) != (verty[j] > testy)) &&
			(testx < (vertx[j] - vertx[i]) * (testy - verty[i]) / (verty[j] - verty[i]) + vertx[i]))
		{
			c = !c;
		}
	}
	return c;
}

/**
  * 函    数：判断指定点是否在指定角度内部
  * 参    数：X Y 指定点的坐标
  * 参    数：StartAngle EndAngle 起始角度和终止角度，范围：-180~180
  *           水平向右为0度，水平向左为180度或-180度，下方为正数，上方为负数，顺时针旋转
  * 返 回 值：指定点是否在指定角度内部，1：在内部，0：不在内部
  */
uint8_t OLED_IsInAngle(int16_t X, int16_t Y, int16_t StartAngle, int16_t EndAngle)
{
	int16_t PointAngle;
	PointAngle = atan2(Y, X) / 3.14 * 180;	//计算指定点的弧度，并转换为角度表示
	if (StartAngle < EndAngle)	//起始角度小于终止角度的情况
	{
		/*如果指定角度在起始终止角度之间，则判定指定点在指定角度*/
		if (PointAngle >= StartAngle && PointAngle <= EndAngle)
		{
			return 1;
		}
	}
	else			//起始角度大于于终止角度的情况
	{
		/*如果指定角度大于起始角度或者小于终止角度，则判定指定点在指定角度*/
		if (PointAngle >= StartAngle || PointAngle <= EndAngle)
		{
			return 1;
		}
	}
	return 0;		//不满足以上条件，则判断判定指定点不在指定角度
}

/*********************工具函数*/


/*功能函数*********************/

/**
  * 函    数：将OLED显存数组更新到OLED屏幕
  * 参    数：无
  * 返 回 值：无
  * 说    明：所有的显示函数，都只是对OLED显存数组进行读写
  *           随后调用OLED_Update函数或OLED_UpdateArea函数
  *           才会将显存数组的数据发送到OLED硬件，进行显示
  *           故调用显示函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_Update(void)
{
	if (OLED_IsBusy())
	{
		return;
	}

	uint8_t j;
	/*遍历每一页*/
	for (j = 0; j < 8; j ++)
	{
		/*设置光标位置为每一页的第一列*/
		OLED_SetCursor(j, 0);
		/*连续写入128个数据，将显存数组的数据写入到OLED硬件*/
		OLED_WriteData(OLED_DisplayBuf[j], 128);
	}
}

/**
  * 函    数：将OLED显存数组部分更新到OLED屏幕
  * 参    数：X 指定区域左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定区域左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：Width 指定区域的宽度，范围：0~128
  * 参    数：Height 指定区域的高度，范围：0~64
  * 返 回 值：无
  * 说    明：此函数会至少更新参数指定的区域
  *           如果更新区域Y轴只包含部分页，则同一页的剩余部分会跟随一起更新
  * 说    明：所有的显示函数，都只是对OLED显存数组进行读写
  *           随后调用OLED_Update函数或OLED_UpdateArea函数
  *           才会将显存数组的数据发送到OLED硬件，进行显示
  *           故调用显示函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_UpdateArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height)
{
	if (OLED_IsBusy())
	{
		return;
	}

	int16_t j;
	int16_t Page, Page1;
	
	/*负数坐标在计算页地址时需要加一个偏移*/
	/*(Y + Height - 1) / 8 + 1的目的是(Y + Height) / 8并向上取整*/
	Page = Y / 8;
	Page1 = (Y + Height - 1) / 8 + 1;
	if (Y < 0)
	{
		Page -= 1;
		Page1 -= 1;
	}
	
	/*遍历指定区域涉及的相关页*/
	for (j = Page; j < Page1; j ++)
	{
		if (X >= 0 && X <= 127 && j >= 0 && j <= 7)		//超出屏幕的内容不显示
		{
			/*设置光标位置为相关页的指定列*/
			OLED_SetCursor(j, X);
			/*连续写入Width个数据，将显存数组的数据写入到OLED硬件*/
			OLED_WriteData(&OLED_DisplayBuf[j][X], Width);
		}
	}
}

/**
  * 函    数：将OLED显存数组全部清零
  * 参    数：无
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_Clear(void)
{
	uint8_t i, j;
	for (j = 0; j < 8; j ++)				//遍历8页
	{
		for (i = 0; i < 128; i ++)			//遍历128列
		{
			OLED_DisplayBuf[j][i] = 0x00;	//将显存数组数据全部清零
		}
	}
}

/**
  * 函    数：将OLED显存数组部分清零
  * 参    数：X 指定区域左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定区域左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：Width 指定区域的宽度，范围：0~128
  * 参    数：Height 指定区域的高度，范围：0~64
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_ClearArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height)
{
	int16_t i, j;
	
	for (j = Y; j < Y + Height; j ++)		//遍历指定页
	{
		for (i = X; i < X + Width; i ++)	//遍历指定列
		{
			if (i >= 0 && i <= 127 && j >=0 && j <= 63)				//超出屏幕的内容不显示
			{
				OLED_DisplayBuf[j / 8][i] &= ~(0x01 << (j % 8));	//将显存数组指定数据清零
			}
		}
	}
}

/**
  * 函    数：将OLED显存数组全部取反
  * 参    数：无
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_Reverse(void)
{
	uint8_t i, j;
	for (j = 0; j < 8; j ++)				//遍历8页
	{
		for (i = 0; i < 128; i ++)			//遍历128列
		{
			OLED_DisplayBuf[j][i] ^= 0xFF;	//将显存数组数据全部取反
		}
	}
}
	
/**
  * 函    数：将OLED显存数组部分取反
  * 参    数：X 指定区域左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定区域左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：Width 指定区域的宽度，范围：0~128
  * 参    数：Height 指定区域的高度，范围：0~64
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_ReverseArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height)
{
	int16_t i, j;
	
	for (j = Y; j < Y + Height; j ++)		//遍历指定页
	{
		for (i = X; i < X + Width; i ++)	//遍历指定列
		{
			if (i >= 0 && i <= 127 && j >=0 && j <= 63)			//超出屏幕的内容不显示
			{
				OLED_DisplayBuf[j / 8][i] ^= 0x01 << (j % 8);	//将显存数组指定数据取反
			}
		}
	}
}

/**
  * 函    数：OLED显示一个字符
  * 参    数：X 指定字符左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定字符左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：Char 指定要显示的字符，范围：ASCII码可见字符
  * 参    数：FontSize 指定字体大小
  *           范围：OLED_8X16		宽8像素，高16像素
  *                 OLED_6X8		宽6像素，高8像素
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_ShowChar(int16_t X, int16_t Y, char Char, uint8_t FontSize)
{
	if (FontSize == OLED_8X16)		//字体为宽8像素，高16像素
	{
		/*将ASCII字模库OLED_F8x16的指定数据以8*16的图像格式显示*/
		OLED_ShowImage(X, Y, 8, 16, OLED_F8x16[Char - ' ']);
	}
	else if(FontSize == OLED_6X8)	//字体为宽6像素，高8像素
	{
		/*将ASCII字模库OLED_F6x8的指定数据以6*8的图像格式显示*/
		OLED_ShowImage(X, Y, 6, 8, OLED_F6x8[Char - ' ']);
	}
}

/**
  * 函    数：OLED显示字符串
  * 参    数：X 指定字符串左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定字符串左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：String 指定要显示的字符串，范围：ASCII码可见字符组成的字符串
  * 参    数：FontSize 指定字体大小
  *           范围：OLED_8X16		宽8像素，高16像素
  *                 OLED_6X8		宽6像素，高8像素
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_ShowString(int16_t X, int16_t Y, char *String, uint8_t FontSize)
{
	uint8_t i;
	for (i = 0; String[i] != '\0'; i++)		//遍历字符串的每个字符
	{
		/*调用OLED_ShowChar函数，依次显示每个字符*/
		OLED_ShowChar(X + i * FontSize, Y, String[i], FontSize);
	}
}

/**
  * 函    数：OLED显示数字（十进制，正整数）
  * 参    数：X 指定数字左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定数字左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：Number 指定要显示的数字，范围：0~4294967295
  * 参    数：Length 指定数字的长度，范围：0~10
  * 参    数：FontSize 指定字体大小
  *           范围：OLED_8X16		宽8像素，高16像素
  *                 OLED_6X8		宽6像素，高8像素
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_ShowNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
	uint8_t i;
	for (i = 0; i < Length; i++)		//遍历数字的每一位
	{
		/*调用OLED_ShowChar函数，依次显示每个数字*/
		/*Number / OLED_Pow(10, Length - i - 1) % 10 可以十进制提取数字的每一位*/
		/*+ '0' 可将数字转换为字符格式*/
		OLED_ShowChar(X + i * FontSize, Y, Number / OLED_Pow(10, Length - i - 1) % 10 + '0', FontSize);
	}
}

/**
  * 函    数：OLED显示有符号数字（十进制，整数）
  * 参    数：X 指定数字左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定数字左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：Number 指定要显示的数字，范围：-2147483648~2147483647
  * 参    数：Length 指定数字的长度，范围：0~10
  * 参    数：FontSize 指定字体大小
  *           范围：OLED_8X16		宽8像素，高16像素
  *                 OLED_6X8		宽6像素，高8像素
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_ShowSignedNum(int16_t X, int16_t Y, int32_t Number, uint8_t Length, uint8_t FontSize)
{
	uint8_t i;
	uint32_t Number1;
	
	if (Number >= 0)						//数字大于等于0
	{
		OLED_ShowChar(X, Y, '+', FontSize);	//显示+号
		Number1 = Number;					//Number1直接等于Number
	}
	else									//数字小于0
	{
		OLED_ShowChar(X, Y, '-', FontSize);	//显示-号
		Number1 = -Number;					//Number1等于Number取负
	}
	
	for (i = 0; i < Length; i++)			//遍历数字的每一位
	{
		/*调用OLED_ShowChar函数，依次显示每个数字*/
		/*Number1 / OLED_Pow(10, Length - i - 1) % 10 可以十进制提取数字的每一位*/
		/*+ '0' 可将数字转换为字符格式*/
		OLED_ShowChar(X + (i + 1) * FontSize, Y, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0', FontSize);
	}
}

/**
  * 函    数：OLED显示十六进制数字（十六进制，正整数）
  * 参    数：X 指定数字左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定数字左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：Number 指定要显示的数字，范围：0x00000000~0xFFFFFFFF
  * 参    数：Length 指定数字的长度，范围：0~8
  * 参    数：FontSize 指定字体大小
  *           范围：OLED_8X16		宽8像素，高16像素
  *                 OLED_6X8		宽6像素，高8像素
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_ShowHexNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
	uint8_t i, SingleNumber;
	for (i = 0; i < Length; i++)		//遍历数字的每一位
	{
		/*以十六进制提取数字的每一位*/
		SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
		
		if (SingleNumber < 10)			//单个数字小于10
		{
			/*调用OLED_ShowChar函数，显示此数字*/
			/*+ '0' 可将数字转换为字符格式*/
			OLED_ShowChar(X + i * FontSize, Y, SingleNumber + '0', FontSize);
		}
		else							//单个数字大于10
		{
			/*调用OLED_ShowChar函数，显示此数字*/
			/*+ 'A' 可将数字转换为从A开始的十六进制字符*/
			OLED_ShowChar(X + i * FontSize, Y, SingleNumber - 10 + 'A', FontSize);
		}
	}
}

/**
  * 函    数：OLED显示二进制数字（二进制，正整数）
  * 参    数：X 指定数字左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定数字左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：Number 指定要显示的数字，范围：0x00000000~0xFFFFFFFF
  * 参    数：Length 指定数字的长度，范围：0~16
  * 参    数：FontSize 指定字体大小
  *           范围：OLED_8X16		宽8像素，高16像素
  *                 OLED_6X8		宽6像素，高8像素
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_ShowBinNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
	uint8_t i;
	for (i = 0; i < Length; i++)		//遍历数字的每一位
	{
		/*调用OLED_ShowChar函数，依次显示每个数字*/
		/*Number / OLED_Pow(2, Length - i - 1) % 2 可以二进制提取数字的每一位*/
		/*+ '0' 可将数字转换为字符格式*/
		OLED_ShowChar(X + i * FontSize, Y, Number / OLED_Pow(2, Length - i - 1) % 2 + '0', FontSize);
	}
}

/**
  * 函    数：OLED显示浮点数字（十进制，小数）
  * 参    数：X 指定数字左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定数字左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：Number 指定要显示的数字，范围：-4294967295.0~4294967295.0
  * 参    数：IntLength 指定数字的整数位长度，范围：0~10
  * 参    数：FraLength 指定数字的小数位长度，范围：0~9，小数进行四舍五入显示
  * 参    数：FontSize 指定字体大小
  *           范围：OLED_8X16		宽8像素，高16像素
  *                 OLED_6X8		宽6像素，高8像素
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_ShowFloatNum(int16_t X, int16_t Y, double Number, uint8_t IntLength, uint8_t FraLength, uint8_t FontSize)
{
	uint32_t PowNum, IntNum, FraNum;
	
	if (Number >= 0)						//数字大于等于0
	{
		OLED_ShowChar(X, Y, '+', FontSize);	//显示+号
	}
	else									//数字小于0
	{
		OLED_ShowChar(X, Y, '-', FontSize);	//显示-号
		Number = -Number;					//Number取负
	}
	
	/*提取整数部分和小数部分*/
	IntNum = Number;						//直接赋值给整型变量，提取整数
	Number -= IntNum;						//将Number的整数减掉，防止之后将小数乘到整数时因数过大造成错误
	PowNum = OLED_Pow(10, FraLength);		//根据指定小数的位数，确定乘数
	FraNum = round(Number * PowNum);		//将小数乘到整数，同时四舍五入，避免显示误差
	IntNum += FraNum / PowNum;				//若四舍五入造成了进位，则需要再加给整数
	
	/*显示整数部分*/
	OLED_ShowNum(X + FontSize, Y, IntNum, IntLength, FontSize);
	
	/*显示小数点*/
	OLED_ShowChar(X + (IntLength + 1) * FontSize, Y, '.', FontSize);
	
	/*显示小数部分*/
	OLED_ShowNum(X + (IntLength + 2) * FontSize, Y, FraNum, FraLength, FontSize);
}

/**
  * 函    数：OLED显示汉字串
  * 参    数：X 指定汉字串左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定汉字串左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：Chinese 指定要显示的汉字串，范围：必须全部为汉字或者全角字符，不要加入任何半角字符
  *           显示的汉字需要在OLED_Data.c里的OLED_CF16x16数组定义
  *           未找到指定汉字时，会显示默认图形（一个方框，内部一个问号）
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_ShowChinese(int16_t X, int16_t Y, char *Chinese)
{
	uint8_t pChinese = 0;
	uint8_t pIndex;
	uint8_t i;
	char SingleChinese[OLED_CHN_CHAR_WIDTH + 1] = {0};
	
	for (i = 0; Chinese[i] != '\0'; i ++)		//遍历汉字串
	{
		SingleChinese[pChinese] = Chinese[i];	//提取汉字串数据到单个汉字数组
		pChinese ++;							//计次自增
		
		/*当提取次数到达OLED_CHN_CHAR_WIDTH时，即代表提取到了一个完整的汉字*/
		if (pChinese >= OLED_CHN_CHAR_WIDTH)
		{
			pChinese = 0;		//计次归零
			
			/*遍历整个汉字字模库，寻找匹配的汉字*/
			/*如果找到最后一个汉字（定义为空字符串），则表示汉字未在字模库定义，停止寻找*/
			for (pIndex = 0; strcmp(OLED_CF16x16[pIndex].Index, "") != 0; pIndex ++)
			{
				/*找到匹配的汉字*/
				if (strcmp(OLED_CF16x16[pIndex].Index, SingleChinese) == 0)
				{
					break;		//跳出循环，此时pIndex的值为指定汉字的索引
				}
			}
			
			/*将汉字字模库OLED_CF16x16的指定数据以16*16的图像格式显示*/
			OLED_ShowImage(X + ((i + 1) / OLED_CHN_CHAR_WIDTH - 1) * 16, Y, 16, 16, OLED_CF16x16[pIndex].Data);
		}
	}
}

/**
  * 函    数：OLED显示图像
  * 参    数：X 指定图像左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定图像左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：Width 指定图像的宽度，范围：0~128
  * 参    数：Height 指定图像的高度，范围：0~64
  * 参    数：Image 指定要显示的图像
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_ShowImage(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, const uint8_t *Image)
{
	uint8_t i = 0, j = 0;
	int16_t Page, Shift;
	
	/*将图像所在区域清空*/
	OLED_ClearArea(X, Y, Width, Height);
	
	/*遍历指定图像涉及的相关页*/
	/*(Height - 1) / 8 + 1的目的是Height / 8并向上取整*/
	for (j = 0; j < (Height - 1) / 8 + 1; j ++)
	{
		/*遍历指定图像涉及的相关列*/
		for (i = 0; i < Width; i ++)
		{
			if (X + i >= 0 && X + i <= 127)		//超出屏幕的内容不显示
			{
				/*负数坐标在计算页地址和移位时需要加一个偏移*/
				Page = Y / 8;
				Shift = Y % 8;
				if (Y < 0)
				{
					Page -= 1;
					Shift += 8;
				}
				
				if (Page + j >= 0 && Page + j <= 7)		//超出屏幕的内容不显示
				{
					/*显示图像在当前页的内容*/
					OLED_DisplayBuf[Page + j][X + i] |= Image[j * Width + i] << (Shift);
				}
				
				if (Page + j + 1 >= 0 && Page + j + 1 <= 7)		//超出屏幕的内容不显示
				{					
					/*显示图像在下一页的内容*/
					OLED_DisplayBuf[Page + j + 1][X + i] |= Image[j * Width + i] >> (8 - Shift);
				}
			}
		}
	}
}

/**
  * 函    数：OLED使用printf函数打印格式化字符串
  * 参    数：X 指定格式化字符串左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定格式化字符串左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：FontSize 指定字体大小
  *           范围：OLED_8X16		宽8像素，高16像素
  *                 OLED_6X8		宽6像素，高8像素
  * 参    数：format 指定要显示的格式化字符串，范围：ASCII码可见字符组成的字符串
  * 参    数：... 格式化字符串参数列表
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_Printf(int16_t X, int16_t Y, uint8_t FontSize, char *format, ...)
{
	char String[256];						//定义字符数组
	va_list arg;							//定义可变参数列表数据类型的变量arg
	va_start(arg, format);					//从format开始，接收参数列表到arg变量
	vsprintf(String, format, arg);			//使用vsprintf打印格式化字符串和参数列表到字符数组中
	va_end(arg);							//结束变量arg
	OLED_ShowString(X, Y, String, FontSize);//OLED显示字符数组（字符串）
}

/**
  * 函    数：OLED在指定位置画一个点
  * 参    数：X 指定点的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定点的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_DrawPoint(int16_t X, int16_t Y)
{
	if (X >= 0 && X <= 127 && Y >=0 && Y <= 63)		//超出屏幕的内容不显示
	{
		/*将显存数组指定位置的一个Bit数据置1*/
		OLED_DisplayBuf[Y / 8][X] |= 0x01 << (Y % 8);
	}
}

/**
  * 函    数：OLED获取指定位置点的值
  * 参    数：X 指定点的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定点的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 返 回 值：指定位置点是否处于点亮状态，1：点亮，0：熄灭
  */
uint8_t OLED_GetPoint(int16_t X, int16_t Y)
{
	if (X >= 0 && X <= 127 && Y >=0 && Y <= 63)		//超出屏幕的内容不读取
	{
		/*判断指定位置的数据*/
		if (OLED_DisplayBuf[Y / 8][X] & 0x01 << (Y % 8))
		{
			return 1;	//为1，返回1
		}
	}
	
	return 0;		//否则，返回0
}

/**
  * 函    数：OLED画线
  * 参    数：X0 指定一个端点的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y0 指定一个端点的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：X1 指定另一个端点的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y1 指定另一个端点的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_DrawLine(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1)
{
	int16_t x, y, dx, dy, d, incrE, incrNE, temp;
	int16_t x0 = X0, y0 = Y0, x1 = X1, y1 = Y1;
	uint8_t yflag = 0, xyflag = 0;
	
	if (y0 == y1)		//横线单独处理
	{
		/*0号点X坐标大于1号点X坐标，则交换两点X坐标*/
		if (x0 > x1) {temp = x0; x0 = x1; x1 = temp;}
		
		/*遍历X坐标*/
		for (x = x0; x <= x1; x ++)
		{
			OLED_DrawPoint(x, y0);	//依次画点
		}
	}
	else if (x0 == x1)	//竖线单独处理
	{
		/*0号点Y坐标大于1号点Y坐标，则交换两点Y坐标*/
		if (y0 > y1) {temp = y0; y0 = y1; y1 = temp;}
		
		/*遍历Y坐标*/
		for (y = y0; y <= y1; y ++)
		{
			OLED_DrawPoint(x0, y);	//依次画点
		}
	}
	else				//斜线
	{
		/*使用Bresenham算法画直线，可以避免耗时的浮点运算，效率更高*/
		/*参考文档：https://www.cs.montana.edu/courses/spring2009/425/dslectures/Bresenham.pdf*/
		/*参考教程：https://www.bilibili.com/video/BV1364y1d7Lo*/
		
		if (x0 > x1)	//0号点X坐标大于1号点X坐标
		{
			/*交换两点坐标*/
			/*交换后不影响画线，但是画线方向由第一、二、三、四象限变为第一、四象限*/
			temp = x0; x0 = x1; x1 = temp;
			temp = y0; y0 = y1; y1 = temp;
		}
		
		if (y0 > y1)	//0号点Y坐标大于1号点Y坐标
		{
			/*将Y坐标取负*/
			/*取负后影响画线，但是画线方向由第一、四象限变为第一象限*/
			y0 = -y0;
			y1 = -y1;
			
			/*置标志位yflag，记住当前变换，在后续实际画线时，再将坐标换回来*/
			yflag = 1;
		}
		
		if (y1 - y0 > x1 - x0)	//画线斜率大于1
		{
			/*将X坐标与Y坐标互换*/
			/*互换后影响画线，但是画线方向由第一象限0~90度范围变为第一象限0~45度范围*/
			temp = x0; x0 = y0; y0 = temp;
			temp = x1; x1 = y1; y1 = temp;
			
			/*置标志位xyflag，记住当前变换，在后续实际画线时，再将坐标换回来*/
			xyflag = 1;
		}
		
		/*以下为Bresenham算法画直线*/
		/*算法要求，画线方向必须为第一象限0~45度范围*/
		dx = x1 - x0;
		dy = y1 - y0;
		incrE = 2 * dy;
		incrNE = 2 * (dy - dx);
		d = 2 * dy - dx;
		x = x0;
		y = y0;
		
		/*画起始点，同时判断标志位，将坐标换回来*/
		if (yflag && xyflag){OLED_DrawPoint(y, -x);}
		else if (yflag)		{OLED_DrawPoint(x, -y);}
		else if (xyflag)	{OLED_DrawPoint(y, x);}
		else				{OLED_DrawPoint(x, y);}
		
		while (x < x1)		//遍历X轴的每个点
		{
			x ++;
			if (d < 0)		//下一个点在当前点东方
			{
				d += incrE;
			}
			else			//下一个点在当前点东北方
			{
				y ++;
				d += incrNE;
			}
			
			/*画每一个点，同时判断标志位，将坐标换回来*/
			if (yflag && xyflag){OLED_DrawPoint(y, -x);}
			else if (yflag)		{OLED_DrawPoint(x, -y);}
			else if (xyflag)	{OLED_DrawPoint(y, x);}
			else				{OLED_DrawPoint(x, y);}
		}	
	}
}

/**
  * 函    数：OLED矩形
  * 参    数：X 指定矩形左上角的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定矩形左上角的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：Width 指定矩形的宽度，范围：0~128
  * 参    数：Height 指定矩形的高度，范围：0~64
  * 参    数：IsFilled 指定矩形是否填充
  *           范围：OLED_UNFILLED		不填充
  *                 OLED_FILLED			填充
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_DrawRectangle(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, uint8_t IsFilled)
{
	int16_t i, j;
	if (!IsFilled)		//指定矩形不填充
	{
		/*遍历上下X坐标，画矩形上下两条线*/
		for (i = X; i < X + Width; i ++)
		{
			OLED_DrawPoint(i, Y);
			OLED_DrawPoint(i, Y + Height - 1);
		}
		/*遍历左右Y坐标，画矩形左右两条线*/
		for (i = Y; i < Y + Height; i ++)
		{
			OLED_DrawPoint(X, i);
			OLED_DrawPoint(X + Width - 1, i);
		}
	}
	else				//指定矩形填充
	{
		/*遍历X坐标*/
		for (i = X; i < X + Width; i ++)
		{
			/*遍历Y坐标*/
			for (j = Y; j < Y + Height; j ++)
			{
				/*在指定区域画点，填充满矩形*/
				OLED_DrawPoint(i, j);
			}
		}
	}
}

/**
  * 函    数：OLED三角形
  * 参    数：X0 指定第一个端点的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y0 指定第一个端点的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：X1 指定第二个端点的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y1 指定第二个端点的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：X2 指定第三个端点的横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y2 指定第三个端点的纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：IsFilled 指定三角形是否填充
  *           范围：OLED_UNFILLED		不填充
  *                 OLED_FILLED			填充
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_DrawTriangle(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint8_t IsFilled)
{
	int16_t minx = X0, miny = Y0, maxx = X0, maxy = Y0;
	int16_t i, j;
	int16_t vx[] = {X0, X1, X2};
	int16_t vy[] = {Y0, Y1, Y2};
	
	if (!IsFilled)			//指定三角形不填充
	{
		/*调用画线函数，将三个点用直线连接*/
		OLED_DrawLine(X0, Y0, X1, Y1);
		OLED_DrawLine(X0, Y0, X2, Y2);
		OLED_DrawLine(X1, Y1, X2, Y2);
	}
	else					//指定三角形填充
	{
		/*找到三个点最小的X、Y坐标*/
		if (X1 < minx) {minx = X1;}
		if (X2 < minx) {minx = X2;}
		if (Y1 < miny) {miny = Y1;}
		if (Y2 < miny) {miny = Y2;}
		
		/*找到三个点最大的X、Y坐标*/
		if (X1 > maxx) {maxx = X1;}
		if (X2 > maxx) {maxx = X2;}
		if (Y1 > maxy) {maxy = Y1;}
		if (Y2 > maxy) {maxy = Y2;}
		
		/*最小最大坐标之间的矩形为可能需要填充的区域*/
		/*遍历此区域中所有的点*/
		/*遍历X坐标*/
		for (i = minx; i <= maxx; i ++)
		{
			/*遍历Y坐标*/
			for (j = miny; j <= maxy; j ++)
			{
				/*调用OLED_pnpoly，判断指定点是否在指定三角形之中*/
				/*如果在，则画点，如果不在，则不做处理*/
				if (OLED_pnpoly(3, vx, vy, i, j)) {OLED_DrawPoint(i, j);}
			}
		}
	}
}

/**
  * 函    数：OLED画圆
  * 参    数：X 指定圆的圆心横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定圆的圆心纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：Radius 指定圆的半径，范围：0~255
  * 参    数：IsFilled 指定圆是否填充
  *           范围：OLED_UNFILLED		不填充
  *                 OLED_FILLED			填充
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_DrawCircle(int16_t X, int16_t Y, uint8_t Radius, uint8_t IsFilled)
{
	int16_t x, y, d, j;
	
	/*使用Bresenham算法画圆，可以避免耗时的浮点运算，效率更高*/
	/*参考文档：https://www.cs.montana.edu/courses/spring2009/425/dslectures/Bresenham.pdf*/
	/*参考教程：https://www.bilibili.com/video/BV1VM4y1u7wJ*/
	
	d = 1 - Radius;
	x = 0;
	y = Radius;
	
	/*画每个八分之一圆弧的起始点*/
	OLED_DrawPoint(X + x, Y + y);
	OLED_DrawPoint(X - x, Y - y);
	OLED_DrawPoint(X + y, Y + x);
	OLED_DrawPoint(X - y, Y - x);
	
	if (IsFilled)		//指定圆填充
	{
		/*遍历起始点Y坐标*/
		for (j = -y; j < y; j ++)
		{
			/*在指定区域画点，填充部分圆*/
			OLED_DrawPoint(X, Y + j);
		}
	}
	
	while (x < y)		//遍历X轴的每个点
	{
		x ++;
		if (d < 0)		//下一个点在当前点东方
		{
			d += 2 * x + 1;
		}
		else			//下一个点在当前点东南方
		{
			y --;
			d += 2 * (x - y) + 1;
		}
		
		/*画每个八分之一圆弧的点*/
		OLED_DrawPoint(X + x, Y + y);
		OLED_DrawPoint(X + y, Y + x);
		OLED_DrawPoint(X - x, Y - y);
		OLED_DrawPoint(X - y, Y - x);
		OLED_DrawPoint(X + x, Y - y);
		OLED_DrawPoint(X + y, Y - x);
		OLED_DrawPoint(X - x, Y + y);
		OLED_DrawPoint(X - y, Y + x);
		
		if (IsFilled)	//指定圆填充
		{
			/*遍历中间部分*/
			for (j = -y; j < y; j ++)
			{
				/*在指定区域画点，填充部分圆*/
				OLED_DrawPoint(X + x, Y + j);
				OLED_DrawPoint(X - x, Y + j);
			}
			
			/*遍历两侧部分*/
			for (j = -x; j < x; j ++)
			{
				/*在指定区域画点，填充部分圆*/
				OLED_DrawPoint(X - y, Y + j);
				OLED_DrawPoint(X + y, Y + j);
			}
		}
	}
}

/**
  * 函    数：OLED画椭圆
  * 参    数：X 指定椭圆的圆心横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定椭圆的圆心纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：A 指定椭圆的横向半轴长度，范围：0~255
  * 参    数：B 指定椭圆的纵向半轴长度，范围：0~255
  * 参    数：IsFilled 指定椭圆是否填充
  *           范围：OLED_UNFILLED		不填充
  *                 OLED_FILLED			填充
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_DrawEllipse(int16_t X, int16_t Y, uint8_t A, uint8_t B, uint8_t IsFilled)
{
	int16_t x, y, j;
	int16_t a = A, b = B;
	float d1, d2;
	
	/*使用Bresenham算法画椭圆，可以避免部分耗时的浮点运算，效率更高*/
	/*参考链接：https://blog.csdn.net/myf_666/article/details/128167392*/
	
	x = 0;
	y = b;
	d1 = b * b + a * a * (-b + 0.5);
	
	if (IsFilled)	//指定椭圆填充
	{
		/*遍历起始点Y坐标*/
		for (j = -y; j < y; j ++)
		{
			/*在指定区域画点，填充部分椭圆*/
			OLED_DrawPoint(X, Y + j);
			OLED_DrawPoint(X, Y + j);
		}
	}
	
	/*画椭圆弧的起始点*/
	OLED_DrawPoint(X + x, Y + y);
	OLED_DrawPoint(X - x, Y - y);
	OLED_DrawPoint(X - x, Y + y);
	OLED_DrawPoint(X + x, Y - y);
	
	/*画椭圆中间部分*/
	while (b * b * (x + 1) < a * a * (y - 0.5))
	{
		if (d1 <= 0)		//下一个点在当前点东方
		{
			d1 += b * b * (2 * x + 3);
		}
		else				//下一个点在当前点东南方
		{
			d1 += b * b * (2 * x + 3) + a * a * (-2 * y + 2);
			y --;
		}
		x ++;
		
		if (IsFilled)	//指定椭圆填充
		{
			/*遍历中间部分*/
			for (j = -y; j < y; j ++)
			{
				/*在指定区域画点，填充部分椭圆*/
				OLED_DrawPoint(X + x, Y + j);
				OLED_DrawPoint(X - x, Y + j);
			}
		}
		
		/*画椭圆中间部分圆弧*/
		OLED_DrawPoint(X + x, Y + y);
		OLED_DrawPoint(X - x, Y - y);
		OLED_DrawPoint(X - x, Y + y);
		OLED_DrawPoint(X + x, Y - y);
	}
	
	/*画椭圆两侧部分*/
	d2 = b * b * (x + 0.5) * (x + 0.5) + a * a * (y - 1) * (y - 1) - a * a * b * b;
	
	while (y > 0)
	{
		if (d2 <= 0)		//下一个点在当前点东方
		{
			d2 += b * b * (2 * x + 2) + a * a * (-2 * y + 3);
			x ++;
			
		}
		else				//下一个点在当前点东南方
		{
			d2 += a * a * (-2 * y + 3);
		}
		y --;
		
		if (IsFilled)	//指定椭圆填充
		{
			/*遍历两侧部分*/
			for (j = -y; j < y; j ++)
			{
				/*在指定区域画点，填充部分椭圆*/
				OLED_DrawPoint(X + x, Y + j);
				OLED_DrawPoint(X - x, Y + j);
			}
		}
		
		/*画椭圆两侧部分圆弧*/
		OLED_DrawPoint(X + x, Y + y);
		OLED_DrawPoint(X - x, Y - y);
		OLED_DrawPoint(X - x, Y + y);
		OLED_DrawPoint(X + x, Y - y);
	}
}

/**
  * 函    数：OLED画圆弧
  * 参    数：X 指定圆弧的圆心横坐标，范围：-32768~32767，屏幕区域：0~127
  * 参    数：Y 指定圆弧的圆心纵坐标，范围：-32768~32767，屏幕区域：0~63
  * 参    数：Radius 指定圆弧的半径，范围：0~255
  * 参    数：StartAngle 指定圆弧的起始角度，范围：-180~180
  *           水平向右为0度，水平向左为180度或-180度，下方为正数，上方为负数，顺时针旋转
  * 参    数：EndAngle 指定圆弧的终止角度，范围：-180~180
  *           水平向右为0度，水平向左为180度或-180度，下方为正数，上方为负数，顺时针旋转
  * 参    数：IsFilled 指定圆弧是否填充，填充后为扇形
  *           范围：OLED_UNFILLED		不填充
  *                 OLED_FILLED			填充
  * 返 回 值：无
  * 说    明：调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  */
void OLED_DrawArc(int16_t X, int16_t Y, uint8_t Radius, int16_t StartAngle, int16_t EndAngle, uint8_t IsFilled)
{
	int16_t x, y, d, j;
	
	/*此函数借用Bresenham算法画圆的方法*/
	
	d = 1 - Radius;
	x = 0;
	y = Radius;
	
	/*在画圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处理*/
	if (OLED_IsInAngle(x, y, StartAngle, EndAngle))	{OLED_DrawPoint(X + x, Y + y);}
	if (OLED_IsInAngle(-x, -y, StartAngle, EndAngle)) {OLED_DrawPoint(X - x, Y - y);}
	if (OLED_IsInAngle(y, x, StartAngle, EndAngle)) {OLED_DrawPoint(X + y, Y + x);}
	if (OLED_IsInAngle(-y, -x, StartAngle, EndAngle)) {OLED_DrawPoint(X - y, Y - x);}
	
	if (IsFilled)	//指定圆弧填充
	{
		/*遍历起始点Y坐标*/
		for (j = -y; j < y; j ++)
		{
			/*在填充圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处理*/
			if (OLED_IsInAngle(0, j, StartAngle, EndAngle)) {OLED_DrawPoint(X, Y + j);}
		}
	}
	
	while (x < y)		//遍历X轴的每个点
	{
		x ++;
		if (d < 0)		//下一个点在当前点东方
		{
			d += 2 * x + 1;
		}
		else			//下一个点在当前点东南方
		{
			y --;
			d += 2 * (x - y) + 1;
		}
		
		/*在画圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处理*/
		if (OLED_IsInAngle(x, y, StartAngle, EndAngle)) {OLED_DrawPoint(X + x, Y + y);}
		if (OLED_IsInAngle(y, x, StartAngle, EndAngle)) {OLED_DrawPoint(X + y, Y + x);}
		if (OLED_IsInAngle(-x, -y, StartAngle, EndAngle)) {OLED_DrawPoint(X - x, Y - y);}
		if (OLED_IsInAngle(-y, -x, StartAngle, EndAngle)) {OLED_DrawPoint(X - y, Y - x);}
		if (OLED_IsInAngle(x, -y, StartAngle, EndAngle)) {OLED_DrawPoint(X + x, Y - y);}
		if (OLED_IsInAngle(y, -x, StartAngle, EndAngle)) {OLED_DrawPoint(X + y, Y - x);}
		if (OLED_IsInAngle(-x, y, StartAngle, EndAngle)) {OLED_DrawPoint(X - x, Y + y);}
		if (OLED_IsInAngle(-y, x, StartAngle, EndAngle)) {OLED_DrawPoint(X - y, Y + x);}
		
		if (IsFilled)	//指定圆弧填充
		{
			/*遍历中间部分*/
			for (j = -y; j < y; j ++)
			{
				/*在填充圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处理*/
				if (OLED_IsInAngle(x, j, StartAngle, EndAngle)) {OLED_DrawPoint(X + x, Y + j);}
				if (OLED_IsInAngle(-x, j, StartAngle, EndAngle)) {OLED_DrawPoint(X - x, Y + j);}
			}
			
			/*遍历两侧部分*/
			for (j = -x; j < x; j ++)
			{
				/*在填充圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处理*/
				if (OLED_IsInAngle(-y, j, StartAngle, EndAngle)) {OLED_DrawPoint(X - y, Y + j);}
				if (OLED_IsInAngle(y, j, StartAngle, EndAngle)) {OLED_DrawPoint(X + y, Y + j);}
			}
		}
	}
}

/* DMA刷新仅由主循环调度；中断回调只发布完成/错误事件 */
uint8_t OLED_IsBusy(void)
{
    return OLED_DMA_State != OLED_DMA_IDLE;
}

uint8_t OLED_Update_DMA(void)
{
    if (OLED_IsBusy() ||
        HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY)
    {
        return 0;
    }

    for (uint8_t page = 0; page < 8; page++)
    {
        OLED_DMA_Frame[page][0] = 0x40;
        memcpy(&OLED_DMA_Frame[page][1], OLED_DisplayBuf[page], 128);
    }

    OLED_DMA_Page = 0;
    OLED_DMA_Done = 0;
    OLED_DMA_Error = 0;
    OLED_DMA_ErrorCode = HAL_I2C_ERROR_NONE;
    OLED_DMA_StartTick = HAL_GetTick();
    OLED_DMA_State = OLED_DMA_SEND_CMD;
    return 1;
}

void OLED_Task(void)
{
    HAL_StatusTypeDef status;
    uint8_t *buffer;
    uint16_t length;
    OLED_DMA_State_t waitState;

    if (OLED_DMA_State == OLED_DMA_IDLE ||
        OLED_DMA_State == OLED_DMA_FAULT)
    {
        return;
    }

    if (OLED_DMA_Error)
    {
        OLED_I2C_Status = HAL_ERROR;
        OLED_DMA_State = OLED_DMA_FAULT;
        return;
    }

    if (OLED_DMA_State == OLED_DMA_WAIT_CMD ||
        OLED_DMA_State == OLED_DMA_WAIT_DATA)
    {
        if (OLED_DMA_Done)
        {
            OLED_DMA_Done = 0;
            if (OLED_DMA_State == OLED_DMA_WAIT_CMD)
            {
                OLED_DMA_State = OLED_DMA_SEND_DATA;
            }
            else
            {
                OLED_DMA_Page++;
                if (OLED_DMA_Page >= 8)
                {
                    OLED_I2C_Status = HAL_OK;
                    OLED_DMA_State = OLED_DMA_IDLE;
                    return;
                }
                OLED_DMA_State = OLED_DMA_SEND_CMD;
            }
        }
    }

    if ((uint32_t)(HAL_GetTick() - OLED_DMA_StartTick) >=
        OLED_DMA_FRAME_TIMEOUT_MS)
    {
        OLED_DMA_ErrorCode |= HAL_I2C_ERROR_TIMEOUT;
        OLED_I2C_Status = HAL_TIMEOUT;
        OLED_DMA_State = OLED_DMA_FAULT;
        return;
    }

    if (OLED_DMA_State == OLED_DMA_WAIT_CMD ||
        OLED_DMA_State == OLED_DMA_WAIT_DATA)
    {
        return;
    }

    /* STOP尚未完成时直接返回，不循环等待总线 */
    if (HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY ||
        __HAL_I2C_GET_FLAG(&hi2c1, I2C_FLAG_BUSY) != RESET)
    {
        return;
    }

    if (OLED_DMA_State == OLED_DMA_SEND_CMD)
    {
        OLED_DMA_Command[0] = 0x00;
        OLED_DMA_Command[1] = 0xB0 | OLED_DMA_Page;
        OLED_DMA_Command[2] = 0x10;
        OLED_DMA_Command[3] = 0x00;
        buffer = OLED_DMA_Command;
        length = sizeof(OLED_DMA_Command);
        waitState = OLED_DMA_WAIT_CMD;
    }
    else
    {
        buffer = OLED_DMA_Frame[OLED_DMA_Page];
        length = sizeof(OLED_DMA_Frame[0]);
        waitState = OLED_DMA_WAIT_DATA;
    }

    OLED_DMA_Done = 0;
    /* 先进入等待状态，再启动DMA，避免快速回调被遗漏 */
    OLED_DMA_State = waitState;
    status = HAL_I2C_Master_Transmit_DMA(
        &hi2c1, OLED_I2C_ADDR, buffer, length
    );

    if (status != HAL_OK)
    {
        OLED_I2C_Status = status;
        OLED_DMA_ErrorCode = HAL_I2C_GetError(&hi2c1);
        OLED_DMA_State = OLED_DMA_FAULT;
    }
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == &hi2c1 &&
        (OLED_DMA_State == OLED_DMA_WAIT_CMD ||
         OLED_DMA_State == OLED_DMA_WAIT_DATA))
    {
        OLED_DMA_Done = 1;
    }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == &hi2c1 &&
        (OLED_DMA_State == OLED_DMA_WAIT_CMD ||
         OLED_DMA_State == OLED_DMA_WAIT_DATA))
    {
        OLED_DMA_ErrorCode = HAL_I2C_GetError(hi2c);
        OLED_DMA_Error = 1;
    }
}

/*********************功能函数*/


/*****************江协科技|版权所有****************/
/*****************jiangxiekeji.com*****************/
