# Flash/

电机 / 驱动 IC **本地烧录** (ISP) 代码归档目录。按「厂家 / 芯片型号」两级展开，每个芯片目录下再分 `Inc/` 与 `Src/`。

## 目录结构

```
Flash/
  AW/                     AW (Awinic) 系列
    AW86008_86100/        AW86008 / AW86100 共用 UBOOT 协议
      Inc/
      Src/
  DW/                     (预留) 后续 DW 系列
```

## 集成状态

| 子目录 | 来源 | 状态 |
|--------|------|------|
| `AW/AW86008_86100/` | 厂家参考代码 (Shanghai Awinic, 2019 / 2020) | **未集成 — 未编入 Makefile，不参与构建** |

### `AW/AW86008_86100/` 未集成原因

`Src/aw_uboot_isp.c` 中存在以下未完成项，直接加入 `C_SOURCES` 会导致编译失败：

- `aw_reset_chip()` / `aw_boot_control()` —— 厂家以伪代码形式提供（例如 `0x6b, w, 0xff, 0xa3, 0x02,`），无法直接编译，需要按 STM32 端 I2C API 重写
- `delay_ms()` —— 空 stub，需要接 `BSP_GetTick()`
- `aw_i2c_write()` / `aw_i2c_read()` —— 空 stub，需要转发到 `BSP_I2C2_TransparentWrite` / `BSP_I2C2_TransparentRead`
- `aw_flash_jump_check()` —— 整段被注释，直接 `return ISP_OK`，需要确认厂家约定后补回

## 后续集成路线

详见 `protocol.md` v2.6 修订记录（命令号 `0x32` ~ `0x37` 已预留）与 `CLAUDE.md` 路线图阶段 4.6（待规划）。集成时还需解决：

- 烧录期间 I2C2 总线与采样 / 发生器互斥（与 0x20 / 0x21 / 0x30 / 0x31 一致）
- 流式分包烧录（AW86100 Flash = 128 KB > STM32 SRAM1 = 112 KB，无法整片缓存）
- 厂家代码大栈数组（`w_buff[255]` 等）改为 `static`，匹配本项目 2 KB 栈预算
