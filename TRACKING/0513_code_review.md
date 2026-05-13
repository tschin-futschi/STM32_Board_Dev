# 代码 Review 报告 — 2026-05-13

> 范围：阶段 0–4.5 全量业务代码（不含 SPL / CMSIS）
> 协议基准：`protocol.MD` v2.5
> 编译目标：STM32F429ZG，`make DEBUG=1` 通过（text 23496B / bss 4928B）

---

## 1. 总评

| 维度 | 评分 | 说明 |
|------|------|------|
| 架构与分层 | A | BSP / App / Core 边界清晰；硬件参数集中在 `bsp_xxx.h` 宏；命名规范一致 |
| 协议正确性 | A− | 控制帧解析、CRC16、状态机健壮；新加 0x30 / 0x31 透传逻辑清晰 |
| 错误处理 | B | BSP 返回 ErrorStatus 一致；含 I2C RecoverBus / 50-tick auto-stop 兜底；少数 magic number |
| 并发安全 | **C** | **I2C2 总线在 ISR（采样）与主循环（寄存器/透传 IO）之间无互斥，存在抢断时总线损坏风险** |
| 资源使用 | A | 全静态分配，无堆；栈 2 KB 充裕；bss 4.9 KB |
| 文档一致性 | B− | CLAUDE.md 与代码漂移（心跳间隔 / 默认采样档位） |
| 测试覆盖 | B | 已有 4 个 PcTest 脚本覆盖系统控制 / 寄存器 / PMIC / 0x30-0x31；缺并发场景 |

**结论**：代码整体质量良好，主要功能完整可用。**唯一阻塞性问题**是 H1（I2C 总线 ISR vs 主循环并发），建议在下一次发版前修复；其它均为可控的局部改进点。

---

## 2. 严重问题（H）

### H1. I2C2 总线在 ISR 与主循环之间无互斥 ⚠️

**位置**
- `App/Src/app_sample.c:236-290`（`SampleTimerISR` 在 TIM6 中断里调用 `App_Motor_ReadReg`）
- `App/Src/app_protocol.c` 中 `HandleReadReg` / `HandleWriteReg` / `HandleBulkRead` / `HandleI2cPassWrite` / `HandleI2cPassRead`（主循环里调用 `BSP_I2C2_*`）
- `BSP/Src/bsp_i2c2.c` 底层位拼，无锁机制

**问题描述**
TIM6 ISR 优先级 7，主循环 CPU 优先级 0。采样运行中，主循环若执行任意寄存器 IO，TIM6 ISR 可随时抢断 → 两个上下文同时操作 PB10/PB11 GPIO → I2C 总线时序被破坏，可能出现：
- ACK 误读
- 数据字节位错乱
- 总线挂死，触发 RecoverBus（9 SCL 脉冲）

`protocol.MD` 第 4.4 节"I2C 统一调度"只覆盖**采样 + 发生器**的并发，未覆盖**PC 主动下发寄存器 IO + 采样运行**的并发。

**影响范围**
- 采样运行时所有 `0x20`/`0x21`/`0x22` 命令都有概率失败
- 新增 `0x30`/`0x31` 透传（载荷可达 255 B）放大问题
- 现有 `s_bulkReadActive` 标志只反向阻塞（bulk 进行中不响应其它命令），未正向保护采样

**建议修复**
仿 `s_bulkReadActive` 模式，加 sampling-active gate：

```c
/* 在 DispatchFrame 入口扩展 */
if (App_Sample_IsActive() && (
        pFrame->cmd == PROTO_CMD_READ_REG     ||
        pFrame->cmd == PROTO_CMD_WRITE_REG    ||
        pFrame->cmd == PROTO_CMD_BULK_READ    ||
        pFrame->cmd == PROTO_CMD_I2C_PASS_WRITE ||
        pFrame->cmd == PROTO_CMD_I2C_PASS_READ))
{
    SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
    return;
}
```

需同步在 `protocol.MD` 各命令"失败条件"中追加"采样运行中"。

---

### H2. `app_sample.h` 默认采样档位注释与实际不符

**位置** `App/Inc/app_sample.h:11-13`

```c
 * Defaults:
 *   Interval index : 4  (1000 us, 1 kHz)        ← 文档写的
```

实际：`BSP/Src/bsp_tim.c:33` 默认 `BSP_SAMPLE_TIM_DEFAULT_IDX = 3`，对应 400 μs / 2500 Hz，与 `protocol.MD` 4.4 节"采样默认值 索引 0x03（400 us）"一致。

**影响**：注释误导后续维护者；非 runtime 问题，纯文档漂移。

**建议**：直接改注释为 `idx 3 (400 us, 2500 Hz)`。

