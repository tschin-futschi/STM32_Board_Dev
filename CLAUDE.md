# STM32F429ZG 电机调试板 — Claude Code 工作规则

> 本文件是 Claude Code 的行为契约，每次开始任务前必须完整读取。
> 非经开发者明确确认，任何约定不得自行修改。

---

## 1. 项目定位

本板为**通用电机调试板**，唯一职责：

```
PC 上位机
    ↕  UART 双向（指令下发 + 状态数据定期上报 + 多通道波形数据上报）
STM32F429ZG
    ↕  I2C
电机驱动 IC（型号不固定，均通过 I2C 通信）
```

- STM32 **不做闭环控制**
- 功能：解析上位机指令 → 读写电机 IC 寄存器 → 将结果上报上位机
- 支持多通道数据定时上报，用于上位机波形显示（示波器功能）
- 电机 IC 可随时更换，地址通过运行时变量动态设置，无需重新编译

---

## 2. 硬件规格

### 2.1 MCU 核心参数

| 参数 | 值 | 说明 |
|------|----|------|
| MCU | STM32F429ZG（完整型号 STM32F429ZGT6） | Cortex-M4F，单精度 FPU |
| 封装 | LQFP144 | 114 个可用 GPIO |
| 主频 | 180 MHz | HSE 8 MHz → PLL，配置在 system_stm32f4xx.c |
| Flash | 1 MB | 0x0800_0000 |
| SRAM1 | 112 KB | 0x2000_0000，主运行内存 |
| SRAM2 | 16 KB | 0x2001_C000 |
| CCM | 64 KB | 0x1000_0000，**CPU 独占，DMA 绝对禁止访问** |
| APB1 | 45 MHz | I2C1/2/3、USART2/3/4/5、TIM2-7/12-14 挂此总线 |
| APB2 | 90 MHz | USART1/6、TIM1/8-11 挂此总线 |

### 2.2 Flash Sector 分布

| Sector | 大小 | 起始地址 |
|--------|------|----------|
| 0–3 | 16 KB × 4 | 0x0800_0000 |
| 4 | 64 KB | 0x0801_0000 |
| 5–11 | 128 KB × 7 | 0x0802_0000 |

### 2.3 板载外设引脚配置

#### 基础资源（已固定）

| 外设 | 引脚 | 说明 |
|------|------|------|
| LED1 | PG13 | 高电平点亮，心跳指示 |
| LED2 | PG14 | 高电平点亮，错误指示 |

#### UART — 上位机双向通信

> 所有硬件参数集中在 `BSP/Inc/bsp_uart.h` 宏定义中。
> 换引脚只改此处，驱动逻辑代码不需要动。
> 以下为默认配置（USART1，PA9/PA10），根据实际板子修改。

```c
/* 外设 */
#define BSP_UART_PERIPH             USART1

/* TX 引脚：PA9 */
#define BSP_UART_TX_PORT            GPIOA
#define BSP_UART_TX_PIN             GPIO_Pin_9
#define BSP_UART_TX_PINSRC          GPIO_PinSource9

/* RX 引脚：PA10 */
#define BSP_UART_RX_PORT            GPIOA
#define BSP_UART_RX_PIN             GPIO_Pin_10
#define BSP_UART_RX_PINSRC          GPIO_PinSource10

/* GPIO 时钟与复用功能 */
#define BSP_UART_GPIO_CLK           RCC_AHB1Periph_GPIOA
#define BSP_UART_AF                 GPIO_AF_USART1       /* = 7 */

/* 外设时钟：USART1 挂 APB2 */
#define BSP_UART_RCC_APB2           RCC_APB2Periph_USART1

/* 中断 */
#define BSP_UART_IRQn               USART1_IRQn
#define BSP_UART_IRQHandler         USART1_IRQHandler

/* 波特率默认值 */
#define BSP_UART_BAUDRATE_DEFAULT   115200U

/* ============================================================
 * 换引脚时参考：
 * USART1 → AF=7  TX=PA9,  RX=PA10  时钟：APB2
 * USART2 → AF=7  TX=PA2,  RX=PA3   时钟：APB1
 * USART3 → AF=7  TX=PB10, RX=PB11  时钟：APB1
 * UART4  → AF=8  TX=PA0,  RX=PA1   时钟：APB1
 * ============================================================ */
```

