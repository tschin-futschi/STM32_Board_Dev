# 代码审查记录 — 2026-05-01

## 审查范围

全量代码审查，覆盖 Core/BSP/App/Test 全部模块（34 个源文件），按 REVIEW_GUIDE.md P0→P3 优先级逐项推进。

### 审查文件清单

- **Core 层**: main.c, stm32f4xx_it.c, stm32f4xx_conf.h, main.h, stm32f4xx_it.h
- **BSP 层**: bsp_uart.c/h, bsp_i2c.c/h, bsp_i2c1.c/h, bsp_i2c2.c/h, bsp_i2c3.c/h, bsp_tim.c/h, bsp_led.c/h, bsp_tick.c/h, bsp_pmic.c/h
- **App 层**: app_protocol.c/h, app_motor.c/h, app_sample.c/h
- **协议文档**: protocol.MD（v1.9）

## 编译状态

| 模式 | 结果 | 项目警告 |
|------|------|----------|
| Release (-O2) | 通过 | 0（SPL 库自身 stm32f4xx.h 宏重定义警告、system_stm32f4xx.c 缩进警告非项目代码） |
| Debug (-Og) | 未测（环境限制，工具链可用但 make 路径需手动指定） | — |

### 资源用量

```
text    data    bss     dec     hex
22956   104     4664    27724   0x6C4C
```

- Flash 占用: ~23 KB / 1 MB (2.2%) — 充裕
- SRAM 占用: ~4.8 KB / 112 KB (4.3%) — 充裕（含 2KB 栈）

---

## 发现的问题

| 编号 | 优先级 | 维度 | 文件:行 | 描述 | 修复方案 | 状态 |
|------|--------|------|---------|------|----------|------|
| 1 | **P0** | ISR 安全性 | `app_sample.c:197-251` | TIM6 ISR 中执行阻塞式 I2C 读写 + cosf() 浮点运算。最坏执行时间 ~900us，远超最小定时器周期 150us（idx=0），可能导致采样溢出和中断堆积 | 已知设计折衷（commit 185219e）。建议：限制最低采样间隔档位下的最大通道数，或在代码中加入 WCET 防护检查 | 待确认 |
| 2 | **P0** | ISR 安全性 | `bsp_tim.c:34` | `s_callback` 函数指针缺少 `volatile` 修饰。编译器可能将其缓存到寄存器，ISR 读到旧值 | 改为 `static volatile BSP_SampleTim_Callback_t s_callback` | 待修复 |
| 3 | **P1** | 并发安全 | `app_sample.c:27-28` | `s_channelMask` 和 `s_channelRegMap[]` 未声明 `volatile`，但在 ISR (`SampleTimerISR`) 中被读取，在主循环命令中被修改 | 添加 `volatile` 限定符 | 待修复 |
| 4 | **P1** | 并发安全 | `app_sample.c:55-70` | Generator 相关变量（`s_linAddr`、`s_cosAmplitude`、`s_cosPhaseAccum` 等十余个）缺少 `volatile`，ISR 中读写 | 添加 `volatile` 限定符 | 待修复 |
| 5 | **P1** | 并发安全 | `app_sample.c:456-464` | `App_Sample_SetRegMap` 逐字节更新 `s_channelRegMap[]` 时无临界区保护，采样运行中调用会导致 ISR 读到半更新的映射 | 修改期间 `__disable_irq()` 保护，或要求先停止采样 | 待修复 |
| 6 | **P1** | 并发安全 | `app_sample.c:387-391` | 主循环连续读取多个 ISR 共享变量（`s_isrEffectiveMask`/`s_isrFailCount`/`s_isrTotalCount`/`s_isrWriteIdx`）之间无原子保护，可能读到不一致快照 | 用 `__disable_irq()` / `__enable_irq()` 包裹快照读取 | 待修复 |
| 7 | **P1** | 功能正确性 | `app_protocol.c:815-890` | 协议状态机无帧超时机制。若在 `STATE_WAIT_DATA` 等中间状态时后续字节不来，状态机永久停留无法恢复 | 增加字节间超时（如 50ms 无新字节则复位到 `STATE_WAIT_SOF1`） | 待修复 |
| 8 | **P1** | 外部输入防护 | `app_protocol.c:165` | `SendDebugInfo` 中 `uint8_t len = (uint8_t)strlen(msg)` 无截断保护，超过 255 字节的字符串会导致长度字段溢出 | 添加 `if (slen > PROTO_MAX_DATA_LEN) slen = PROTO_MAX_DATA_LEN` 截断 | 待修复 |
| 9 | **P1** | 功能正确性 | `bsp_tim.c:86` | `SetFreq` 运行中修改 ARR，若当前计数器 > 新 ARR，会产生一个异常长周期（计到 0xFFFF 后溢出） | 修改 ARR 后调用 `TIM_GenerateEvent(TIM6, TIM_EventSource_Update)` 强制重载，或启用 ARPE | 待修复 |
| 10 | **P1** | 外部输入防护 | `bsp_i2c.c:240,273` `bsp_i2c2.c:247,278` | I2C WriteReg/ReadReg 缺少 `pData == NULL` 防御性检查 | 入口处添加 NULL 检查返回 ERROR | 待修复 |
| 11 | **P2** | 鲁棒性 | `main.c:43-57` | I2C1/I2C2/I2C3 初始化失败时 `while(1)` 死循环无错误指示，无法区分哪条总线失败 | 用 LED 不同闪烁模式指示，或 UART 输出错误信息 | 待修复 |
| 12 | **P2** | 鲁棒性 | `bsp_uart.c:122-129` | `BSP_UART_Init()` 无条件返回 SUCCESS，无任何错误检查 | 低优先级，风格统一性改进 | 待修复 |
| 13 | **P2** | 并发安全 | `bsp_tim.c:140-143` | `SetCallback` 运行中修改回调指针无中断保护 | 修改前 `__disable_irq()` 或要求先 Stop | 待修复 |
| 14 | **P3** | 可维护性 | `bsp_led.h:3` | 文件注释 "PG13/PG14" 与实际 PF13 不符，且声称有 error LED（实际只有 1 个 LED） | 修正注释 | 待修复 |
| 15 | **P3** | 可维护性 | `app_protocol.h:5` `app_sample.h:5` | 代码注释引用协议版本 v1.4，实际协议文档已升级到 v1.9 | 更新版本号 | 待修复 |
| 16 | **P3** | 可维护性 | `app_sample.h:13` | 注释写默认间隔索引 4（1000us），实际默认值为索引 3（400us） | 修正注释 | 待修复 |
| 17 | **P3** | 魔法数字 | `stm32f4xx_it.c:25-38` | HardFault_Handler 中 `600000` 等裸数字 | 定义为宏 `HARDFAULT_BLINK_DELAY` | 待修复 |
| 18 | **P3** | 编译质量 | `system_stm32f4xx.c:753` | `-Wmisleading-indentation` 警告（SPL 库自带文件，不修改） | 不修复（只读库文件） | 忽略 |

