/**
  * @file    aw_port_stm32.h
  * @brief   AW ISP 回调表外部接口 (STM32 BSP 实现)
  *
  * 本模块是 aw_uboot_isp.c 与本项目 BSP 之间的适配层。
  * 由 main.c 在初始化阶段通过 aw_isp_init(&g_awOpsStm32) 注册到 vendor ISP 代码。
  */

#ifndef __AW_PORT_STM32_H
#define __AW_PORT_STM32_H

#include "aw_uboot_isp.h"

extern const aw_isp_ops_t g_awOpsStm32;

#endif /* __AW_PORT_STM32_H */
