# STM32F429ZG 电机调试板 — Claude Code 工作规则

> 本文件是 Claude Code 的行为契约。非经开发者明确确认，任何约定不得自行修改。

---

## 1. 项目定位

```
PC 上位机  ↕ UART 双向（指令 + 状态上报 + 多通道波形）
STM32F429ZG  ↕ I2C
电机驱动 IC（型号不固定，均 I2C 通信）
```

- STM32 **不做闭环控制**，只做：解析上位机指令 → 读写电机 IC 寄存器 → 结果上报
- 电机 IC 地址通过运行时变量 `g_motorIcAddr` 动态设置，无需重新编译

---

## 2. 硬件规格

| 参数 | 值 | 说明 |
|------|----|------|
| MCU | STM32F429ZGT6 | Cortex-M4F, 180 MHz, HSE 8 MHz |
| Flash | 1 MB | 0x0800_0000 |
| SRAM1 | 112 KB | 0x2000_0000，主运行内存 |
| CCM | 64 KB | 0x1000_0000，**DMA 绝对禁止访问** |
| APB1 | 45 MHz | I2C1/2/3、USART2/3/4/5、TIM2-7/12-14 |
| APB2 | 90 MHz | USART1/6、TIM1/8-11 |

### 外设引脚配置

所有硬件参数集中在 `BSP/Inc/bsp_xxx.h` 宏定义中，换引脚只改宏。

| 外设 | 默认配置 | 宏前缀 | 说明 |
|------|----------|--------|------|
| LED1 | PG13 | `BSP_LED_` | 高电平点亮，心跳 |
| LED2 | PG14 | `BSP_LED_` | 高电平点亮，错误 |
| UART | USART1, PA9/PA10, AF7 | `BSP_UART_` | APB2, 115200 默认 |
| I2C | I2C1, PB6/PB7, AF4 | `BSP_I2C_` | APB1, 400 kHz 默认 |
| 采样定时器 | TIM6 | `BSP_SAMPLE_TIM_` | APB1, **ISR 名 `TIM6_DAC_IRQHandler`** |

> TIM6 时钟 = APB1 × 2 = 90 MHz。PSC=89 → 1 MHz，ARR = 1M/freq - 1

---

## 3. 构建系统与 SPL 规范

- **环境**：Windows MSYS2/MinGW（非 WSL），路径用 `/`，变量用 `$VAR`
- **工具链**：`arm-none-eabi-gcc` 12.x/13.x
- **外设库**：STM32F4xx SPL V1.9.0
- **烧录**：OpenOCD 0.12.x + ST-Link V2

```bash
make              # Release（-O2）
make DEBUG=1      # Debug（-Og -g3 -DDEBUG）
make flash        # 编译并烧录
make clean        # 删除 build/
```

### 编译标志（不得省略）

```makefile
CPU_FLAGS  = -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb
OPT_FLAGS  = -ffunction-sections -fdata-sections -fno-common
WARN_FLAGS = -Wall -Wextra -Wshadow
LINK_FLAGS = -Wl,--gc-sections -specs=nano.specs
```

预定义宏：`USE_STDPERIPH_DRIVER`、`STM32F429xx`、`HSE_VALUE=8000000U`

### 库路径

```makefile
SPL_DIR = STM32F4xx_DSP_StdPeriph_Lib_V1.9.0/Libraries
# 头文件：$(SPL_DIR)/STM32F4xx_StdPeriph_Driver/inc
#         $(SPL_DIR)/CMSIS/Device/ST/STM32F4xx/Include
#         $(SPL_DIR)/CMSIS/Include
# 源文件：$(SPL_DIR)/STM32F4xx_StdPeriph_Driver/src 中按需编译
```

### 内存分配

- **栈**：2 KB（`_Min_Stack_Size = 0x800`）
- **堆**：0（`_Min_Heap_Size = 0`），**全项目禁止 `malloc`/`free`，全用静态分配**

### SPL 关键规则

- `stm32f4xx_conf.h` 启用：rcc、gpio、usart、i2c、dma、tim、misc
- **外设初始化第一步必须使能 RCC 时钟**
- **GPIO 复用两步缺一不可**：① `GPIO_Mode_AF` + `GPIO_Init()` ② `GPIO_PinAFConfig()`
- USART1 TX DMA：**DMA2 Stream7 Channel4**

---

## 4. 目录结构与分层