#### I2C — 电机驱动 IC

> 所有硬件参数集中在 `BSP/Inc/bsp_i2c.h` 宏定义中。
> 以下为默认配置（I2C1，PB6/PB7），根据实际板子修改。

```c
/* 外设 */
#define BSP_I2C_PERIPH              I2C1

/* SCL 引脚：PB6 */
#define BSP_I2C_SCL_PORT            GPIOB
#define BSP_I2C_SCL_PIN             GPIO_Pin_6
#define BSP_I2C_SCL_PINSRC          GPIO_PinSource6

/* SDA 引脚：PB7 */
#define BSP_I2C_SDA_PORT            GPIOB
#define BSP_I2C_SDA_PIN             GPIO_Pin_7
#define BSP_I2C_SDA_PINSRC          GPIO_PinSource7

/* GPIO 时钟与复用功能（I2C1/2/3 的 AF 编号均为 4）*/
#define BSP_I2C_GPIO_CLK            RCC_AHB1Periph_GPIOB
#define BSP_I2C_AF                  GPIO_AF_I2C1         /* = 4，I2C1/2/3 均相同 */

/* 外设时钟：I2C1/2/3 均挂 APB1 */
#define BSP_I2C_RCC_APB1            RCC_APB1Periph_I2C1

/* 中断 */
#define BSP_I2C_EV_IRQn             I2C1_EV_IRQn
#define BSP_I2C_ER_IRQn             I2C1_ER_IRQn
#define BSP_I2C_EV_IRQHandler       I2C1_EV_IRQHandler
#define BSP_I2C_ER_IRQHandler       I2C1_ER_IRQHandler

/* I2C 速率与超时 */
#define BSP_I2C_SPEED               400000U    /* 400 kHz 快速模式，可改为 100000U */
#define BSP_I2C_TIMEOUT             10000U     /* 软件超时计数上限 */

/* ============================================================
 * 换引脚时参考：
 * I2C1：SCL=PB6 或 PB8，SDA=PB7 或 PB9
 * I2C2：SCL=PB10 或 PF1，SDA=PB11 或 PF0
 * I2C3：SCL=PA8，SDA=PC9
 * 所有 I2C 外设 AF 编号均为 4
 * ============================================================ */
```

#### 定时器 — 多通道数据采样上报

> 以下为默认配置（TIM6），通讯协议确定后再启用。
> **Claude 在通讯协议未确定前，不得实现采样上报逻辑，只允许搭建框架。**

```c
/* 定时器选择：TIM6（基本定时器，挂 APB1）*/
#define BSP_SAMPLE_TIM              TIM6
#define BSP_SAMPLE_TIM_RCC          RCC_APB1Periph_TIM6
#define BSP_SAMPLE_TIM_IRQn         TIM6_DAC_IRQn        /* TIM6 与 DAC 共享向量 */
#define BSP_SAMPLE_TIM_IRQHandler   TIM6_DAC_IRQHandler  /* ISR 名称固定为此 */

/* 默认采样频率（上位机可动态修改）*/
#define BSP_SAMPLE_FREQ_DEFAULT     100U                 /* 100 Hz，待定 */

/* ============================================================
 * 时钟计算：TIM6 挂 APB1，定时器时钟 = APB1 × 2 = 90 MHz
 * PSC = 89（90MHz ÷ 90 = 1MHz）
 * ARR = 1MHz ÷ 频率 - 1
 * 例：100 Hz → ARR = 9999
 * 注意：TIM6 中断名是 TIM6_DAC_IRQHandler，不是 TIM6_IRQHandler
 * ============================================================ */
```

---

## 3. 构建系统

### 3.1 工具链与环境

- **开发环境**：Windows，MSYS2/MinGW 原生（不使用 WSL）
- **工具链**：`arm-none-eabi-gcc` 12.x 或 13.x，已加入 MSYS2 PATH
- **Make**：MSYS2 内置 GNU Make 4.x
- **烧录**：OpenOCD 0.12.x + ST-Link V2
- **外设库**：STM32F4xx 标准外设库 V1.8.0（SPL），**禁止使用 HAL / LL**