---

## 审查通过项（确认无问题）

### P0 — ISR 安全性
- 中断优先级分配与设计文档一致 ✓
- `stm32f4xx_it.c` 中各 Handler 本身只做最小操作 ✓
- SysTick_Handler 仅递增 tick 计数器 ✓

### P1 — 功能正确性
- CRC16 算法与协议文档 v1.4+ 一致（直接多项式 0x8005 + 右移） ✓
- XOR 校验范围正确（mask ^ len ^ data） ✓
- 数据流帧：每通道 2 字节 int16 大端，按通道编号低→高 ✓
- I2C 7-bit 地址在 Send7bitAddress 时正确左移 1 位 ✓
- I2C2 电机 IC 寄存器地址 16-bit 大端发送 ✓
- I2C 超时 + 恢复机制完整（硬件 I2C: 9 SCL + SoftwareReset；软件 I2C: 9 SCL + STOP） ✓
- UART DMA：DMA2 Stream7 Channel4 ✓
- 环形缓冲区溢出丢弃新数据 ✓
- 命令码枚举与协议文档一一对应 ✓
- PMIC 电压公式 / 上电序列正确 ✓
- 同步丢失恢复（SOF2 状态收到 0xAA 保持等待）正确 ✓

### P1 — 并发
- `s_rxHead`/`s_rxTail`/`s_txDone` 正确标记 volatile ✓
- `s_sampleFlag` 正确标记 volatile ✓
- ISR 双缓冲变量（`s_isrBuf`/`s_isrWriteIdx`/`s_isrDataReady` 等）全部 volatile ✓
- DMA 缓冲区 `s_txBuf` 位于 SRAM1（.bss 段） ✓
- `g_pmicHwenFlag` 正确标记 volatile ✓

### P2 — 资源管理
- 无 malloc/free 调用 ✓
- Flash/SRAM 用量充裕 ✓
- 局部数组最大 ~255 字节（HandleI2CScan），2KB 栈可承受 ✓

### P2 — 分层
- BSP 层不含业务逻辑、不含 printf ✓
- App 层只调用 BSP 公开 API，不直接操作寄存器 ✓
- 硬件参数全部在 .h 宏中定义 ✓
- main.c 初始化顺序和主循环结构与设计文档一致 ✓
- conf.h 包含完整（rcc/gpio/usart/dma/i2c/tim/misc/exti/syscfg） ✓

### P2 — 鲁棒性
- BSP 返回 ERROR 时，App 层向上位机回复 NACK，不做本地重试 ✓
- I2C 总线恢复机制完整 ✓

### P3 — 可维护性
- 命名规范一致（BSP_/App_/g_/s_/k_ 前缀） ✓
- include guard 完整 ✓
- 关键数值均有宏定义 ✓

---

## 结论

**有条件通过**

整体代码质量较高，分层清晰，超时/恢复机制完整，volatile 使用大部分正确，协议实现与文档一致。

需优先处理的问题：
1. **P0 #2**：`s_callback` 缺少 volatile — 简单修复，风险明确
2. **P1 #3~#6**：采样模块多个 ISR 共享变量缺少 volatile / 临界区保护 — 并发安全隐患
3. **P1 #7**：协议状态机缺少帧超时 — 影响通信鲁棒性
4. **P1 #9**：SetFreq 运行中修改 ARR 无重载保护 — 可能导致采样异常

P0 #1（ISR 中 I2C 阻塞操作）为已知设计折衷，需开发者确认是否接受当前方案或调整架构。

*审查人: Claude Code | 审查方法: 全量代码阅读 + 编译验证 + 协议文档交叉比对*
