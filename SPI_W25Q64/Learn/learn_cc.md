# 硬件 SPI 读写 W25Q64 —— HAL 库移植版 带读笔记

> 对应原课：
> - 江协 STM32 入门教程 p40 [11-5] 硬件 SPI 读写 W25Q64（标准库，BV1th411z7sn p40）
> - 28.硬件 SPI 操作 W25Q64【HAL 库复现江协全部 STM32 例子合集】（BV1S4421U7YV p1）
>
> 说明：本工程里 **W25Q64 走的是硬件 SPI1**；**OLED 走的是硬件 I2C1**（`hi2c1`）。两条总线互不相干，别混了。

---

## 0. 先看全局：代码分层

```
main.c                 ← 你要写的：调用 W25Q64_xxx，把结果显示到 OLED
  │
W25Q64.c / W25Q64.h    ← 业务层：按 W25Q64 指令拼"指令+地址+数据"
W25Q64_Ins.h           ← 指令码宏定义表（0x9F、0x06、0x02 ...）
  │
MySPI.c / MySPI.h      ← 通信层：Start / Stop / SwapByte 三个动作
  │
spi.c / gpio.c         ← CubeMX 生成：SPI1 外设 + PA4 片选脚初始化
  │
HAL 库                 ← HAL_SPI_TransmitReceive / HAL_GPIO_WritePin
```

**核心思想（两份课都强调）**：上层 W25Q64.c 只调用 `MySPI_Start / MySPI_Stop / MySPI_SwapByte`，
不关心底层是软件翻 GPIO 还是硬件 SPI。所以从软件 SPI → 硬件 SPI，**只改 MySPI.c**，W25Q64.c 一行不动。

---

## 1. 阅读优先级总表

| 文件 / 内容 | 级别 | 说明 |
| --- | --- | --- |
| `MySPI.c` 全部（4 个函数） | ★★★ 必须理解 | 很短，但它是整个移植的关键 |
| `W25Q64.c` 的 `ReadID / WriteEnable / WaitBusy / PageProgram / SectorErase / ReadData` | ★★★ 必须理解 | 理解"指令+地址+数据"的帧结构，以及"写前要擦、写前要使能、写后要等忙" |
| `spi.c` 中 `MX_SPI1_Init` 的参数 | ★★☆ 理解含义 | 知道每个参数为什么这么选（模式 0、主机、软件 NSS、MSB、8 位） |
| `gpio.c` 中 PA4（FLASH_CS）配置 | ★★☆ 理解含义 | 为什么是推挽输出、为什么默认高电平 |
| `W25Q64_Ins.h` | ★☆☆ 会查就行 | 指令表，用到哪个查哪个，不用背 |
| `HAL_SPI_MspInit`（引脚复用、时钟使能） | ★☆☆ 知道在干嘛 | CubeMX 自动生成，会看懂 PA5/6/7 是 SCK/MISO/MOSI 即可 |
| `OLED.c / OLED_Data.c` | ☆☆☆ 只会调用 | 只需会用 `OLED_ShowString / ShowHexNum / Clear / Update` |
| HAL 库内部 `HAL_SPI_TransmitReceive` 实现 | ☆☆☆ 只会调用 | 但要知道它内部等价于江协讲的"等 TXE→写 DR→等 RXNE→读 DR" |

---

## 2. 必须理解：MySPI.c（通信层）

### 2.1 片选 SS —— 仍然是软件控制

```c
void MySPI_W_SS(uint8_t BitValue)
{
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, (GPIO_PinState)BitValue);
}
void MySPI_Init(void)  { MySPI_W_SS(1); }   // 默认不选中从机
void MySPI_Start(void) { MySPI_W_SS(0); }   // 拉低 = 选中 W25Q64，开始一帧
void MySPI_Stop(void)  { MySPI_W_SS(1); }   // 拉高 = 结束一帧
```

要点：
- CubeMX 里 NSS 选的是 **Software**（`SPI_NSS_SOFT`），硬件不会帮你管片选，所以另开 PA4 推挽输出，命名为 `FLASH_CS`。
- `FLASH_CS_Pin / FLASH_CS_GPIO_Port` 是 CubeMX 根据你起的名字在 `main.h` 里生成的宏。改了名字，这里要跟着改（HAL 课第 4 章的坑）。
- **W25Q64 以 CS 的下降沿作为一条指令的开始、上升沿作为结束**。比如页编程、擦除，芯片是在 CS 拉高那一刻才真正开始执行的。所以 Start/Stop 不能乱省。
- 初始化时一定要先置高（`gpio.c` 里 CubeMX 已经 `GPIO_PIN_SET`，`MySPI_Init` 再保险一次），否则上电即被选中，第一条指令会错乱。

### 2.2 交换一个字节 —— 整个移植唯一实质性的改动

