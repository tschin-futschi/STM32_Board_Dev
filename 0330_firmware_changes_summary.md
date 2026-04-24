# 0330 Firmware Changes Summary

## Scope

This document summarizes only the STM32 firmware-side changes made during this debugging session.
It excludes `PcTest` script rewrites and other PC-side test harness changes.

Target project:

- MCU: `STM32F429ZGT6`
- Main protocol path: UART <-> STM32 <-> I2C motor/PMIC devices

## Confirmed Firmware Outcomes

The following firmware behaviors were manually verified on hardware:

- Heartbeat command `0x00` works at `115200`
- Motor address set command `0x02` works with motor `7-bit` address `0x6F`
- Motor ping command `0x05` works
- Single register read `0x20` works for:
  - `0xB000 -> 0x8600`
  - `0xB002 -> 0xD208`
- Dual-channel sampling works with:
  - `ch0 -> 0xB000`
  - `ch1 -> 0xB002`
- Streaming frames were manually observed as:
  - `BB 03 04 86 00 D2 08 5B`
- Baudrate switching `115200 -> 57600 -> 115200` was manually verified to work after the UART BSP fix

## Firmware Files Changed

### [BSP/Src/bsp_uart.c](D:/echo.qfq/24_Codex/19_STM32_Project/BSP/Src/bsp_uart.c)

#### 1. Hardened `BSP_UART_SetBaudrate()`

Earlier behavior was too lightweight for baudrate switching and could leave USART/DMA state inconsistent.

Current behavior now:

- disables `RXNE` interrupt before reconfiguration
- disables USART DMA TX request before reconfiguration
- disables DMA TX stream and waits for it to become idle
- clears DMA stream status flags
- forces `s_txDone = 1`
- waits for `USART_FLAG_TC` so the final transmitted bit is actually on the wire
- disables USART
- calls `USART_DeInit(USART1)` for a full peripheral reset
- reads `SR` then `DR` to clear transition noise / stale RX status
- re-runs UART peripheral initialization with the new baudrate

Why this matters:

- This addressed the symptom where command `0x03` returned an ACK but the actual baudrate did not reliably switch.

#### 2. Existing TX wait / transition safety kept in place

`BSP_UART_TxWait()` is still used before baudrate switch and reset-sensitive flows to ensure:

- DMA transfer complete
- USART shift register drained (`TC`)

This is required so ACK frames are not truncated before a reset or baudrate change.

### [App/Src/app_protocol.c](D:/echo.qfq/24_Codex/19_STM32_Project/App/Src/app_protocol.c)

#### 3. `SendFrame()` retries when UART TX is temporarily busy

`SendFrame()` was changed to retry a few times instead of failing immediately when DMA TX is busy.

Why this matters:

- Prevents transient command ACK loss under UART contention
- Reduced cases where command handlers would continue state transitions without the PC reliably seeing the ACK

#### 4. `HandleSetBaudrate()` only switches after ACK transmission succeeds

Current behavior:

- sends ACK first at the old baudrate
- if ACK cannot be queued, it aborts the baudrate change
- waits for TX completion
- then performs `BSP_UART_SetBaudrate(...)`
- flushes RX garbage after the transition
- resets protocol RX parser state to `STATE_WAIT_SOF1`

Why this matters:

- Prevents the PC and MCU from losing sync because the MCU switched baudrate before the ACK was actually delivered

#### 5. `HandleReset()` only resets after ACK transmission succeeds

Current behavior:

- sends reset ACK first
- if ACK cannot be queued, reset is aborted
- waits for TX completion
- then calls `NVIC_SystemReset()`

Why this matters:

- Prevents silent resets where the host never sees the reset ACK

#### 6. Bulk-read path waits for previous TX to complete

In `HandleBulkRead()`:

- each packet send is preceded by `BSP_UART_TxWait()`

Why this matters:

- stabilizes multi-packet `0x22` responses
- avoids overlapping packet transmission under DMA TX pressure

### [App/Src/app_sample.c](D:/echo.qfq/24_Codex/19_STM32_Project/App/Src/app_sample.c)

#### 7. Sampling stream TX now waits for UART TX availability

Before emitting a sampling stream frame, the code now calls:

- `BSP_UART_TxWait()`

Why this matters:

- reduces collisions between stream frames and control responses
- helps keep sample streaming alive without corrupting UART DMA state

### [Test/Src/test_i2c_scan.c](D:/echo.qfq/24_Codex/19_STM32_Project/Test/Src/test_i2c_scan.c)

#### 8. I2C scan debug output waits for UART TX completion

Added `BSP_UART_TxWait()` in the test/debug UART send path.

Why this matters:

- avoids scan debug text colliding with other UART traffic during bring-up

### [Test/Src/test_pmic.c](D:/echo.qfq/24_Codex/19_STM32_Project/Test/Src/test_pmic.c)

#### 9. PMIC test output waits for UART TX completion

Added `BSP_UART_TxWait()` before UART transmit in the PMIC test path.

Why this matters:

- same reason as above: reduces UART contention during debug/test output

### [Makefile](D:/echo.qfq/24_Codex/19_STM32_Project/Makefile)

#### 10. `flash` target no longer forces an extra reset step

Changed:

- from: `flash: all reset`
- to: `flash: all`

Why this matters:

- avoided double-reset behavior during flashing
- made board startup behavior more predictable during repeated test cycles

### [Core/Src/main.c](D:/echo.qfq/24_Codex/19_STM32_Project/Core/Src/main.c)

#### 11. LED heartbeat interval changed for debug visibility

The heartbeat interval was adjusted multiple times during debugging.

Final value at the end of this session:

```c
#define HEARTBEAT_INTERVAL_MS   2000U
```

This is only a debug/visibility change and does not affect the protocol behavior.

### [Test/Inc/test_config.h](D:/echo.qfq/24_Codex/19_STM32_Project/Test/Inc/test_config.h)

#### 12. I2C scan debug was enabled during hardware inspection

Used configuration:

```c
#define TEST_PMIC_PID_READ   0
#define TEST_I2C_SCAN        1
```

Observed hardware scan results:

- `I2C1: 0x40`
- `I2C2: 0x6A 0x6B 0x6F 0x7E`
- `I2C3: 0x20 0x68`

This was used to confirm the motor device address path.

## Motor Address Finding

The motor IC was described as `8-bit address = 0xDE`.

Firmware/test usage in this project is `7-bit` addressing, so:

- `0xDE >> 1 = 0x6F`

This matches the I2C scan result and the manually verified working motor address.

## Build Status

Firmware build was re-run successfully after the UART BSP changes.

Successful local build produced:

- `build/firmware.elf`
- `build/firmware.bin`

Latest confirmed successful build in this session reported:

- `text = 12716`
- `data = 16`
- `bss = 3688`

## Key Firmware Conclusion For Claude

The firmware-side protocol implementation is largely functional.

Most remaining instability observed during automated testing is better explained by PC-side test timing / synchronization issues, not by a broad firmware failure.

Most important firmware-side fix from this session:

- baudrate switching is now implemented with a much more conservative USART/DMA reinitialization path, and manual hardware testing confirmed `115200 -> 57600 -> 115200` works.

