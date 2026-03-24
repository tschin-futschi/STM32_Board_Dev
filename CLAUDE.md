# STM32F429ZG 电机调试板 — Claude Code 工作规则

> 本文件是 Claude Code 的行为契约，每次开始任务前必须完整读取。
> 非经开发者明确确认，任何约定不得自行修改。

---

## 1. 项目定位

本板为**通用电机调试板**，唯一职责：

```
PC 上位机
    ↕  UART 双向（指令下发 + 状态数据定期上报）
STM32F429ZG
    ↕  I2C
电机驱动 IC（型号不固定，均通过 I2C 通信）
```

- STM32 **不做闭环控制**，无控制周期定时器需求
- 功能：解析上位机指令 → 读写电机 IC 寄存器 → 将结果上报上位机
- 电机 IC 可随时更换，切换只需修改 `bsp_i2c.h` 中的地址宏

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
| APB1 | 45 MHz | I2C1/2/3、USART2/3/4/5 挂此总线 |
| APB2 | 90 MHz | USART1/6 挂此总线 |

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

#### UART — 上位机双向通信（引脚开放配置）

> 所有硬件参数集中在 `BSP/Inc/bsp_uart.h` 宏定义中。
> 换引脚只改此处，驱动逻辑代码不需要动。
> **Claude 遇到仍为占位符（含 `_TODO_`）的宏时，必须停下来提示开发者填写，不得自行假设。**

```c
/* ============================================================
 * 开发者填写区 — UART 引脚配置
 * ============================================================ */

/* 外设选择：USART1 / USART2 / USART3 / UART4 / UART5 */
#define BSP_UART_PERIPH             USART_TODO_

/* TX 引脚 */
#define BSP_UART_TX_PORT            GPIO_TODO_PORT   /* 例：GPIOA */
#define BSP_UART_TX_PIN             GPIO_TODO_PIN    /* 例：GPIO_Pin_9 */
#define BSP_UART_TX_PINSRC          GPIO_TODO_PINSRC /* 例：GPIO_PinSource9 */

/* RX 引脚 */
#define BSP_UART_RX_PORT            GPIO_TODO_PORT
#define BSP_UART_RX_PIN             GPIO_TODO_PIN
#define BSP_UART_RX_PINSRC          GPIO_TODO_PINSRC

/* GPIO 时钟与复用功能 */
#define BSP_UART_GPIO_CLK           RCC_TODO_        /* 例：RCC_AHB1Periph_GPIOA */
#define BSP_UART_AF                 GPIO_AF_TODO_    /* 见下方速查表 */

/* 外设时钟（USART1/6 → APB2；其余 → APB1，二选一取消注释）*/
// #define BSP_UART_RCC_APB2        RCC_APB2Periph_TODO_
// #define BSP_UART_RCC_APB1        RCC_APB1Periph_TODO_

/* 中断 */
#define BSP_UART_IRQn               TODO_IRQn        /* 例：USART1_IRQn */
#define BSP_UART_IRQHandler         TODO_IRQHandler  /* 例：USART1_IRQHandler */

/* 波特率默认值（上位机可通过 CMD_SET_BAUDRATE 命令动态修改）*/
#define BSP_UART_BAUDRATE_DEFAULT   115200U

/* ============================================================
 * AF 编号速查
 * USART1 → GPIO_AF_USART1 = 7  常用：TX=PA9,  RX=PA10
 * USART2 → GPIO_AF_USART2 = 7  常用：TX=PA2,  RX=PA3
 * USART3 → GPIO_AF_USART3 = 7  常用：TX=PB10, RX=PB11
 * UART4  → GPIO_AF_UART4  = 8  常用：TX=PA0,  RX=PA1
 *
 * 时钟域速查
 * USART1 / USART6 → APB2，使用 RCC_APB2Periph_USARTx
 * USART2 / USART3 / UART4 / UART5 → APB1，使用 RCC_APB1Periph_USARTx
 * ============================================================ */
```