---

### H3. 全通道失败计数器误触发

**位置** `App/Src/app_sample.c:339-359`

```c
if (failCount >= totalCount)  /* ← totalCount==0 时 0>=0 为 true */
{
    s_i2cFailCount++;
    ...
}
```

**场景**：运行中若 `0x54` 把所有 reg map 清成 `0xFFFF`，下一次 ISR `totalCount==0`，`failCount==0`，进入"全失败"分支，连续 50 个 tick 后误报 `"I2C bus stuck, sampling auto-stopped"`。实际不是 I2C 故障，而是配置问题。

**建议**：

```c
if (totalCount > 0U && failCount >= totalCount) { ... }
else { s_i2cFailCount = 0U; s_i2cWarnSent = 0U; }
```

并行的 Poll() 在 `mask==0` 时已经会 auto-stop，但 ISR 仍可能在主循环执行 stop 之前持续累积——上述改动可双重保险。

---

### H4. TIM6 ISR 内 I2C clock-stretch 最长阻塞 10 ms

**位置** `BSP/Inc/bsp_i2c2.h:35`

```c
#define BSP_I2C2_SW_STRETCH_TIMEOUT 1800000U    /* 10 ms clock stretching */
```

**问题**：`SendByte` / `RecvByte` 等待 SCL 上升时若从机长时间拉低 SCL，最差等 10 ms。该函数在 `SampleTimerISR` 中被调用 → ISR 阻塞 10 ms → 期间所有 ≥7 优先级中断（TIM6 update 自身被压住下次重新触发，TX DMA TC=10、EXTI4=8）被延迟最长 10 ms。UART RX（优先级 2）可抢断 TIM6，所以接收不丢，但 TX DMA 完成中断延迟可能导致下一帧迟发。

**建议**：把 stretch timeout 缩到 ≤1 个 tick 周期（如 200 μs ≈ 36000 cycles），单次失败由 50-tick auto-stop 路径兜底。

---

## 3. 中等问题（M）

| ID | 位置 | 问题 | 建议 |
|----|------|------|------|
| M1 | `CLAUDE.md` 第 4 节 | "LED 心跳（500ms 翻转）"，实际 `main.c:25` `HEARTBEAT_INTERVAL_MS = 100U` | 同步文档为 100 ms |
| M2 | `Core/Src/main.c:42-63` | 各 `BSP_*_Init() != SUCCESS` 时 `while(1){;}` 死循环无反馈 | 用 LED 闪烁不同频率区分 I2C1/I2C2/I2C3/PMIC 失败 |
| M3 | `App/Src/app_protocol.c:166`, `App/Src/app_sample.c:344` | `SendDebugInfo` / auto-stop SendErrorResp 用 `SEQ=0x00`，与心跳响应 SEQ 撞 | 用 `0xFF` 表示"主动发送" |
| M4 | `App/Src/app_protocol.c:181` | `ProbeMotorIc` 用 `ReadReg(reg=0x0000, len=1)`，依赖从机支持读 reg 0 | 改为"START + addr+W + STOP" 纯 ACK 探测 |
| M5 | `Core/Src/stm32f4xx_it.c:38` | HardFault delay `for (i=0; i<600000; i++)` magic number | 定义 `HARDFAULT_BLINK_LOOPS` 宏 |
| M6 | `Core/Src/stm32f4xx_it.c:107` | EXTI4 "basic debounce" 仅 sample-and-test，名不副实 | 改注释为 "spurious-edge filter"；或用 SysTick 计数延迟二次采样 |
| M7 | `App/Src/app_protocol.c` 1095 行单文件 | 文件过大，命令组混在一起 | 后续拆为 `app_protocol_sys.c` / `_reg.c` / `_scope.c` / `_passthru.c` |
| M8 | `BSP/Inc/bsp_i2c2.h:42` | `extern uint8_t g_motorIcAddr;` 被 ISR 读、主循环写，无 `volatile` | 加 `volatile`（byte 读写硬件原子，但编译器可能寄存器化主循环侧） |

---

## 4. 低优先级（L）

