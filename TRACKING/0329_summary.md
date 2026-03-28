# 总结 0329

## 变更清单

### 新增文件

| 文件 | 说明 |
|------|------|
| `BSP/Inc/bsp_tim.h` | TIM6 采样定时器 BSP 头文件，含硬件配置宏、采样间隔表、API 声明 |
| `BSP/Src/bsp_tim.c` | TIM6 初始化、Start/Stop、SetFreq、标志位管理、ISR Callback |
| `App/Inc/app_motor.h` | 电机寄存器读写接口声明 |
| `App/Src/app_motor.c` | 封装 BSP_I2C2，实现 16-bit 大端寄存器读写 |
| `App/Inc/app_sample.h` | 采样模块接口声明，含通道配置、启停、轮询 API |
| `App/Src/app_sample.c` | 采样状态管理、TIM6 标志轮询、0xBB 数据流帧组帧发送 |

### 修改文件

| 文件 | 具体改动 |
|------|----------|
| `Core/Inc/stm32f4xx_conf.h` | 新增 `#include "stm32f4xx_tim.h"` |
| `Core/Src/stm32f4xx_it.c` | 新增 `TIM6_DAC_IRQHandler`，新增 `#include "bsp_tim.h"` |
| `Core/Src/main.c` | 新增 `#include "app_sample.h"`，初始化序列追加 `App_Sample_Init()`，主循环追加 `App_Sample_Poll()` |
| `App/Inc/app_protocol.h` | 枚举追加：`0x06`（调试信息）、`0x02~0x05`（系统控制）、`0x20~0x22`（寄存器读写）、`0x50~0x54`（采样控制），注释更新为 v1.4 |
| `App/Src/app_protocol.c` | 新增 include（bsp_i2c2、app_motor、app_sample、string.h）；新增波特率查表；新增 `SendDebugInfo()`；新增 9 个命令 handler；`DispatchFrame` 追加 9 个 case；`HandleBulkRead` 增加采样进行中检查 |
| `Makefile` | 追加 `BSP/Src/bsp_tim.c`、`App/Src/app_motor.c`、`App/Src/app_sample.c`、`$(SPL_DRV_DIR)/src/stm32f4xx_tim.c` |
| `CLAUDE.md` | 路线图 4.1~4.4 全部标注完成；新增 TODO 章节（阶段 4 代码 Review）；新增语言约束规则（仅汉语/英语） |

## 当前系统状态

### 路线图进度

| 阶段 | 内容 | 状态 |
|------|------|------|
| 0~3.5 | 所有基础阶段 | **完成** |
| 4.1 | 采样定时器 BSP（`bsp_tim.c`） | **完成** |
| 4.2 | 系统控制命令（`0x02~0x05`） | **完成** |
| 4.3 | 寄存器读写命令（`0x20~0x22`，`app_motor.c`） | **完成** |
| 4.4 | 采样控制命令（`0x50~0x54`，`app_sample.c`） | **完成** |

### 已实现命令码汇总

| 命令码 | 名称 | 状态 |
|--------|------|------|
| `0x00` | 心跳 | 完成 |
| `0x02` | 设置电机 IC 地址 | 完成 |
| `0x03` | 设置波特率 | 完成 |
| `0x04` | 系统复位 | 完成 |
| `0x05` | 电机应答测试 | 完成 |
| `0x06` | 调试信息（STM32→PC） | 完成 |
| `0x20` | 读单个寄存器 | 完成 |
| `0x21` | 写单个寄存器 | 完成 |
| `0x22` | 批量读寄存器（分包） | 完成 |
| `0x50` | 启动采样 | 完成 |
| `0x51` | 停止采样 | 完成 |
| `0x52` | 设置采样间隔 | 完成 |
| `0x53` | 设置采样通道 | 完成 |
| `0x54` | 设置通道寄存器映射 | 完成 |

### 已知遗留问题
- `TEST_I2C_SCAN` 当前为 1，正式固件前需改回 0
- GYRO 器件型号（I2C3, 0x68）待确认
- **阶段 4 代码 Review 待执行**（已记录至 CLAUDE.md TODO）