#### I2C — 电机驱动 IC（引脚与速率开放配置）

> 同上，所有硬件参数集中在 `BSP/Inc/bsp_i2c.h` 宏定义中。

```c
/* ============================================================
 * 开发者填写区 — I2C 引脚与速率配置
 * ============================================================ */

/* 外设选择：I2C1 / I2C2 / I2C3 */
#define BSP_I2C_PERIPH              I2C_TODO_

/* SCL 引脚 */
#define BSP_I2C_SCL_PORT            GPIO_TODO_PORT
#define BSP_I2C_SCL_PIN             GPIO_TODO_PIN
#define BSP_I2C_SCL_PINSRC          GPIO_TODO_PINSRC

/* SDA 引脚 */
#define BSP_I2C_SDA_PORT            GPIO_TODO_PORT
#define BSP_I2C_SDA_PIN             GPIO_TODO_PIN
#define BSP_I2C_SDA_PINSRC          GPIO_TODO_PINSRC

/* GPIO 时钟与复用功能（I2C1/2/3 的 AF 编号均为 4）*/
#define BSP_I2C_GPIO_CLK            RCC_TODO_        /* 例：RCC_AHB1Periph_GPIOB */
#define BSP_I2C_AF                  GPIO_AF_I2C1     /* I2C1/2/3 均为 4，直接用此宏 */

/* 外设时钟（I2C1/2/3 均挂 APB1）*/
#define BSP_I2C_RCC_APB1            RCC_APB1Periph_TODO_  /* 例：RCC_APB1Periph_I2C1 */

/* 中断 */
#define BSP_I2C_EV_IRQn             TODO_EV_IRQn     /* 例：I2C1_EV_IRQn */
#define BSP_I2C_ER_IRQn             TODO_ER_IRQn     /* 例：I2C1_ER_IRQn */
#define BSP_I2C_EV_IRQHandler       TODO_EV_IRQHandler
#define BSP_I2C_ER_IRQHandler       TODO_ER_IRQHandler

/* I2C 速率（单位 Hz，开放配置）
 * 标准模式：100000U（100 kHz）
 * 快速模式：400000U（400 kHz，需外部上拉电阻 ≤ 1 kΩ）*/
#define BSP_I2C_SPEED               400000U          /* 默认 400 kHz，可修改 */
#define BSP_I2C_TIMEOUT             10000U           /* 软件超时计数，防止死等 */

/* ============================================================
 * 电机 IC 配置（换型号只改这两行）
 * ============================================================ */
#define BSP_MOTOR_IC_ADDR           0x00U            /* 7-bit 地址，开发者填写 */
#define BSP_MOTOR_IC_NAME           "UNKNOWN"        /* 型号字符串，仅用于日志 */

/* ============================================================
 * I2C 常用引脚速查
 * I2C1：SCL=PB6 或 PB8，SDA=PB7 或 PB9
 * I2C2：SCL=PB10 或 PF1，SDA=PB11 或 PF0
 * I2C3：SCL=PA8，SDA=PC9
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
│   │   ├── main.c                ← 主循环：轮询协议解析，轮询上报调度
│   │   ├── stm32f4xx_it.c        ← 全部 ISR 集中在此，不得分散
│   │   ├── system_stm32f4xx.c    ← 时钟配置，不手动修改 PLL 参数
│   │   └── retarget.c            ← printf 重定向（仅 DEBUG 模式编译）
│   └── Startup/
│       └── startup_stm32f429zgtx.s
├── BSP/
│   ├── Inc/
│   │   ├── bsp_uart.h            ← UART 引脚宏（开发者填写）+ 函数声明
│   │   ├── bsp_i2c.h             ← I2C 引脚/速率/地址宏（开发者填写）+ 函数声明
│   │   └── bsp_led.h
│   └── Src/
│       ├── bsp_uart.c            ← 环形缓冲接收 + DMA 发送
│       ├── bsp_i2c.c             ← 通用读/写/批量读/扫描，含总线软复位
│       └── bsp_led.c
├── App/
│   ├── Inc/
│   │   ├── app_protocol.h        ← 帧格式定义、命令字枚举
│   │   └── app_motor.h           ← 电机 IC 操作封装（基于 bsp_i2c）
│   └── Src/
│       ├── app_protocol.c        ← 帧解析、命令分发、数据上报
│       └── app_motor.c           ← 寄存器读写封装，换 IC 型号只改此文件
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

本项目仅需以下外设，其余保持注释：

```c
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_usart.h"     // UART 上位机通信
#include "stm32f4xx_i2c.h"       // I2C 电机 IC
#include "stm32f4xx_dma.h"       // UART TX DMA
// #include "stm32f4xx_tim.h"    // 本项目无定时器需求
// #include "stm32f4xx_exti.h"   // 如需传感器 INT 脚中断时开启
// #include "stm32f4xx_adc.h"    // 暂不使用
// #include "stm32f4xx_flash.h"  // IAP 时才开启
#include "misc.h"                // NVIC / SysTick，始终包含
```

### 5.2 时钟使能规则

**外设初始化函数第一步必须使能 RCC 时钟，Claude 生成代码时不得遗漏。**

```c
RCC_AHB1PeriphClockCmd(BSP_UART_GPIO_CLK,        ENABLE);  // UART GPIO
RCC_AHB1PeriphClockCmd(BSP_I2C_GPIO_CLK,         ENABLE);  // I2C GPIO
RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOG,     ENABLE);  // LED
RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1,      ENABLE);  // 或 DMA2，按实际 UART 选择
/* UART 时钟：根据外设编号选其一 */
RCC_APB2PeriphClockCmd(BSP_UART_RCC_APB2,         ENABLE);  // USART1 或 USART6
// RCC_APB1PeriphClockCmd(BSP_UART_RCC_APB1,      ENABLE);  // USART2/3 等
/* I2C 时钟：I2C1/2/3 均挂 APB1 */
RCC_APB1PeriphClockCmd(BSP_I2C_RCC_APB1,          ENABLE);
```

### 5.3 GPIO 复用功能（两步缺一不可）

```c
// 步骤 1：配置为复用模式
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
#define BSP_MOTOR_IC_ADDR           0x60U
#define PROTO_FRAME_MAX_LEN         128U
#define PROTO_SOF                   0xAAU