| ID | 位置 | 问题 |
|----|------|------|
| L1 | `BSP/Src/bsp_uart.c:151,173` | `while (DMA_GetCmdStatus(...) != DISABLE) {}` 无超时；正常瞬时返回，DMA 异常时死循环 |
| L2 | `App/Src/app_protocol.c:152,166` 等多处 | `(void)SendFrame(...)` 丢弃返回值，BSP busy 时 ACK 静默丢失 — 关键场景（baudrate 切换响应）已通过 `BSP_UART_TxWait()` 兜底，但 `App_Protocol_SendErrorResp` 等无重试 |
| L3 | `PcTest/firmware_test_i2C_command_0329.py:13,29` | 默认 baud 仍 115200，与 v2.0 默认 460800 不一致；新脚本已用 460800 |
| L4 | `App/Src/app_sample.c:347-348` | `App_Sample_Stop()` 调用从 `SendStreamFrame` 内发起，调用栈较深；可读性上 stop 逻辑应在 Poll() 完成 |
| L5 | `App/Src/app_protocol.c:801` | `pFrame->len != (uint8_t)(7U + channelCount * 4U)` 类型转换正确，但语义可读性差；建议拆两步比较 |

---

## 5. 正面观察

✓ **状态机健壮**：`ProcessByte()` 在 `STATE_WAIT_SOF2` 若再次收到 `0xAA` 不退回 `SOF1`，避免连续帧头被错过
✓ **CRC16 实时累计**：状态机内逐字节更新 `s_rxCrcAccum`，无 buffer 二次扫描
✓ **错误恢复完备**：`BSP_I2C2_RecoverBus()` 9 SCL 脉冲 + SoftwareReset；UART `TxWait` 含 timeout 强制恢复
✓ **协议错误码语义清晰**：CRC 失败 `0xFF` 表示原 SEQ 不可信；其它错误回原 SEQ
✓ **耗时校验**：`CheckTickOverrun` 启动 sample/generator 前预估 I2C 耗时是否 < tick × 80%，超限拒绝并发 `0x06` 调试信息
✓ **全静态分配**：无 `malloc`/`free`，符合 CLAUDE.md 第 3 节
✓ **双缓冲设计**：`s_isrBuf[2]` ISR 写 / 主循环读，正确的 swap 时序（先写 buffer → swap idx → set dataReady）
✓ **新增 0x30/0x31 透传 API 设计**：参数解析含 `LEN == 3+AddrSize+DataLen` 完整一致性校验，BSP 层 AddrSize=0 / >0 走不同 I2C 时序符合协议
✓ **命名一致**：`BSP_xxx_动词` / `App_xxx_动词` / `s_/g_/k_` 前缀全项目统一

---

## 6. 模块完成度核查

| 阶段 | 模块 | 路线图状态 | 代码核查 | 备注 |
|------|------|----------|---------|------|
| 1 | LED / 启动 | 完成 | ✓ | `bsp_led.c` 41 行 |
| 2 | UART (RX 环形 + TX DMA) | 完成 | ✓ | `bsp_uart.c` 298 行 |
| 3 | I2C1/2/3 + Scan | 完成 | ✓ | I2C2 软件位拼 ~870 kHz |
| 3.2 | 协议格式 v2.5 | 完成 | ✓ | 命令枚举与文档一致 |
| 3.4 | 协议解析框架 | 完成 | ✓ | 状态机 + dispatch |
| 3.5 | PMIC RT5112WSC | 完成 | ✓ | 上电序列 + 回读验证 |
| 4.1 | TIM6 BSP | 完成 | ✓ | 7 档采样间隔 |
| 4.2 | 系统控制 0x02-0x0A | 完成 | ✓ | 全部含校验 |
| 4.3 | 寄存器读写 0x20-0x22 | 完成 | ✓ | bulk read 分包正确 |
| 4.4 | 采样/发生器 0x50-0x58 | 完成 | ✓ | 线性/余弦/锯齿三种 |
| **4.5** | **AW Firmware I2C 0x30/0x31** | **完成** | ✓ | 本次新增，编译通过；待硬件联调 |

**未覆盖（OPEN 项）**：
- 0x31 AddrSize=0 在 AW SDK 中实际是否被调用（待供应商书面确认）
- 0x30/0x31 在真实电机 IC 上的 I2C 时序验证（需硬件联调）

---

## 7. 建议下一步动作

按优先级排序：

1. **修 H1（采样运行时 I2C 命令 gate）** — 防止总线竞争，需协议文档同步更新
2. **修 H2 / H3** — 文件级别小改动，无风险
3. **修 H4** — 缩短 I2C clock-stretch 超时
4. **联调 0x30/0x31** — 用 `PcTest/firmware_test_aw_i2c_passthru_0513.py --motor 0xXX` 跑 T7.1-T7.8
5. **同步 CLAUDE.md** — M1, M5 文档修正
6. **拆分 app_protocol.c**（M7）— 单文件 >1000 行，长期维护成本高，可暂缓

---

*Reviewer: Claude / 模型 claude-opus-4-7[1m]*
*基线 commit: 87a9a3b（LED 心跳 100ms）+ 当前 HEAD 改动（0x30/0x31）*