```
Core/Inc/    main.h, stm32f4xx_conf.h, stm32f4xx_it.h
Core/Src/    main.c, stm32f4xx_it.c（全部 ISR 集中于此）,
             system_stm32f4xx.c, retarget.c（仅 DEBUG）
Core/Startup/  startup_stm32f429zgtx.s
BSP/Inc/     bsp_uart.h, bsp_i2c.h, bsp_tim.h, bsp_led.h, bsp_tick.h
BSP/Src/     bsp_uart.c, bsp_i2c.c, bsp_tim.c, bsp_led.c, bsp_tick.c
App/Inc/     app_protocol.h, app_motor.h, app_sample.h
App/Src/     app_protocol.c, app_motor.c, app_sample.c
STM32F4xx_DSP_StdPeriph_Lib_V1.9.0/Libraries/  ← 只读，Makefile 中用 SPL_DIR 变量指向
Linker/            STM32F429ZGTX_FLASH.ld
```

| 层 | 职责边界 |
|----|----------|
| `BSP/` 驱动层 | 只管硬件字节收发，不知道"帧"和"电机" |
| `App/` 应用层 | 协议解析、命令执行、数据上报调度。可用 `printf`（`#ifdef DEBUG`） |
| `Core/` 框架层 | 启动、中断向量、时钟、main 循环骨架 |

`main()` 初始化顺序：NVIC 分组 → SysTick → LED → UART → I2C → (采样定时器待启用) → App_Protocol_Init。主循环：`App_Protocol_Poll()` → `App_Sample_Poll()` → LED 心跳（500ms 翻转）。

`bsp_tick.c` 提供 `BSP_GetTick()` 返回毫秒计数（`volatile uint32_t`），SysTick_Handler 中递增。主循环用时间差控制 LED 翻转间隔。

---

## 5. 代码规范

### 命名

| 类别 | 规则 | 示例 |
|------|------|------|
| 宏 | 全大写下划线 | `BSP_UART_BAUDRATE_DEFAULT` |
| 类型 | PascalCase + `_t` | `Proto_Frame_t` |
| BSP 函数 | `BSP_模块_动词` | `BSP_UART_Init()` |
| App 函数 | `App_模块_动词` | `App_Protocol_Poll()` |
| 全局变量 | `g_` 前缀 | `uint8_t g_motorIcAddr` |
| 静态变量 | `s_` 前缀 | `static volatile uint8_t s_sampleFlag` |
| 局部变量 | camelCase | `uint8_t regVal` |
| 编译期常量 | `k_` 前缀 | `static const uint8_t k_protoSof` |

### 电机 IC 运行时配置

`g_motorIcAddr`（uint8_t, 7-bit, 默认 0x00）和 `g_motorIcName[16]`（默认 "UNKNOWN"）声明在 `bsp_i2c.h`，定义在 `bsp_i2c.c`。上位机通过协议设置，不做掉电保存。

### BSP 驱动层规则

- 每个外设一对 `bsp_xxx.h / .c`，硬件参数只在 `.h` 宏中定义
- 初始化统一返回 `ErrorStatus`
- 禁止 `printf()`、`Delay_ms()`、业务逻辑
- **错误处理策略**：BSP 返回 `ERROR` 时，应用层直接向上位机回复 NACK，**不做本地重试**（重试策略由上位机决定）

### 中断

`NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4)` — main() 第一行。

| 优先级 | 外设 |
|--------|------|
| 0 | 保留 |
| 2 | UART RX |
| 5/6 | I2C EV / ER |
| 7 | TIM6 采样（待启用） |
| 10 | UART TX DMA |
| 15 | SysTick |

ISR 只做最小操作（写缓冲区/置标志/清中断）。ISR 与主循环共享变量必须 `volatile`。临界区用 `__disable_irq()` / `__enable_irq()`。

---

## 6. 模块实现约定

### UART（`bsp_uart.c`）

- **接收**：RXNE 中断 → 环形缓冲区（256B，溢出丢弃新数据） → 主循环 `App_Protocol_Poll()` 解析
- **发送**：DMA2 Stream7 Ch4，单缓冲 256B（必须在 SRAM1），发送前检查 `s_uartTxDone`，未完成返回 `ERROR`（BUSY），由应用层下次 Poll 重试
- **retarget.c**：`printf` 重定向为 UART 轮询发送（仅 `#ifdef DEBUG`），不走 DMA，避免与数据通道冲突
- API：`Init()`、`Transmit(pData, len)`、`SetBaudrate(baudrate)`
- 支持波特率：9600 / 19200 / 38400 / 57600 / 115200 / 230400 / 460800 / 921600

### I2C（`bsp_i2c.c`）

- API：`Init()`、`WriteReg(devAddr, reg, pData, len)`、`ReadReg(...)`、`ReadRegs(devAddr, startReg, pData, len)`、`Scan(pAddrList, pCount)`
- 所有等待必须有超时，超时后 `BSP_I2C_RecoverBus()`（9 个 SCL 脉冲 + SoftwareReset）
- 7-bit 地址：`I2C_Send7bitAddress` 时 `devAddr << 1`

