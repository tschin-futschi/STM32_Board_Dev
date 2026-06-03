# STM32F429ZG 电机调试板固件

基于 **STM32F429ZGT6** 的通用电机调试板固件。PC 上位机通过串口下发指令，STM32 解析后通过 I2C 读写电机驱动 IC 的寄存器并回传结果，同时支持多通道波形采样上报、信号发生、固件烧录与片上文件存储。STM32 **不做闭环控制**，只负责「解析指令 → 读写寄存器 → 结果上报」。使用 STM32F4xx SPL V1.9.0 标准外设库开发。

```
PC 上位机  ←→  UART（指令 + 状态上报 + 多通道波形）  ←→  STM32F429ZGT6  ←→  I2C  ←→  电机驱动 IC
```

电机 IC 型号不固定，均为 I2C 通信；其 7-bit 地址由运行时变量 `g_motorIcAddr` 动态设置，无需重新编译。

---

## 主要功能

| 功能 | 命令码 | 说明 |
|------|--------|------|
| 寄存器读写 | `0x20`~`0x22` | 单个读 / 单个写 / 批量读，寄存器地址为 16-bit（大端） |
| AW Firmware I2C 透传 | `0x30`/`0x31` | 任意 DevId / AddrSize（含 0）/ DataLen 的读写透传，服务 AW SDK DLL 回调 |
| AW86008/AW86100 本地 ISP 烧录 | `0x32`~`0x38` | 分包接收固件 → EXEC 端到端 5~10 s 烧录，带实时进度上报 |
| 片上 Flash 文件存储 | `0x39`~`0x3F` | Sector 5~11 共 896 KB 文件区，CRC32 校验，支持写入/读取/查询/清空 |
| 多通道波形采样 | `0x50`~`0x54` | TIM6 定时采样最多 8 通道，`0xBB` 数据流帧高频上报 |
| 信号发生器 | `0x55`~`0x58` | 线性 / 余弦 / 锯齿波，写入电机寄存器作激励 |
| PMIC 电压配置 | `0x08`~`0x0A` | RT5112WSC（I2C3）LDO 使能/电压设置，保障电机 IC 供电安全 |
| 系统控制 | `0x02`~`0x07`, `0x0B` | 设地址 / 波特率 / 复位 / Ping / I2C 扫描 / 启动状态上报 |

---

## 硬件规格

| 参数 | 值 |
|------|----|
| MCU | STM32F429ZGT6（Cortex-M4F，180 MHz，HSE 8 MHz） |
| Flash | 1 MB（Sector 5~11 共 896 KB 划作文件存储区） |
| SRAM1 | 112 KB（主运行内存） |
| CCM | 64 KB（**DMA 禁止访问**） |
| UART | USART1，PA9/PA10，AF7，默认 **460800 bps**，DMA2 Stream7 发送 |
| I2C1 | PB6/PB7，AF4，400 kHz，硬件 I2C，INA 功耗/电流测量（0x40） |
| I2C2 | PB10/PB11，AF4，**软件 bit-banging**，电机驱动 IC（地址运行时设置） |
| I2C3 | PA8/PC9，AF4，400 kHz，硬件 I2C，PMIC RT5112WSC（0x20）+ GYRO（0x68） |
| 采样定时器 | TIM6（`TIM6_DAC_IRQHandler`），间隔由 `0x52` 索引选择 |
| LED | PF13，高电平点亮，500 ms 心跳 |

> I2C2 采用软件 bit-banging：硬件 I2C 最高 400 kHz 达不到电机 IC 所需的 1 MHz，故用 GPIO 翻转 + DWT 周期延时实现。

---

## 环境要求

