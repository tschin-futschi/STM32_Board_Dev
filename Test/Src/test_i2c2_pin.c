/**
  * @file    test_i2c2_pin.c
  * @brief   I2C2 SCL/SDA 串扰确认：PB10 输出 1 kHz 方波，PB11(SDA) 保持恒低。
  *
  * 用途：确认 SCL 上的高频杂波是否来自 SDA 的窜扰 / 两线间短路。
  *       本版本只翻 PB10，PB11 全程按住为低电平。烧入后用逻辑分析仪看 SCL：
  *         - SCL 变成干净的 1 kHz（杂波消失） -> 杂波确实来自 SDA，
  *           SCL/SDA 之间存在耦合或半短路（硬件缺陷）。
  *         - SCL 仍然乱                        -> PB10 自身另有问题，需再查。
  *
  * 实现说明：
  *   - 推挽（push-pull）全摆幅输出，不依赖外部上拉，识别最清晰；
  *   - 直接操作 GPIO/DWT 寄存器（无对应 BSP API），相关裸寄存器操作仅在本
  *     文件内进行，不修改任何 BSP/App 业务文件；
  *   - 函数永不返回：启用本测试时，板子专职输出方波，不再跑正常业务。
  */

#include "test_config.h"

#if TEST_I2C2_PIN_ID

#include "test_i2c2_pin.h"
#include "stm32f4xx.h"

/*--------------------------------------------------------------------------*/
/*                       被测引脚（独立硬编码以自证）                        */
/*--------------------------------------------------------------------------*/

#define PINID_GPIO_PORT     GPIOB
#define PINID_GPIO_CLK      RCC_AHB1Periph_GPIOB
#define PINID_SCL_PIN       GPIO_Pin_10   /* PB10 -> 输出 1 kHz 方波 */
#define PINID_SDA_PIN       GPIO_Pin_11   /* PB11 -> 全程恒低（不翻转） */

/*--------------------------------------------------------------------------*/
/*                        时基（DWT 周期计数）                              */
/*--------------------------------------------------------------------------*/

#define PINID_SYSCLK_HZ     180000000U
#define PINID_SCL_HZ        1000U         /* PB10 方波频率 */
/* 半周期（cycle 数）= SYSCLK / (2 * freq) */
#define PINID_SCL_HALF_CYC  (PINID_SYSCLK_HZ / (2U * PINID_SCL_HZ))   /* 90000 */

/*--------------------------------------------------------------------------*/

static void PinId_DwtInit(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

void Test_I2C2_PinId_Run(void)
{
    GPIO_InitTypeDef gpioInit;
    uint32_t nextScl;
    uint8_t  sclHigh = 0U;

    /* 1. 使能 GPIO 时钟 */
    RCC_AHB1PeriphClockCmd(PINID_GPIO_CLK, ENABLE);

    /* 2. PB10 / PB11 配置为推挽输出（全摆幅） */
    gpioInit.GPIO_Mode  = GPIO_Mode_OUT;
    gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    gpioInit.GPIO_OType = GPIO_OType_PP;
    gpioInit.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    gpioInit.GPIO_Pin   = PINID_SCL_PIN | PINID_SDA_PIN;
    GPIO_Init(PINID_GPIO_PORT, &gpioInit);

    /* 3. 两脚起始拉低；PB11(SDA) 之后全程保持此低电平不变 */
    PINID_GPIO_PORT->BSRR = (uint32_t)(PINID_SCL_PIN | PINID_SDA_PIN) << 16U;

    /* 4. 启动 DWT 周期计数器 */
    PinId_DwtInit();
    nextScl = DWT->CYCCNT + PINID_SCL_HALF_CYC;

    /* 5. 永不返回：只翻 PB10(SCL)，PB11(SDA) 恒低
     *    （有符号差比较，自动处理 CYCCNT 回绕） */
    for (;;)
    {
        uint32_t now = DWT->CYCCNT;

        if ((int32_t)(now - nextScl) >= 0)
        {
            sclHigh ^= 1U;
            PINID_GPIO_PORT->BSRR = sclHigh ?
                (uint32_t)PINID_SCL_PIN : ((uint32_t)PINID_SCL_PIN << 16U);
            nextScl += PINID_SCL_HALF_CYC;
        }
    }
}

#endif /* TEST_I2C2_PIN_ID */