### 3.2 MSYS2 环境强制规则

- 所有路径使用正斜杠 `/`，**禁止反斜杠** `\`
- 变量引用使用 `$VAR`，**禁止** `%VAR%`（cmd.exe 语法）
- OpenOCD 通过 USB HID 识别 ST-Link，不依赖 COM 口编号

### 3.3 Make 目标

```bash
make              # Release 构建（-O2）
make DEBUG=1      # Debug 构建（-Og -g3 -DDEBUG）
make flash        # 编译并烧录
make debug_server # 启动 OpenOCD GDB server，端口 3333
make size         # 显示 Flash / RAM 占用
make clean        # 删除 build/
```

### 3.4 核心编译标志（不得省略）

```makefile
CPU_FLAGS  = -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb
OPT_FLAGS  = -ffunction-sections -fdata-sections -fno-common
WARN_FLAGS = -Wall -Wextra -Wshadow
LINK_FLAGS = -Wl,--gc-sections -specs=nano.specs
```

### 3.5 全局预定义宏

```c
USE_STDPERIPH_DRIVER
STM32F429xx
HSE_VALUE=8000000U
```

---

## 4. 目录结构

```
项目根/
├── CLAUDE.md
├── Makefile
├── Core/
│   ├── Inc/
│   │   ├── main.h
│   │   ├── stm32f4xx_conf.h      ← SPL 按需 include，见第 5.1 节
│   │   └── stm32f4xx_it.h
│   ├── Src/
│   │   ├── main.c                ← 主循环：轮询协议解析，轮询采样上报
│   │   ├── stm32f4xx_it.c        ← 全部 ISR 集中在此，不得分散
│   │   ├── system_stm32f4xx.c    ← 时钟配置，不手动修改 PLL 参数
│   │   └── retarget.c            ← printf 重定向（仅 DEBUG 模式编译）
│   └── Startup/
│       └── startup_stm32f429zgtx.s
├── BSP/
│   ├── Inc/
│   │   ├── bsp_uart.h            ← UART 引脚宏 + 函数声明
│   │   ├── bsp_i2c.h             ← I2C 引脚/速率宏 + 电机IC地址变量声明 + 函数声明
│   │   ├── bsp_tim.h             ← 采样定时器宏 + 函数声明
│   │   └── bsp_led.h
│   └── Src/
│       ├── bsp_uart.c            ← 环形缓冲接收 + DMA 发送
│       ├── bsp_i2c.c             ← 通用读/写/批量读/扫描，含总线软复位
│       ├── bsp_tim.c             ← 采样定时器初始化（框架已建，待启用）
│       └── bsp_led.c
├── App/
│   ├── Inc/
│   │   ├── app_protocol.h        ← 帧格式定义、命令字枚举（待完善）
│   │   ├── app_motor.h           ← 电机 IC 操作封装
│   │   └── app_sample.h          ← 多通道采样调度（待实现）
│   └── Src/
│       ├── app_protocol.c        ← 帧解析、命令分发、数据上报
│       ├── app_motor.c           ← 寄存器读写封装，换 IC 只改此文件
│       └── app_sample.c          ← 多通道采样数据打包上报（待实现）
├── StdPeriph_Driver/
│   ├── inc/                      ← 只读，禁止修改
│   └── src/                      ← 只读，禁止修改
├── CMSIS/
│   ├── Device/                   ← 只读，禁止修改
│   └── Include/                  ← 只读，禁止修改
└── Linker/
    └── STM32F429ZGTX_FLASH.ld