```c
uint8_t MySPI_SwapByte(uint8_t ByteSend)
{
    uint8_t ByteReceive = 0;
    HAL_SPI_TransmitReceive(&hspi1, &ByteSend, &ByteReceive, 1, HAL_MAX_DELAY);
    return ByteReceive;
}
```

必须理解的三件事：

1. **SPI 是"交换"**：主机每发出 1 字节，同时一定收回 1 字节。
   - 只想发：调用 `SwapByte(指令)`，返回值丢掉。
   - 只想收：调用 `SwapByte(0xFF)`（`W25Q64_DUMMY_BYTE`），发个无意义字节换回数据。
   - 硬件 SPI **不写 DR 就不产生时钟**，所以"只读不发"是不可能的（江协第 5 章的坑）。

2. **HAL 函数对应江协标准库的四步**：

   | 江协标准库写法 | HAL 里由 `HAL_SPI_TransmitReceive` 内部完成 |
   | --- | --- |
   | `while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) != SET);` | 等发送缓冲空 |
   | `SPI_I2S_SendData(SPI1, ByteSend);` | 写 DR，硬件自动产生 8 个 SCK |
   | `while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) != SET);` | 等接收完成（收完 = 发完） |
   | `return SPI_I2S_ReceiveData(SPI1);` | 读 DR |

   TXE / RXNE 不用手动清除：写 DR 自动清 TXE，读 DR 自动清 RXNE。

3. **为什么用阻塞方式 + 长度 1**：保持和江协一样的"一次换一个字节"接口，上层代码完全不用改。
   代价是每字节都有 HAL 函数调用开销，速度不是最优——学习阶段不用在意。以后想提速，可以把
   `ReadData` 的循环改成一次 `HAL_SPI_Receive(&hspi1, buf, n, ...)`，甚至用 DMA。

---

## 3. 理解含义：CubeMX 的 SPI1 配置（spi.c）

```c
hspi1.Init.Mode              = SPI_MODE_MASTER;        // STM32 当主机
hspi1.Init.Direction         = SPI_DIRECTION_2LINES;   // 全双工（MOSI+MISO）
hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;      // 8 位一帧
hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;       // CPOL=0：SCK 空闲低电平
hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;        // CPHA=0：第一个边沿采样
hspi1.Init.NSS               = SPI_NSS_SOFT;           // 片选软件控制
hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;// 分频
hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;       // 高位先行
```

- **CPOL=0 + CPHA=0 = 模式 0**。W25Q64 支持模式 0 和模式 3，软件 SPI 那节用的是模式 0，这里保持一致。模式配错 → 读回的 ID/数据错位或全乱。
- **分频**：SPI1 挂在 APB2 = 72 MHz。
  - 本工程 /4 → SCK = 18 MHz（HAL 课里说的"改成四就不报错"指的就是这个）。
  - 江协标准库用的是 /128 → 562.5 kHz，求稳。
  - W25Q64 普通读（0x03）最高约 50 MHz，18 MHz 没问题；杜邦线很长、读数据异常时，可以先把分频调大来排查。
  - 注意 SPI2 在 APB1 = 36 MHz，算频率时别按 72 算。
- **引脚（HAL_SPI_MspInit 自动生成）**：PA5=SCK、PA7=MOSI 为复用推挽；PA6=MISO 为输入；PA4=CS 为普通推挽输出（gpio.c）。
  这些是 SPI1 的固定复用引脚，不能随便换（要换只能用重映射 PA15/PB3/PB4/PB5，且需先关 JTAG）。

> 接线：PA4→CS，PA5→CLK，PA6→DO(MISO)，PA7→DI(MOSI)，VCC 3.3V，GND。

---

## 4. 必须理解：W25Q64.c（业务层）

所有函数都是同一个套路：**Start → 发指令 → [发 24 位地址] → [收/发数据] → Stop**。

### 4.1 ReadID —— 先用它验证通信

```c
MySPI_Start();
MySPI_SwapByte(0x9F);                 // JEDEC ID 指令
*MID  = MySPI_SwapByte(0xFF);         // 厂商 ID：0xEF（Winbond）
*DID  = MySPI_SwapByte(0xFF) << 8;    // 设备 ID 高字节：0x40
*DID |= MySPI_SwapByte(0xFF);         // 设备 ID 低字节：0x17（64Mbit）
MySPI_Stop();
```

正确结果：**MID = EF，DID = 4017**。读出 FF/FFFF → MISO 没接好或 CS 没拉低；读出 00/0000 → 芯片没供电或 MISO 短地。

### 4.2 写操作的三条铁律

