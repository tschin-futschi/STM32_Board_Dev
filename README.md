# STM32F429ZG 电机调试板固件

基于 STM32F429ZGT6 的通用电机调试板固件。PC 上位机通过串口下发指令，STM32 解析后通过 I2C 读写电机驱动 IC 寄存器并回传结果。使用 STM32F4xx SPL V1.9.0 标准外设库开发。

```
PC 上位机  ←→  UART（指令 + 状态上报 + 波形数据）  ←→  STM32F429ZGT6  ←→  I2C  ←→  电机驱动 IC
```

---

## 硬件规格

| 参数 | 值 |
|------|----|
| MCU | STM32F429ZGT6（Cortex-M4F，180 MHz，HSE 8 MHz） |
| Flash | 1 MB |
| SRAM1 | 112 KB（主运行内存） |
| CCM | 64 KB（DMA 禁止访问） |
| UART | USART1，PA9/PA10，默认 115200 bps，DMA 发送 |
| I2C1 | PB6/PB7，400 kHz，INA 功耗/电流测量（0x40） |
| I2C2 | PB10/PB11，400 kHz，电机驱动 IC（地址运行时设置） |
| I2C3 | PA8/PC9，400 kHz，PMIC RT5112WSC（0x20）+ GYRO（0x68） |
| 采样定时器 | TIM6，8 档频率（100 Hz ~ 10 kHz） |
| LED | PF13，高电平点亮，500 ms 心跳 |

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
make DEBUG=1      # Debug 编译（-Og -g3）
make flash        # 编译并烧录
make clean        # 清理 build/
```

编译产物位于 `build/`：

| 文件 | 说明 |
|------|------|
| `firmware.elf` | 含调试信息，用于 GDB |
| `firmware.bin` | 纯二进制，用于烧录 |
| `firmware.map` | 链接映射，查看内存占用 |

---

## 项目结构

```
Core/
  Inc/        main.h, stm32f4xx_conf.h, 中断声明
  Src/        main.c, 全部 ISR, system 时钟配置, retarget.c（仅 DEBUG）
  Startup/    GCC 启动文件

BSP/
  Inc/        板级驱动头文件（硬件参数宏定义，换引脚只改此处）
  Src/        bsp_led.c, bsp_tick.c, bsp_uart.c,
              bsp_i2c1.c, bsp_i2c2.c, bsp_i2c3.c,
              bsp_pmic.c, bsp_tim.c

App/
  Inc/        app_protocol.h, app_motor.h, app_sample.h
  Src/        app_protocol.c, app_motor.c, app_sample.c

Test/
  Inc/        test_config.h（测试使能宏），各模块测试头文件
  Src/        各模块测试实现

Linker/       STM32F429ZGTX_FLASH.ld
TRACKING/     开发阶段总结文件
protocol.MD   串口通信协议文档（v1.4）
```

---

## 通信协议概述

完整定义见 [protocol.MD](protocol.MD)。

### 控制帧（双向，低频，可靠性优先）

```
[0xAA][0x55][帧序号][命令码][数据长度][数据 n 字节][CRC16 高字节][CRC16 低字节]
```

- CRC16-MODBUS（多项式 `0x8005`，初值 `0xFFFF`，低位先行），校验范围：帧序号起至数据末尾，结果大端存放

### 数据流帧（STM32→PC，高频采样上报）

```
[0xBB][通道掩码][数据长度][各通道数据 n 字节][XOR 校验]
```

- 每通道 2 字节 int16 大端，通道编号从低到高排列
- XOR 校验：通道掩码、数据长度与所有数据字节逐字节异或

### 已定义命令码

| 命令码 | 名称 | 方向 | 说明 |
|--------|------|------|------|
| `0x00` | 心跳 | 双向 | PC 每 1 s 发送，STM32 原样回复 |
| `0x01` | 错误响应 | STM32→PC | 1 字节错误码（`0x01` CRC 错，`0x02` 未知命令，`0x03` 执行失败） |
| `0x02` | 设置电机 IC 地址 | PC→STM32 | 1 字节 7-bit I2C 地址，设置前探测 ACK |
| `0x03` | 设置波特率 | PC→STM32 | 1 字节索引（0x00~0x07 对应 9600~921600） |
| `0x04` | 系统复位 | PC→STM32 | 回复后执行软件复位 |
| `0x05` | 电机 IC Ping | PC→STM32 | 测试电机 IC I2C 应答 |
| `0x06` | 调试信息 | STM32→PC | ASCII 字符串，STM32 主动上报 |
| `0x20` | 读单个寄存器 | PC→STM32 | 2 字节寄存器地址（16-bit，大端） |
| `0x21` | 写单个寄存器 | PC→STM32 | 2 字节寄存器地址 + 2 字节数据（均大端） |
| `0x22` | 批量读寄存器 | PC→STM32 | 2 字节起始地址 + 2 字节数量（1~32130），自动分包返回 |
| `0x50` | 启动采样 | PC→STM32 | 需提前配置通道寄存器映射 |
| `0x51` | 停止采样 | PC→STM32 | 立即停止 TIM6 |
| `0x52` | 设置采样间隔 | PC→STM32 | 1 字节索引（0~7，对应 100 µs ~ 2000 µs） |
| `0x53` | 设置通道掩码 | PC→STM32 | 1 字节，bit=1 表示该通道有效 |
| `0x54` | 设置通道寄存器映射 | PC→STM32 | 16 字节，8 通道 × 2 字节寄存器地址（大端） |

> 电机 IC 寄存器地址为 **16-bit**，I2C 传输时高字节在前。

---

## 串口指令解析验证

烧录固件后，使用 `test.py` 自动验证串口指令解析是否正确。

### 依赖

```bash
pip install pyserial
```

### 运行

```bash
python test.py        # 默认使用 COM3
python test.py COM5   # 指定串口号
```

### 测试覆盖

| 测试用例 | 发送内容 | 期望响应 |
|----------|---------|---------|
| 心跳帧 | `CMD=0x00`，数据为空 | 原样回传，SEQ 一致 |
| CRC 错误帧 | CRC 故意填错 | `CMD=0x01`，`SEQ=0xFF`，错误码 `0x01` |
| 未知命令码 | `CMD=0x10` | `CMD=0x01`，错误码 `0x02` |
| 连续两帧 | 两次心跳 | 各自正确响应（验证状态机复位） |

全部通过输出 `4 通过 / 0 失败`，脚本退出码为 0。

---

## 开发进度

| 阶段 | 内容 | 状态 |
|------|------|------|
| 0 | CLAUDE.md 规则确定 | 完成 |
| 1 | Makefile + 启动文件 + LED 闪烁 | 完成 |
| 2 | UART 收发（环形缓冲 + DMA 发送） | 完成 |
| 3 | I2C 驱动（I2C1/2/3）+ Scan | 完成 |
| 3.2 | 协议格式确定（v1.4） | 完成 |
| 3.4 | 串口指令解析基础框架 | 完成 |
| 3.5 | PMIC 配置（RT5112WSC） | 完成 |
| 4.1 | 采样定时器 BSP（TIM6） | 完成 |
| 4.2 | 系统控制命令（0x02~0x05） | 完成 |
| 4.3 | 寄存器读写命令（0x20~0x22） | 完成 |
| 4.4 | 采样控制命令（0x50~0x54） | 完成 |