- **OS**：Windows 10/11
- **MSYS2**：安装至 `C:\msys64`（[下载](https://www.msys2.org/)）
- **工具链**：`arm-none-eabi-gcc` 12.x / 13.x
- **烧录**：ST-Link V2 + OpenOCD 0.12.x

### 安装工具链

打开 **MSYS2 MINGW64** 终端执行：

```bash
pacman -S --noconfirm mingw-w64-x86_64-arm-none-eabi-gcc mingw-w64-x86_64-arm-none-eabi-newlib make
```

验证：

```bash
arm-none-eabi-gcc --version   # 应显示 12.x 或 13.x
make --version                # 应显示 GNU Make
```

---

## 编译与烧录

在 **MSYS2 MINGW64** 终端中：

```bash
make              # Release 编译（-O2）
make DEBUG=1      # Debug 编译（-Og -g3 -DDEBUG）
make flash        # 编译并烧录
make clean        # 清理 build/
```

编译产物位于 `build/`：

| 文件 | 说明 |
|------|------|
| `firmware.elf` | 含调试信息，用于 GDB |
| `firmware.bin` | 纯二进制，用于烧录 |
| `firmware.hex` | Intel HEX，用于第三方烧录器 |
| `firmware.map` | 链接映射，查看内存占用 |

> 关键编译标志：`-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb`、`-Wall -Wextra -Wshadow`、`-Wl,--gc-sections -specs=nano.specs`。栈 2 KB，**堆为 0（全项目静态分配，禁用 malloc/free）**。

---

## 项目结构

```
Core/
  Inc/        main.h, stm32f4xx_conf.h, 中断声明
  Src/        main.c, stm32f4xx_it.c（全部 ISR）, system 时钟, retarget.c（仅 DEBUG）
  Startup/    GCC 启动文件

BSP/  ── 驱动层：只管硬件字节收发，硬件参数集中在 Inc/*.h 宏定义（换引脚只改宏）
  Src/        bsp_led.c, bsp_tick.c, bsp_uart.c,
              bsp_i2c.c, bsp_i2c1.c, bsp_i2c2.c, bsp_i2c3.c,
              bsp_pmic.c, bsp_tim.c, bsp_flash.c

App/  ── 应用层：协议解析、命令执行、采样调度
  Src/        app_protocol.c, app_motor.c, app_sample.c, app_flashstore.c

Flash/AW/AW86008_86100/   AW 电机 IC 本地 ISP 烧录模块（供应商代码 + STM32 移植层）
Test/                     测试框架（test_config.h 使能宏，默认全 0，正式固件自动裁剪）
PcTest/                   PC 端 Python 测试脚本（pyserial）
Linker/                   STM32F429ZGTX_FLASH.ld
DOC/                      讲解 HTML 文档
TRACKING/                 开发阶段总结
protocol.MD               串口通信协议文档（v2.11）
CLAUDE.md                 开发工作规则与架构契约
```

分层约束：`BSP/` 不知道「帧」和「电机」；`App/` 负责协议与业务；`Core/` 负责启动、中断、主循环骨架。

---

## 通信协议概述

完整定义见 [protocol.MD](protocol.MD)（v2.11）。

### 控制帧（双向，低频，可靠性优先）

```
[0xAA][0x55][帧序号][命令码][数据长度][数据 n 字节][CRC16 高字节][CRC16 低字节]
```

- CRC16-MODBUS（多项式 `0x8005`，初值 `0xFFFF`，低位先行），校验范围：帧序号起至数据末尾（不含帧头），结果**大端**存放
- PC 等待响应超时 100 ms，最多重发 2 次；STM32 收到重复帧序号不去重，直接重新执行
- 心跳：PC 每 1 s 发 `0x00`，STM32 原样回复；连续 3 次无回复判定断连

### 数据流帧（STM32→PC，高频采样上报，只发不收）

```
[0xBB][通道掩码][数据长度][各通道数据 n 字节][XOR 校验]
```

- 每通道 2 字节 int16 **大端**，按通道编号从低到高排列
- XOR 校验：通道掩码、数据长度与所有数据字节逐字节异或

### 命令码一览

**心跳与上报**

| 命令码 | 名称 | 方向 | 说明 |
|--------|------|------|------|
| `0x00` | 心跳 | 双向 | PC 每 1 s 发送，STM32 原样回复 |
| `0x01` | 错误响应 | STM32→PC | 1 字节错误码（`0x01` CRC 错 / `0x02` 未知命令 / `0x03` 执行失败） |
| `0x06` | 调试信息 | STM32→PC | ASCII 字符串，STM32 主动上报 |
| `0x0B` | 启动状态 | STM32→PC | 1 字节启动结果（OK / 各模块 init 失败码），SEQ=0xFF |

**系统控制（0x02~0x0A）**

| 命令码 | 名称 | 数据段 |
|--------|------|--------|
| `0x02` | 设置电机 IC 地址 | 1 字节 7-bit I2C 地址（设置前探测 ACK） |
| `0x03` | 设置波特率 | 1 字节索引（0x00~0x07 → 9600~921600） |
| `0x04` | 系统复位 | 空（回复后软件复位） |
| `0x05` | 电机 IC Ping | 空（测试电机 IC I2C 应答） |
| `0x07` | I2C 扫描 | 1 字节总线号（1/2/3）；返回 `[count][addr...]` |
| `0x08` | PMIC 使能 LDO | 空 |
| `0x09` | PMIC 设置电压 | 6 字节 `[DRVVDD][IOVDD][VCMVDD]`（各 2 字节） |
| `0x0A` | PMIC 禁用 LDO | 空 |

**寄存器读写（0x20~0x22）**

| 命令码 | 名称 | 数据段 |
|--------|------|--------|
| `0x20` | 读单个寄存器 | 2 字节寄存器地址（16-bit 大端） |
| `0x21` | 写单个寄存器 | 2 字节地址 + 2 字节数据（均大端） |
| `0x22` | 批量读寄存器 | 2 字节起始地址 + 2 字节数量，自动分包返回 |

**AW Firmware I2C 透传（0x30~0x31）**

| 命令码 | 名称 | 数据段 |
|--------|------|--------|
| `0x30` | AW Firmware I2C 写指令 | `[DevId][AddrSize][Addr...][DataLen][Data...]` |
| `0x31` | AW Firmware I2C 读指令 | `[DevId][AddrSize][Addr...][ReadLen]` |

**AW86008/AW86100 ISP 烧录（0x32~0x38）**

| 命令码 | 名称 | 数据段 / 响应 |
|--------|------|---------------|
| `0x32` | 烧录开始 | `[addr(4 LE)][totalBytes(4 LE)]`，totalBytes ≤ 64 KB |
| `0x33` | 烧录数据 | `[pktSeq(2 LE)][chunk]`；resp `[nextSeq(2 LE)]` |
| `0x34` | 执行烧录 | 空；resp `[ispStatus]`；阻塞 ~5-10 s |
| `0x35` | 烧录状态 | resp `[state][rxOffset(4)][totalBytes(4)]` |
| `0x36` | 取消烧录 | 空 |
| `0x37` | 复位芯片 | 空；resp `[ispStatus]` |
| `0x38` | 烧录进度 | STM32→PC 主动帧，`[phase][done(4 LE)][total(4 LE)]`，SEQ=0xFF |

**片上 Flash 文件存储（0x39~0x3F，Sector 5~11，896 KB）**

| 命令码 | 名称 | 数据段 / 响应 |
|--------|------|---------------|
| `0x39` | 写开始 | `[totalBytes(4 LE)]`；resp `[status]`；阻塞 ~3-7 s 整区擦 |
| `0x3A` | 写数据 | `[pktSeq(2 LE)][chunk]`；resp `[nextSeq(2 LE)]` |
| `0x3B` | 写结束 | `[expectedCrc32(4 LE)]`；resp `[status]`，CRC 校验通过才写元数据 |
| `0x3C` | 读开始 | 空；resp `[status][size(4 LE)][crc32(4 LE)]` |
| `0x3D` | 读数据 | `[pktSeq(2 LE)]`；resp `[chunk(≤252)]` |
| `0x3E` | 容量查询 | 空；resp `[totalCapacity(4 LE)][usedSize(4 LE)]` |
| `0x3F` | 清空存储区 | 空；resp `[status]`；阻塞 ~3-7 s 整区擦 |

**示波器与信号发生器（0x50~0x58）**

| 命令码 | 名称 | 数据段 |
|--------|------|--------|
| `0x50` | 启动采样 | 空（需先配置通道映射） |
| `0x51` | 停止采样 | 空 |
| `0x52` | 设置采样间隔 | 1 字节索引（0~6） |
| `0x53` | 设置通道掩码 | 1 字节，bit=1 表示该通道有效 |
| `0x54` | 设置通道寄存器映射 | 16 字节（8 通道 × 2 字节地址，大端） |
| `0x55` | 启动线性发生器 | 10 字节：addr + min + max + step + interval |
| `0x56` | 启动余弦发生器 | 7+N×4 字节：amp + off + freq + N + channels |
| `0x57` | 停止发生器 | 空 |
| `0x58` | 启动锯齿发生器 | 8 字节：addr + min + max + step |

> 电机 IC 寄存器地址为 **16-bit**，I2C 传输时高字节在前（大端）。

---

## PC 测试脚本

PC 端测试脚本统一存放于 `PcTest/`，仅依赖 `pyserial` + Python 标准库：

```bash
pip install pyserial
python PcTest/firmware_test_protocol_0331.py            # 协议解析与心跳
python PcTest/firmware_test_i2C_command_0329.py         # I2C 指令
python PcTest/firmware_test_aw_i2c_passthru_0513.py     # AW 透传读写
python PcTest/test_pmic_cmd09_0420.py                   # PMIC 电压设置
```

每个脚本将测试项统一汇总，全部通过时退出码为 0。需要电机在线的测试项通过 `--motor <addr>` 参数控制（`0x00` 跳过）。

---

## 开发进度

路线图阶段 **0 ~ 4.6 全部完成**，固件功能开发阶段结束，进入健壮性加固与联调阶段。

| 阶段 | 内容 | 状态 |
|------|------|------|
| 1 | Makefile + 启动文件 + LED 闪烁 | ✅ |
| 2 | UART 收发（环形缓冲 + DMA 发送） | ✅ |
| 3 | I2C 驱动（I2C1/2/3）+ Scan | ✅ |
| 3.2 / 3.4 | 协议格式确定 + 指令解析框架 | ✅ |
| 3.5 | PMIC 配置（RT5112WSC） | ✅ |
| 4.1 ~ 4.4 | 采样定时器 + 系统/寄存器/采样命令 | ✅ |
| 4.5 | AW Firmware I2C 透传读写（0x30/0x31） | ✅ |
| 4.6 | AW86008/AW86100 本地 ISP 烧录（0x32~0x38） | ✅ |
| — | 片上 Flash 文件存储（0x39~0x3F） | ✅ |
| — | 健壮性加固（2026-06-01 全量审查） | ✅ S1 / S3 / H-a ~ H-d 已修 |

### 2026-06-01 健壮性加固

一轮全量代码审查后修复了以下问题（修复细节与剩余待办见 [CLAUDE.md](CLAUDE.md) `## TODO`）：

| 编号 | 问题 |
|------|------|
| S1 | `bsp_flash` 写/擦地址下界保护，防误擦固件扇区变砖 |
| S3 | 软件 I2C2 拉伸超时的总线状态污染与读脏数据静默 |
| H-a | AW ISP I2C 返回值检查，失败快速返回（不再白等长延时） |
| H-b | AW 回读缓冲区栈越界（`r_buff[69]` → `[78]`） |
| H-c | `BSP_UART_Init()` 返回值检查，UART 故障专用快闪指示 |
| H-d | 采样/发生器运行时 I2C2 事务的并发保护补全（5 个 IO handler 全覆盖） |

完整路线图与待办（含剩余审查项）见 [CLAUDE.md](CLAUDE.md) 第 7 节与 `## TODO`。

---

## 相关文档

| 文档 | 内容 |
|------|------|
| [protocol.MD](protocol.MD) | 完整串口通信协议（v2.11） |
| [CLAUDE.md](CLAUDE.md) | 开发工作规则、架构契约、路线图与 TODO |
| `DOC/*.html` | 各模块讲解（PMIC 初始化、软件 I2C、采样流程等） |
| `TRACKING/*.md` | 各开发阶段变更总结 |