| 铁律 | 代码体现 | 为什么 |
| --- | --- | --- |
| ① 写之前先**写使能** | 每个 `PageProgram / SectorErase` 开头调 `W25Q64_WriteEnable()`（0x06） | 芯片的写保护机制；而且每执行完一次写/擦，写使能会自动失效，所以每次都要重新使能 |
| ② 写之后要**等忙** | 结尾调 `W25Q64_WaitBusy()`，循环读状态寄存器 1（0x05）的 BUSY 位（bit0） | 芯片在 CS 拉高后才开始内部擦写，需要时间（页编程约 ms 级、扇区擦除几十 ms），忙的时候发别的指令会被忽略 |
| ③ 写之前要**先擦除** | main 里先 `SectorErase` 再 `PageProgram` | Flash 只能把 1 写成 0，擦除才能把整块变回 0xFF。不擦直接写，结果 = 旧数据 & 新数据 |

> 江协写法是"写后等忙"；也有人写成"写前等忙"（效率更高）。理解原理即可，本工程沿用写后等忙。

### 4.3 地址与范围

- 24 位地址，按高→中→低三个字节发送：`Address>>16`、`Address>>8`、`Address`（`SwapByte` 参数是 uint8_t，自动截取低 8 位）。
- 容量 8MB：地址 `0x000000 ~ 0x7FFFFF`。
- **扇区擦除最小单位 4KB**：`SectorErase(0x000000)` 擦的是 `0x000000~0x000FFF` 整个扇区（地址落在扇区内任意位置都擦整个扇区）。
- **页编程一次最多 256 字节，且不能跨页**：页边界是 `xxxx00`。比如从 `0x0000FF` 开始写 2 个字节，第 2 个字节会卷回本页开头 `0x000000`，而不是写到 `0x000100`。
- **读数据没有页限制**，可以一口气连续读到底。

### 4.4 只需会用的部分

- `W25Q64_Ins.h` 里的大部分指令（双线/四线读、掉电、Block 擦除……）本例用不到，知道有这张表即可。
- `WaitBusy` 里的超时计数 `100000`，只是防死等的保护，数值不用纠结。

---

## 5. 主程序怎么写

### 5.1 初始化顺序（很重要）

```
HAL_Init → SystemClock_Config → MX_GPIO_Init → MX_DMA_Init → MX_SPI1_Init → MX_I2C1_Init
        → OLED_Init → W25Q64_Init
```

`W25Q64_Init()` 里只做了 `MySPI_Init()`（CS 置高），真正的 SPI 外设初始化已经由 `MX_SPI1_Init()` 完成，
所以 **W25Q64_Init 必须放在 MX_SPI1_Init / MX_GPIO_Init 之后**。

### 5.2 实验流程（与江协一致）

1. 读 ID 并显示 → 验证 SPI 通信是否正常
2. 擦除扇区 0
3. 往 0x000000 页编程写入 4 个字节
4. 从 0x000000 读回 4 个字节
5. 显示"W:"写入值和"R:"读出值，两行一致即成功

### 5.3 代码（写在 main.c 的 USER CODE 区内，CubeMX 重新生成不会被覆盖）

```c
/* USER CODE BEGIN Includes */
#include "OLED.h"
#include "W25Q64.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PV */
uint8_t  MID;                                   // 厂商 ID
uint16_t DID;                                   // 设备 ID
uint8_t  ArrayWrite[] = {0x01, 0x02, 0x03, 0x04}; // 要写入的数据
uint8_t  ArrayRead[4];                          // 读回的数据
/* USER CODE END PV */

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI1_Init();
  MX_I2C1_Init();

  /* USER CODE BEGIN 2 */
  OLED_Init();
  W25Q64_Init();

  /* 固定文字（8x16 字体：每字符宽 8 像素，每行高 16 像素） */
  OLED_ShowString(0,  0, "MID:   DID:", OLED_8X16);
  OLED_ShowString(0, 16, "W:", OLED_8X16);
  OLED_ShowString(0, 32, "R:", OLED_8X16);

  /* 1. 读 ID */
  W25Q64_ReadID(&MID, &DID);
  OLED_ShowHexNum(32, 0, MID, 2, OLED_8X16);   // 应显示 EF
  OLED_ShowHexNum(88, 0, DID, 4, OLED_8X16);   // 应显示 4017

  /* 2~4. 擦除 → 写入 → 读回 */
  W25Q64_SectorErase(0x000000);                // 先擦除（铁律③）
  W25Q64_PageProgram(0x000000, ArrayWrite, 4); // 写入 4 字节
  W25Q64_ReadData(0x000000, ArrayRead, 4);     // 读回 4 字节

  /* 5. 显示 */
  for (uint8_t i = 0; i < 4; i++)
  {
    OLED_ShowHexNum(24 + i * 24, 16, ArrayWrite[i], 2, OLED_8X16);
    OLED_ShowHexNum(24 + i * 24, 32, ArrayRead[i],  2, OLED_8X16);
  }

  OLED_Update();   // 本 OLED 驱动是显存缓冲式，Show 之后必须 Update 才会真正刷到屏上
  /* USER CODE END 2 */

  while (1)
  {
  }
}
```

