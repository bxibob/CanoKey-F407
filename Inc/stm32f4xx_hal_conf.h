/* SPDX-License-Identifier: Apache-2.0 */
/* 精简版 HAL 配置：只打开 CanoKey 用到的外设 */
#ifndef __STM32F4xx_HAL_CONF_H
#define __STM32F4xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_MODULE_ENABLED

/* 用到的模块 */
#define HAL_RCC_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_RNG_MODULE_ENABLED
/* TIM/UART 的头文件里无条件引用 DMA_HandleTypeDef，必须一并打开 DMA */
#define HAL_DMA_MODULE_ENABLED

/* 时钟源参数（HSE_VALUE 由 CMake 的 -DHSE_VALUE= 传入） */
#ifndef HSE_VALUE
#define HSE_VALUE 8000000U
#endif
#ifndef HSE_STARTUP_TIMEOUT
#define HSE_STARTUP_TIMEOUT 100U
#endif
#ifndef HSI_VALUE
#define HSI_VALUE 16000000U
#endif
#ifndef LSE_STARTUP_TIMEOUT
#define LSE_STARTUP_TIMEOUT 5000U
#endif
#ifndef LSI_VALUE
#define LSI_VALUE 32000U
#endif
/* 外部方波时钟源（非晶振）时的频率，HAL_RCCEx_GetPeriphCLKFreq 会引用 */
#ifndef EXTERNAL_CLOCK_VALUE
#define EXTERNAL_CLOCK_VALUE 12288000U
#endif

#define VDD_VALUE 3300U
#define TICK_INT_PRIORITY 0x0FU

#define USE_RTOS 0
#define PREFETCH_ENABLE 1U
#define INSTRUCTION_CACHE_ENABLE 1U
#define DATA_CACHE_ENABLE 1U

/* 断言：默认关闭；需要时编译时加 -DUSE_FULL_ASSERT 并提供 assert_failed */
#ifdef USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
extern void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif

/* 以太网/DMA/CAN 等未使用，不打开 */

#include "stm32f4xx_hal_rcc.h"
#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_pwr.h"
#include "stm32f4xx_hal_cortex.h"
#include "stm32f4xx_hal_dma.h"
#include "stm32f4xx_hal_dma_ex.h"
#include "stm32f4xx_hal_tim.h"
#include "stm32f4xx_hal_tim_ex.h"
#include "stm32f4xx_hal_uart.h"
#include "stm32f4xx_hal_rng.h"

#ifdef __cplusplus
}
#endif

#endif /* __STM32F4xx_HAL_CONF_H */