```

**分层职责**：

| 层 | 目录 | 职责边界 |
|----|------|----------|
| 驱动层 | `BSP/` | 只管硬件字节收发，不知道"帧"和"电机"是什么 |
| 应用层 | `App/` | 协议解析、命令执行、数据上报调度 |
| 框架层 | `Core/` | 启动、中断向量、时钟、main 循环骨架 |

---

## 5. SPL 外设库使用规范

### 5.1 stm32f4xx_conf.h 启用清单

```c
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_usart.h"     // UART 上位机通信
#include "stm32f4xx_i2c.h"       // I2C 电机 IC
#include "stm32f4xx_dma.h"       // UART TX DMA
#include "stm32f4xx_tim.h"       // 采样定时器
// #include "stm32f4xx_exti.h"   // 如需外部中断时开启
// #include "stm32f4xx_adc.h"    // 暂不使用
// #include "stm32f4xx_flash.h"  // IAP 时才开启
#include "misc.h"                // NVIC / SysTick，始终包含
```

### 5.2 时钟使能规则

**外设初始化函数第一步必须使能 RCC 时钟，Claude 生成代码时不得遗漏。**

```c
RCC_AHB1PeriphClockCmd(BSP_UART_GPIO_CLK,    ENABLE);  // UART GPIO（GPIOA）
RCC_AHB1PeriphClockCmd(BSP_I2C_GPIO_CLK,     ENABLE);  // I2C GPIO（GPIOB）
RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOG, ENABLE);  // LED
RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2,  ENABLE);  // USART1 TX 用 DMA2
RCC_APB2PeriphClockCmd(BSP_UART_RCC_APB2,    ENABLE);  // USART1
RCC_APB1PeriphClockCmd(BSP_I2C_RCC_APB1,     ENABLE);  // I2C1
RCC_APB1PeriphClockCmd(BSP_SAMPLE_TIM_RCC,   ENABLE);  // TIM6
```

### 5.3 GPIO 复用功能（两步缺一不可）

```c
// 步骤 1：设置引脚为复用模式
GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
GPIO_Init(GPIOx, &GPIO_InitStruct);
// 步骤 2：绑定具体 AF 编号
GPIO_PinAFConfig(GPIOx, GPIO_PinSourceN, GPIO_AF_xxx);
```

---

## 6. 代码规范

### 6.1 命名规范

```c
/* 宏：全大写 + 下划线 */
#define BSP_UART_BAUDRATE_DEFAULT   115200U
#define PROTO_FRAME_MAX_LEN         128U
#define PROTO_SOF                   0xAAU

/* 类型：PascalCase + _t */
typedef struct {
    uint8_t sof;
    uint8_t len;
    uint8_t cmd;
    uint8_t data[64];
    uint8_t crc;
} Proto_Frame_t;