> 坐标换算：江协旧版 OLED 用"行,列"（`OLED_ShowHexNum(1, 5, ...)`），本工程新版 OLED 用像素坐标。
> 8x16 字体下：X = (列 - 1) × 8，Y = (行 - 1) × 16。所以第 1 行第 5 列 → (32, 0)，第 12 列 → (88, 0)。

### 5.3.1 OLED 为什么用 `OLED_Update()`，不用 DMA？

本工程的 OLED 驱动同时提供两套刷新方式：

| 方式 | 调用 | 特点 |
| --- | --- | --- |
| 阻塞刷新 | `OLED_Update()` | 内部用 `HAL_I2C_Master_Transmit`，发完整帧才返回；简单 |
| DMA 刷新 | `OLED_Update_DMA()` 发起 + 主循环每圈 `OLED_Task()` 推进 | CPU 不用等，适合主循环很忙、需要频繁刷屏的场合 |

本例只在开机时显示一次结果，主循环里什么都不做，DMA 省下来的那点 CPU 时间没有用处，
反而多了状态机、忙标志、超时这些要考虑的东西。所以 **这里用阻塞的 `OLED_Update()` 就够了**，
DMA 的两个函数不用调用。CubeMX 里的 I2C DMA 配置保留不动即可（OLED.c 里仍引用了 DMA 接口，删掉反而可能编译报错）。

如果以后想练 DMA（比如在 while 里循环读 Flash 并刷屏），写法是：

```c
while (1)
{
    if (!OLED_IsBusy())          // 上一帧发完了才画下一帧
    {
        /* ...OLED_ShowXxx 画图... */
        OLED_Update_DMA();       // 只是提交请求，立即返回
    }
    OLED_Task();                 // 每圈都要调用，由它推进 DMA 发送
}
```

注意：DMA 刷新进行中（`OLED_IsBusy()==1`）时，`OLED_Update()` 会直接返回、不刷新，两套方式不要混着用。

### 5.4 预期显示

```
MID:EF DID:4017
W:01 02 03 04
R:01 02 03 04
```

---

## 6. 建议做的验证小实验（帮助理解三条铁律）

| 实验 | 操作 | 预期现象 | 说明了什么 |
| --- | --- | --- | --- |
| 改数据 | 把 ArrayWrite 改成 `{0x45,0x67,...}` 重新下载 | R 行跟着变 | 读写正常（HAL 课验证的 4567 就是这个） |
| 不擦除 | 注释掉 `SectorErase`，写入 `{0xAA,0xBB,0xCC,0xDD}`，在上一次写过 `{0x55,...}` 的基础上 | 读回 = 旧值 & 新值 | Flash 只能 1→0，写前必须擦 |
| 掉电保存 | 注释掉擦除和写入，只保留读，重新下载 | 仍能读到上次写的数据 | W25Q64 是非易失存储 |
| 跨页 | 从 `0x0000FF` 写 4 字节，再从 `0x000000` 读 | 后 3 字节出现在页开头 | 页编程不能跨页 |
| 去掉写使能 | 临时注释 `PageProgram` 里的 `WriteEnable` | 写入无效，读回 FF | 写使能是必需的 |

---

## 7. 常见问题速查

| 现象 | 可能原因 | 处理 |
| --- | --- | --- |
| MID=FF, DID=FFFF | MISO 没接/接错，或 CS 没拉低 | 查 PA6↔DO、PA4↔CS 接线 |
| MID=00, DID=0000 | 模块没供电，MISO 被拉地 | 查 VCC/GND |
| ID 对，但读回全 FF | 没写进去：写使能缺失，或写完没等忙就去读 | 检查 WriteEnable / WaitBusy |
| 读回数据错乱 | SPI 模式不对，或线太长频率太高 | 确认 CPOL=Low、CPHA=1Edge；分频改大试试 |
| OLED 不显示 | 忘了 `OLED_Update()` | Show 之后调用 Update |
| 编译报 FLASH_CS 未定义 | CubeMX 里片选脚名字和代码不一致 | 统一命名，或改 MySPI_W_SS |
| 编译报重复定义 | 软件 SPI 遗留函数没删干净 | 删掉旧的 MySPI_W_SCK / W_MOSI / R_MISO 等 |

---

## 8. 一句话总结

- **必须懂的**：SPI 是"交换字节"；CS 拉低开始、拉高结束；W25Q64 写操作 = 写使能 → 发指令+地址+数据 → 等忙；写前先擦、页内写、4KB 擦。
- **理解含义的**：CubeMX 的模式 0、软件 NSS、分频与 SCK 频率的关系。
- **会调用就行的**：HAL_SPI_TransmitReceive 的内部、OLED 驱动、指令表里其它指令。