/* 类型：PascalCase + _t */
typedef struct {
    uint8_t  sof;
    uint8_t  len;
    uint8_t  cmd;
    uint8_t  data[64];
    uint8_t  crc;
} Proto_Frame_t;

typedef enum {
    MOTOR_REG_STATUS  = 0x00U,
    MOTOR_REG_CTRL    = 0x01U,
} Motor_Reg_t;

/* BSP 函数：BSP_模块_动词 */
ErrorStatus BSP_UART_Init(void);
ErrorStatus BSP_UART_Transmit(uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C_Init(void);
ErrorStatus BSP_I2C_WriteReg(uint8_t devAddr, uint8_t reg,
                              uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C_ReadReg(uint8_t devAddr, uint8_t reg,
                             uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C_ReadRegs(uint8_t devAddr, uint8_t startReg,
                              uint8_t *pData, uint16_t len);   // 批量读
ErrorStatus BSP_I2C_Scan(uint8_t *pAddrList, uint8_t *pCount); // 扫描

/* App 函数：App_模块_动词 */
void App_Protocol_Init(void);
void App_Protocol_Poll(void);        // 主循环每次迭代调用一次
void App_Motor_ReportStatus(void);   // 由上位机命令触发或定期调用

/* 静态/全局变量：s_ 前缀，ISR 共享须加 volatile */
static volatile uint8_t  s_uartRxFlag  = 0U;
static volatile uint32_t s_sysTick     = 0U;

/* 局部变量：camelCase */
uint8_t regVal = 0U;

/* 编译期常量：k_ 前缀 */
static const uint8_t k_protoSof = 0xAAU;
```

### 6.2 BSP 驱动层规则

- 每个外设一对 `bsp_xxx.h / bsp_xxx.c`，放 `BSP/`
- **所有硬件参数只在 `.h` 的宏中定义，`.c` 只引用宏，禁止在 .c 中出现裸端口/引脚数字**
- 初始化函数统一返回 `ErrorStatus`（`SUCCESS` / `ERROR`）
- 驱动层禁止调用 `printf()`，禁止含业务逻辑，禁止做协议解析
- 驱动层禁止调用 `Delay_ms()`，用硬件超时计数替代

### 6.3 应用层规则（`App/`）

- `app_protocol.c`：帧解包/组包、命令字分发、调用 BSP 接口执行、组织回包
- `app_motor.c`：将命令翻译成具体 I2C 寄存器操作，**换 IC 型号时只改此文件和地址宏**
- 应用层可以调用 `printf()`（`#ifdef DEBUG` 包裹），BSP 层不可以

### 6.4 中断优先级配置

```c
/* main() 第一行，全项目只调用一次 */
NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);  /* 4 位抢占，0 位子优先级 */
```

本项目优先级分配：

| 抢占优先级 | 外设 / 用途 | 说明 |
|-----------|-------------|------|
| 2 | UART RX（RXNE 中断） | 确保不丢接收字节 |
| 5 | I2C 事件中断（EV） | 电机 IC 通信 |
| 6 | I2C 错误中断（ER） | 总线错误处理 |
| 10 | UART TX DMA 完成 | 数据上报发送完成 |
| 15 | SysTick | 毫秒计时 |

### 6.5 ISR 编写规则

```c
/* UART 接收：只写环形缓冲区 */
void BSP_UART_IRQHandler(void)
{
    if (USART_GetITStatus(BSP_UART_PERIPH, USART_IT_RXNE) != RESET)
    {
        uint8_t byte = (uint8_t)USART_ReceiveData(BSP_UART_PERIPH);
        RingBuf_Push(&s_rxRing, byte);  /* 唯一操作 */
    }
}
```

**ISR 内绝对禁止**：`printf`、`Delay_ms`、`malloc/free`、I2C 阻塞传输、任何 `while` 等待循环。

### 6.6 volatile 与临界区

```c
/* ISR 写、主循环读 → 必须 volatile */
static volatile uint8_t s_uartRxFlag = 0U;

/* 保护共享数据（环形缓冲区指针等） */
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

**接收**：`RXNE` 中断逐字节写入环形缓冲区，主循环调用 `App_Protocol_Poll()` 从缓冲区取数据解析完整帧，**不在 ISR 内解析**。

**发送**：DMA 发送，避免阻塞主循环。发送前检查上次 DMA 是否完成（`s_uartTxDone` 标志），防止覆盖发送缓冲区。

**波特率动态修改**（上位机发 `CMD_SET_BAUDRATE` 后调用）：

```c
ErrorStatus BSP_UART_SetBaudrate(uint32_t baudrate);
/* 支持：9600 / 19200 / 38400 / 57600 / 115200 / 230400 / 460800 / 921600 */
```

### 7.2 I2C 通用驱动（`bsp_i2c.c`）

提供四个通用接口，与具体电机 IC 型号完全解耦：

```c
/* 写单个或多个寄存器（连续地址） */
ErrorStatus BSP_I2C_WriteReg(uint8_t devAddr, uint8_t reg,
                              uint8_t *pData, uint16_t len);

/* 读单个寄存器 */
ErrorStatus BSP_I2C_ReadReg(uint8_t devAddr, uint8_t reg,
                             uint8_t *pData, uint16_t len);

/* 批量读：连续读取从 startReg 开始的 len 个寄存器 */
ErrorStatus BSP_I2C_ReadRegs(uint8_t devAddr, uint8_t startReg,
                              uint8_t *pData, uint16_t len);

/* 扫描总线：返回所有应答设备的 7-bit 地址列表（范围 0x08–0x77） */
ErrorStatus BSP_I2C_Scan(uint8_t *pAddrList, uint8_t *pCount);
```

**超时机制**（所有 I2C 等待必须有超时，禁止裸 while）：

```c
uint32_t timeout = BSP_I2C_TIMEOUT;
while (I2C_GetFlagStatus(BSP_I2C_PERIPH, I2C_FLAG_BUSY) != RESET)
{
    if (--timeout == 0U)
    {
        BSP_I2C_RecoverBus();  /* 自动触发总线软复位 */
        return ERROR;
    }
}
```

**总线锁死恢复 `BSP_I2C_RecoverBus()`**（内部函数，BUSY 超时后自动调用）：

1. 将 SCL 引脚临时切换为 GPIO 输出模式
2. 手动产生 9 个 SCL 时钟脉冲
3. 恢复 SCL 为 AF 模式
4. 调用 `I2C_SoftwareResetCmd(BSP_I2C_PERIPH, ENABLE)` 再 `DISABLE`

**7-bit 地址调用规则**：

```c
/* 发送地址时左移 1 位，SPL 不自动处理 */
I2C_Send7bitAddress(BSP_I2C_PERIPH, devAddr << 1, I2C_Direction_Transmitter);
```

### 7.3 上位机通信协议（`app_protocol.c`）

帧格式：

```
[SOF 0xAA][LEN 1B][CMD 1B][DATA N B][CRC8 1B]
  LEN = DATA 字段字节数（不含 SOF/LEN/CMD/CRC）
  CRC8 覆盖范围：LEN + CMD + DATA
```

命令字定义（`app_protocol.h`）：

```c
typedef enum {
    CMD_PING           = 0x01U,  /* 心跳，板子回 ACK + 固件版本 */
    CMD_I2C_WRITE_REG  = 0x10U,  /* DATA: [devAddr][reg][data...] */
    CMD_I2C_READ_REG   = 0x11U,  /* DATA: [devAddr][reg][readLen] */
    CMD_I2C_READ_REGS  = 0x12U,  /* DATA: [devAddr][startReg][readLen]，批量读 */
    CMD_I2C_SCAN       = 0x13U,  /* 无 DATA，回复总线上所有设备地址 */
    CMD_REPORT_STATUS  = 0x20U,  /* 板子主动上报，DATA 由 app_motor 填充 */
    CMD_SET_BAUDRATE   = 0x30U,  /* DATA: [baudrate 4B little-endian] */
    CMD_ACK            = 0xF0U,  /* 通用成功应答 */
    CMD_NACK           = 0xF1U,  /* 通用失败应答，DATA[0] = 错误码 */
} Proto_Cmd_t;
```

### 7.4 主循环结构（`main.c`）

```c
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    SysTick_Config(SystemCoreClock / 1000);   /* 1ms SysTick */

    BSP_LED_Init();
    BSP_UART_Init();
    BSP_I2C_Init();
    App_Protocol_Init();

    while (1)
    {
        App_Protocol_Poll();      /* 解析收到的帧，执行命令，发送回包 */
        BSP_LED_Toggle(LED1);     /* 心跳（可按需改为定时翻转）*/
    }
}
```

---

## 8. Claude Code 操作规范

### 8.1 每次任务开始的标准流程

```
1. 完整读取本 CLAUDE.md
2. 检查 bsp_uart.h 和 bsp_i2c.h 中的宏是否仍含 _TODO_ 占位符
   → 有占位符：停止，提示开发者先填写对应宏，等待确认后再继续
   → 无占位符：继续
3. 用 Read 工具读取目标文件当前内容
4. 说明修改计划（新增/修改哪些文件）
5. 执行修改
6. 运行 make DEBUG=1 2>&1 | tail -30 验证编译
7. 汇报：修改了哪些文件，编译结果，warning 说明
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
| .c 文件中出现裸端口/引脚数字（魔法数字） | 必须引用 .h 中的宏 |
| 宏含 `_TODO_` 时自行假设引脚并生成代码 | 必须先让开发者填写 |
| Makefile / 脚本使用反斜杠路径或 `%VAR%` | MSYS2 不兼容 |

### 8.4 必须询问开发者的情况

- `bsp_uart.h` 或 `bsp_i2c.h` 中仍有 `_TODO_` 占位符
- 需要新增当前未定义的外设或功能模块
- 上位机协议帧格式与第 7.3 节有差异
- 分配新的中断优先级（防止与已分配优先级冲突）
- 对某个外设挂在 APB1 还是 APB2 不确定

---

## 9. 调试速查

### 9.1 HardFault 信息采集（发生后在 GDB 执行）

```gdb
p/x SCB->CFSR
p/x SCB->HFSR
p/x SCB->MMFAR
p/x SCB->BFAR
info registers pc lr sp xpsr
backtrace
x/8xw $sp
```

将完整输出粘贴给 Claude 分析。

### 9.2 常见问题速查

| 现象 | 优先排查 |
|------|----------|
| I2C BUSY 无法清除，传输卡死 | 调用 `BSP_I2C_RecoverBus()`；检查上拉电阻是否焊接 |
| I2C 读回数据全 0xFF | 设备地址是否正确？是否已左移 1 位？ |
| I2C Scan 扫不到任何设备 | 用示波器确认 SCL/SDA 有波形；检查地址范围 0x08–0x77 |
| UART 收发无响应 | GPIO AF 是否配置？时钟是 APB1 还是 APB2？ |
| UART 接收乱码 | 波特率与上位机是否一致？HSE 是否为 8 MHz？ |
| 上位机收到的帧 CRC 校验失败 | 检查 CRC8 计算范围是否一致（LEN+CMD+DATA）|
| DMA 发送后数据未到上位机 | `s_uartTxDone` 是否正确置位？DMA 缓冲区是否在 CCM？ |
| DMA 缓冲区数据异常 | 缓冲区是否在 CCM（0x1000_0000）？→ 必须移至 SRAM1 |
| printf 无输出 | `retarget.c` 是否加入编译？是否用 `DEBUG=1` 构建？ |
| 编译报 `undefined reference` | 对应 `.c` 是否加入 Makefile `C_SOURCES`？ |
| MSYS2 路径报错 | Makefile 所有路径改为正斜杠 `/` |

### 9.3 工具链验证（首次搭建后执行）

```bash
arm-none-eabi-gcc --version   # 期望：12.x 或 13.x
openocd --version              # 期望：0.12.x
make --version                 # 期望：GNU Make 4.x
```

---

## 10. 开发者上线前 Checklist

完成以下步骤后，即可让 Claude Code 开始生成代码：

- [ ] 填写 `BSP/Inc/bsp_uart.h` 中所有 `_TODO_` 宏（外设编号、TX/RX 引脚、AF、时钟域、IRQ）
- [ ] 填写 `BSP/Inc/bsp_i2c.h` 中所有 `_TODO_` 宏（外设编号、SCL/SDA 引脚、时钟域、IRQ）
- [ ] 确认 `BSP_I2C_SPEED`（默认 400000U，如电机 IC 只支持 100 kHz 则改为 100000U）
- [ ] 填写 `BSP_MOTOR_IC_ADDR`（连接的第一颗电机 IC 的 7-bit 地址）
- [ ] 填写 `BSP_MOTOR_IC_NAME`（型号字符串，用于日志输出）

---

*MCU: STM32F429ZG | 库: STM32F4xx SPL V1.8.0 | 构建: GNU Make | 环境: MSYS2/MinGW*
*定位: 通用电机调试板（无闭环控制）| 通信: UART 双向 + I2C 通用读写*