### 采样定时器（`bsp_tim.c`）— 待启用

API：`BSP_SampleTim_Init(freq)`、`SetFreq(freq)`、`Start()`、`Stop()`。ISR 只置 `s_sampleFlag`。

### 通信协议 — 待定

暂定帧格式 `[SOF 0xAA][LEN][CMD][DATA][CRC8]`。**协议未确定前禁止生成完整实现。**

#### 协议框架允许范围

协议分两层隔离：传输层（帧组装/校验，在 `app_protocol.c`）和命令层（分发/执行，调用 `app_motor.c` 等）。换协议只改传输层和命令字，BSP 层和 `main.c` 不受影响。

| 现在可以做 | 现在不可以做 |
|-----------|-------------|
| 定义 `Proto_Frame_t` 结构体 | 帧解析状态机函数体 |
| 帧常量宏（`PROTO_SOF`、`PROTO_MAX_DATA_LEN`） | CRC 计算实现 |
| 命令字枚举（值可暂定） | 命令分发 switch-case handler |
| `App_Protocol_Init()` / `Poll()` 空函数体 | 组包/回包函数体 |
| 命令处理函数指针类型定义 | |

#### 协议变更隔离点

- 帧常量（SOF、CRC 算法、字段顺序）→ `app_protocol.h` 宏
- 解析逻辑（状态机）→ `app_protocol.c` 内部 `static` 函数
- 命令字（枚举值）→ `app_protocol.h` 枚举

---

## 7. 实施路线图

| 阶段 | 内容 | 验收标准 | 状态 |
|------|------|----------|------|
| 0 | CLAUDE.md 规则确定 | 开发者确认 | **进行中** |
| 1 | Makefile + 启动文件 + LED 闪烁 | 编译通过，烧录后 LED 闪烁 | 未开始 |
| 2 | UART 收发（环形缓冲 + DMA 发送） | 串口助手手动收发字节 | 未开始 |
| 3 | I2C 驱动 + Scan | 接电机 IC，扫到地址 | 未开始 |
| 4 | 协议框架（空函数 + 结构体） | 编译通过，为协议讨论做准备 | 未开始 |
| 5 | （协议确定后）完整协议 + 采样上报 | 上位机完整通信 | 未开始 |

---

## 8. Claude Code 操作规范

### 工作流程

1. **先计划，后执行（强制）**：每次接到任务，必须先读取相关文件、列出修改计划并等待开发者确认，确认后才可动手修改。禁止跳过计划直接写代码
2. **修改前说明（强制）**：每次修改代码前，简要说明要改什么文件、要达到什么目的
3. **单功能逐个实现（强制）**：计划阶段必须将任务分解为独立的单个功能点，然后逐个实现、逐个验证。禁止一次性铺开多个功能同时编写
4. 新增代码时：确认 conf.h 包含对应头文件 → 建 .h/.c → Makefile 追加 → 编译验证
5. `make DEBUG=1 2>&1 | tail -30` 验证，汇报结果
6. 开发者明确要求总结时，在 `TRACKING/` 目录下生成总结文件，文件名格式 `MMDD_HHmm_summary.md`（月日\_时分）。内容结构：
   - **变更清单**：逐条列出本次新增/修改/删除的文件及具体改动
   - **当前系统状态**：路线图进度、各模块完成度、已知遗留问题

### 复用原则

- SPL/CMSIS 中已有的文件（启动文件、`system_stm32f4xx.c` 等）直接复制使用，不得重写

### 禁止行为

| 禁止 | 原因 |
|------|------|
| 修改 `StdPeriph_Driver/` 或 `CMSIS/` | 只读库 |
| 使用 HAL / LL API | 项目统一 SPL |
| BSP 层写业务逻辑或协议解析 | 违反分层 |
| ISR 中调用阻塞函数（printf/delay/malloc/while 等待） | 破坏实时性 |
| 外设操作前未使能 RCC 时钟 | HardFault |
| `.c` 中裸写端口/引脚数字 | 必须引用宏 |
| 硬编码电机 IC 地址 | 必须用 `g_motorIcAddr` |
| 协议未定前实现完整采样上报 | 等协议确定 |
| Makefile 反斜杠或 `%VAR%` | MSYS2 不兼容 |

### 必须询问开发者

- 新增未定义的外设或功能
- 协议帧格式或命令字有疑问
- 分配新的中断优先级
- 引脚宏与实际硬件不符

---

*MCU: STM32F429ZG | SPL V1.9.0 | GNU Make + MSYS2 | 通用电机调试板（无闭环）*