/* BSP 函数：BSP_模块_动词 */
ErrorStatus BSP_UART_Init(void);
ErrorStatus BSP_UART_Transmit(uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C_Init(void);
ErrorStatus BSP_I2C_WriteReg(uint8_t devAddr, uint8_t reg,
                              uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C_ReadReg(uint8_t devAddr, uint8_t reg,
                             uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C_ReadRegs(uint8_t devAddr, uint8_t startReg,
                              uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C_Scan(uint8_t *pAddrList, uint8_t *pCount);

/* App 函数：App_模块_动词 */
void App_Protocol_Init(void);
void App_Protocol_Poll(void);
void App_Sample_Poll(void);       // 检查采样标志，打包上报

/* 静态/全局变量：s_ 前缀，ISR 共享加 volatile */
static volatile uint8_t  s_uartRxFlag  = 0U;
static volatile uint32_t s_sysTick     = 0U;
static volatile uint8_t  s_sampleFlag  = 0U;  // 定时器 ISR 置位，主循环清零

/* 局部变量：camelCase */
uint8_t regVal = 0U;

/* 编译期常量：k_ 前缀 */
static const uint8_t k_protoSof = 0xAAU;
```

### 6.2 电机 IC 运行时配置

**电机 IC 地址和名称为运行时变量，不使用宏定义，不做掉电保存。**
上位机通过通讯协议动态设置，每次上电后重新设置。

```c
/* bsp_i2c.h 中声明 */
extern uint8_t g_motorIcAddr;      /* 当前电机 IC 7-bit 地址，默认 0x00 */
extern char    g_motorIcName[16];  /* 当前电机 IC 型号字符串，默认 "UNKNOWN" */

/* bsp_i2c.c 中定义 */
uint8_t g_motorIcAddr    = 0x00U;
char    g_motorIcName[16] = "UNKNOWN";
```

所有 I2C 读写使用 `g_motorIcAddr`，禁止硬编码地址：

```c
BSP_I2C_ReadReg(g_motorIcAddr, reg, pData, len);
```

### 6.3 BSP 驱动层规则

- 每个外设一对 `bsp_xxx.h / bsp_xxx.c`，放 `BSP/`
- **所有硬件参数只在 `.h` 的宏中定义，`.c` 只引用宏，禁止裸端口/引脚数字**
- 初始化函数统一返回 `ErrorStatus`（`SUCCESS` / `ERROR`）
- 驱动层禁止调用 `printf()`，禁止含业务逻辑，禁止做协议解析
- 驱动层禁止调用 `Delay_ms()`，用硬件超时计数替代

### 6.4 应用层规则（`App/`）

- `app_protocol.c`：帧解包/组包、命令字分发、调用 BSP 执行、组织回包
- `app_motor.c`：I2C 寄存器操作封装，使用 `g_motorIcAddr` 作为设备地址
- `app_sample.c`：检查 `s_sampleFlag`，读取各通道数据，打包上报（待实现）
- 应用层可调用 `printf()`（`#ifdef DEBUG` 包裹），BSP 层不可以

### 6.5 中断优先级配置

```c
/* main() 第一行，全项目只调用一次 */
NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);  /* 4 位抢占，0 位子优先级 */
```

本项目优先级分配：

| 抢占优先级 | 外设 / 用途 | 说明 |
|-----------|-------------|------|
| 0 | 保留 | 不使用，留给将来硬件保护 |
| 2 | UART RX（RXNE 中断） | 确保不丢接收字节 |
| 5 | I2C 事件中断（EV） | 电机 IC 通信 |
| 6 | I2C 错误中断（ER） | 总线错误处理 |
| 7 | 采样定时器中断（TIM6） | 多通道数据采样触发（待启用） |
| 10 | UART TX DMA 完成 | 数据上报发送完成 |
| 15 | SysTick | 毫秒计时 |

### 6.6 ISR 编写规则

```c
/* UART 接收：只写环形缓冲区 */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(BSP_UART_PERIPH, USART_IT_RXNE) != RESET)
    {
        uint8_t byte = (uint8_t)USART_ReceiveData(BSP_UART_PERIPH);
        RingBuf_Push(&s_rxRing, byte);
    }
}

/* 采样定时器：只置标志，主循环负责读数据和上报 */
void TIM6_DAC_IRQHandler(void)
{
    if (TIM_GetITStatus(BSP_SAMPLE_TIM, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(BSP_SAMPLE_TIM, TIM_IT_Update);
        s_sampleFlag = 1U;    /* 唯一操作 */
    }
}
```

**ISR 内绝对禁止**：`printf`、`Delay_ms`、`malloc/free`、I2C 阻塞传输、任何 `while` 等待循环。

### 6.7 volatile 与临界区

```c
/* ISR 写、主循环读 → 必须 volatile */
static volatile uint8_t s_uartRxFlag  = 0U;
static volatile uint8_t s_sampleFlag  = 0U;

/* 保护共享数据 */
__disable_irq();
RingBuf_Push(&s_rxRing, byte);
__enable_irq();

/* 可嵌套场景 */
uint32_t primask = __get_PRIMASK();
__disable_irq();
/* ... */
__set_PRIMASK(primask);
```

---

## 7. 各模块实现约定

### 7.1 UART（`bsp_uart.c`）

**接收**：`RXNE` 中断逐字节写入环形缓冲区，主循环调用 `App_Protocol_Poll()` 解析完整帧。

**发送**：DMA 发送，避免阻塞主循环。发送前检查 `s_uartTxDone` 标志。

**波特率动态修改**：

```c
ErrorStatus BSP_UART_SetBaudrate(uint32_t baudrate);
/* 支持：9600 / 19200 / 38400 / 57600 / 115200 / 230400 / 460800 / 921600 */
```

### 7.2 I2C 通用驱动（`bsp_i2c.c`）

```c
ErrorStatus BSP_I2C_WriteReg(uint8_t devAddr, uint8_t reg,
                              uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C_ReadReg(uint8_t devAddr, uint8_t reg,
                             uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C_ReadRegs(uint8_t devAddr, uint8_t startReg,
                              uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C_Scan(uint8_t *pAddrList, uint8_t *pCount);
```

**超时机制**（所有 I2C 等待必须有超时）：

```c
uint32_t timeout = BSP_I2C_TIMEOUT;
while (I2C_GetFlagStatus(BSP_I2C_PERIPH, I2C_FLAG_BUSY) != RESET)
{
    if (--timeout == 0U) { BSP_I2C_RecoverBus(); return ERROR; }
}
```

**总线锁死恢复**：手动产生 9 个 SCL 脉冲 + `I2C_SoftwareResetCmd`。

**7-bit 地址规则**：

```c
I2C_Send7bitAddress(BSP_I2C_PERIPH, devAddr << 1, I2C_Direction_Transmitter);
```

### 7.3 采样定时器（`bsp_tim.c`）— 框架已建，待启用

```c
/* 初始化采样定时器，freq 单位 Hz */
ErrorStatus BSP_SampleTim_Init(uint32_t freq);

/* 动态修改采样频率（上位机命令触发）*/
ErrorStatus BSP_SampleTim_SetFreq(uint32_t freq);

/* 启动 / 停止采样 */
void BSP_SampleTim_Start(void);
void BSP_SampleTim_Stop(void);
```

### 7.4 上位机通信协议（`app_protocol.c`）— 待完善

> 通讯协议细节待后续单独讨论，以下为占位内容。
> **协议未确定前，Claude 不得基于此生成完整实现。**

帧格式（暂定）：

```
[SOF 0xAA][LEN 1B][CMD 1B][DATA N B][CRC8 1B]
  LEN = DATA 字段字节数
  CRC8 覆盖范围：LEN + CMD + DATA
```

命令字（暂定，待完善）：

```c
typedef enum {
    CMD_PING           = 0x01U,  /* 心跳 */
    CMD_I2C_WRITE_REG  = 0x10U,  /* 写电机 IC 寄存器 */
    CMD_I2C_READ_REG   = 0x11U,  /* 读单个寄存器 */
    CMD_I2C_READ_REGS  = 0x12U,  /* 批量读寄存器 */
    CMD_I2C_SCAN       = 0x13U,  /* 扫描 I2C 总线 */
    CMD_SET_MOTOR_ADDR = 0x14U,  /* 设置电机 IC 地址和名称 */
    CMD_REPORT_STATUS  = 0x20U,  /* 主动上报电机状态 */
    CMD_SET_BAUDRATE   = 0x30U,  /* 修改波特率 */
    /* 波形采样相关命令待通讯协议讨论后补充 */
    CMD_ACK            = 0xF0U,  /* 成功应答 */
    CMD_NACK           = 0xF1U,  /* 失败应答 */
} Proto_Cmd_t;
```

`CMD_SET_MOTOR_ADDR` 处理逻辑：

```c
case CMD_SET_MOTOR_ADDR:
    g_motorIcAddr = frame.data[0];
    memcpy(g_motorIcName, &frame.data[1], frame.len - 1U);
    g_motorIcName[15] = '\0';
    break;
```

### 7.5 主循环结构（`main.c`）

```c
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    SysTick_Config(SystemCoreClock / 1000);

    BSP_LED_Init();
    BSP_UART_Init();
    BSP_I2C_Init();
    /* BSP_SampleTim_Init(BSP_SAMPLE_FREQ_DEFAULT); */  /* 通讯协议确定后启用 */
    App_Protocol_Init();

    while (1)
    {
        App_Protocol_Poll();   /* 解析指令，执行，回包 */
        App_Sample_Poll();     /* 检查采样标志，上报波形数据（待实现）*/
        BSP_LED_Toggle(LED1);  /* 心跳 */
    }
}
```

---

## 8. Claude Code 操作规范

### 8.1 每次任务开始的标准流程

```
1. 完整读取本 CLAUDE.md
2. 用 Read 工具读取目标文件当前内容
3. 说明修改计划
4. 执行修改
5. 运行 make DEBUG=1 2>&1 | tail -30 验证编译
6. 汇报：修改了哪些文件，编译结果，warning 说明
```

### 8.2 新增代码的标准步骤

```
1. 确认 stm32f4xx_conf.h 已取消注释对应外设头文件
2. 新建 .h：引脚宏 / 类型定义 / 函数声明
3. 新建 .c：实现，第一步使能 RCC 时钟
4. 在 Makefile C_SOURCES 追加新的 .c 文件路径
5. 运行 make DEBUG=1 验证，无报错无 warning
```

### 8.3 禁止行为

| 禁止 | 原因 |
|------|------|
| 修改 `StdPeriph_Driver/` 或 `CMSIS/` | 只读库 |
| 使用 HAL / LL API | 项目统一使用 SPL |
| 在 BSP 层写业务逻辑或协议解析 | 违反分层原则 |
| ISR 中调用任何阻塞函数 | 破坏实时性 |
| 外设操作前未使能 RCC 时钟 | 触发 HardFault |
| .c 文件中出现裸端口/引脚数字 | 必须引用 .h 中的宏 |
| 使用宏硬编码电机 IC 地址 | 必须用 g_motorIcAddr 运行时变量 |
| 协议未确定前实现完整采样上报逻辑 | 等通讯协议讨论完成后再实现 |
| Makefile / 脚本使用反斜杠路径或 `%VAR%` | MSYS2 不兼容 |

### 8.4 必须询问开发者的情况

- 需要新增当前未定义的外设或功能
- 通讯协议帧格式或命令字有疑问
- 分配新的中断优先级（防止冲突）
- 采样定时器相关完整实现（协议未确定前禁止）
- 引脚宏与实际硬件不符时

---

## 9. 调试速查

### 9.1 HardFault 信息采集

```gdb
p/x SCB->CFSR
p/x SCB->HFSR
p/x SCB->MMFAR
p/x SCB->BFAR
info registers pc lr sp xpsr
backtrace
x/8xw $sp
```

### 9.2 常见问题速查

| 现象 | 优先排查 |
|------|----------|
| I2C BUSY 无法清除 | 调用 `BSP_I2C_RecoverBus()`；检查上拉电阻 |
| I2C 读回全 0xFF | `g_motorIcAddr` 是否已设置？是否左移 1 位？ |
| I2C Scan 扫不到设备 | 示波器确认波形；地址范围 0x08–0x77 |
| UART 无响应 | GPIO AF 是否配置？APB1/APB2 时钟是否正确？ |
| UART 乱码 | 波特率与上位机一致？HSE 是否 8 MHz？ |
| DMA 缓冲区数据异常 | 缓冲区是否在 CCM？→ 必须移至 SRAM1 |
| 采样数据不更新 | `s_sampleFlag` 是否被置位？定时器中断是否使能？ |
| TIM6 中断不进入 | ISR 名称是否为 `TIM6_DAC_IRQHandler`？ |
| 编译报 `undefined reference` | 对应 `.c` 是否加入 Makefile `C_SOURCES`？ |
| MSYS2 路径报错 | Makefile 所有路径改为正斜杠 `/` |

### 9.3 工具链验证

```bash
arm-none-eabi-gcc --version   # 期望：12.x 或 13.x
openocd --version              # 期望：0.12.x
make --version                 # 期望：GNU Make 4.x
```

---

## 10. 开发者 Checklist

**第一阶段 — 基础驱动（当前可开始）**

- [ ] 核对 `BSP/Inc/bsp_uart.h` 引脚宏与实际板子一致（默认 USART1 PA9/PA10）
- [ ] 核对 `BSP/Inc/bsp_i2c.h` 引脚宏与实际板子一致（默认 I2C1 PB6/PB7）
- [ ] 确认 `BSP_I2C_SPEED`（默认 400 kHz，如电机 IC 只支持 100 kHz 则改为 100000U）

**第二阶段 — 通讯协议确定后**

- [ ] 完善 `app_protocol.h` 命令字定义
- [ ] 确认采样定时器频率，启用 `BSP_SampleTim_Init()`
- [ ] 实现 `app_sample.c` 多通道采样上报逻辑

---

*MCU: STM32F429ZG | 库: STM32F4xx SPL V1.8.0 | 构建: GNU Make | 环境: MSYS2/MinGW*
*定位: 通用电机调试板（无闭环控制）| 通信: UART 双向 + I2C 通用读写 + 多通道波形上报（待实现）*
