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
| UART | USART1，PA9/PA10，默认 115200 bps |
| I2C | I2C1，PB6/PB7，400 kHz |
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
  Src/        bsp_led.c, bsp_tick.c, bsp_uart.c, bsp_i2c.c, bsp_tim.c

App/
  Inc/        app_protocol.h, app_motor.h, app_sample.h
  Src/        app_protocol.c, app_motor.c, app_sample.c

Linker/       STM32F429ZGTX_FLASH.ld
TRACKING/     开发阶段总结文件
protocol.MD   串口通讯协议文档
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
[0xBB][通道掩码][各通道数据 n 字节][XOR 校验]
```

- 每通道 2 字节 int16 大端，通道编号从低到高排列

### 已定义命令码

| 命令码 | 名称 | 说明 |
|--------|------|------|
| `0x00` | 心跳 | PC 每 1 s 发送，STM32 原样回复 |
| `0x01` | 错误响应 | STM32 遇异常时回复，数据段含 1 字节错误码 |

---

## 开发进度

| 阶段 | 内容 | 状态 |
|------|------|------|
| 0 | CLAUDE.md 规则确定 | 完成 |
| 1 | Makefile + 启动 + LED 闪烁 | 完成 |
| 2 | UART 收发（环形缓冲 + DMA 发送） | 完成 |
| 3 | I2C 驱动 + Scan | 未开始 |
| 3.2 | 协议格式确定 | 完成（protocol.MD v1.3） |
| 3.4 | 串口指令解析实现 | 未开始 |
| 3.5 | PMIC 配置 | 待定 |
| 4 | 完整协议 + 采样上报 | 未开始 |
