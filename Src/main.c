// SPDX-License-Identifier: Apache-2.0
#include "main.h"
#include "usb_device.h"

#include "device-config.h"
#include "device-stm32.h"
#include "lfs_init.h"
#include <apdu.h>
#include <applets.h>
#include <ccid.h>
#include <device.h>
#include <nfc.h>

RNG_HandleTypeDef hrng;
TIM_HandleTypeDef htim6;
UART_HandleTypeDef huart1;

extern uint32_t _stack_boundary;
uint8_t device_loop_enable;

/* ------------------------------------------------------------------ */
/* random32 由 canokey-crypto 以弱符号提供，这里用硬件 RNG 覆盖它     */
/* ------------------------------------------------------------------ */
uint32_t random32(void) {
  uint32_t v;
  while (HAL_RNG_GenerateRandomNumber(&hrng, &v) != HAL_OK)
    ;
  return v;
}

/* ------------------------------------------------------------------ */
/* 可选：MPU。第一次点亮板子时建议先注释掉 SetupMPU() 的调用，       */
/* 确认 USB 能枚举后再打开，避免配置不匹配导致直接 HardFault。        */
/* ------------------------------------------------------------------ */
int SetupMPU(void) {
  if (MPU->TYPE == 0) return -1;
  int nRegion = (MPU->TYPE >> 8) & 0xFF;
  MPU_Region_InitTypeDef configs[] = {
      {
          .Enable = MPU_REGION_ENABLE,
          .BaseAddress = 0x20000000, /* SRAM1+SRAM2 128KB */
          .Size = MPU_REGION_SIZE_128KB,
          .SubRegionDisable = 0,
          .TypeExtField = MPU_TEX_LEVEL0,
          .AccessPermission = MPU_REGION_FULL_ACCESS,
          .DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE,
          .IsShareable = MPU_ACCESS_SHAREABLE,
          .IsCacheable = MPU_ACCESS_CACHEABLE,
          .IsBufferable = MPU_ACCESS_NOT_BUFFERABLE,
      },
      {
          .Enable = MPU_REGION_ENABLE,
          .BaseAddress = ((uint32_t)&_stack_boundary) - 0x100,
          .Size = MPU_REGION_SIZE_256B, /* 栈溢出保护 */
          .SubRegionDisable = 0,
          .TypeExtField = MPU_TEX_LEVEL0,
          .AccessPermission = MPU_REGION_NO_ACCESS,
          .DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE,
          .IsShareable = MPU_ACCESS_SHAREABLE,
          .IsCacheable = MPU_ACCESS_CACHEABLE,
          .IsBufferable = MPU_ACCESS_NOT_BUFFERABLE,
      },
      {
          .Enable = MPU_REGION_ENABLE,
          .BaseAddress = FLASH_BASE,
          .Size = MPU_REGION_SIZE_512KB,
          .SubRegionDisable = 0,
          .TypeExtField = MPU_TEX_LEVEL0,
          .AccessPermission = MPU_REGION_FULL_ACCESS,
          .DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE,
          .IsShareable = MPU_ACCESS_NOT_SHAREABLE,
          .IsCacheable = MPU_ACCESS_CACHEABLE,
          .IsBufferable = MPU_ACCESS_NOT_BUFFERABLE,
      },
      {
          .Enable = MPU_REGION_ENABLE,
          .BaseAddress = PERIPH_BASE,
          .Size = MPU_REGION_SIZE_512MB,
          .SubRegionDisable = 0,
          .TypeExtField = MPU_TEX_LEVEL0,
          .AccessPermission = MPU_REGION_FULL_ACCESS,
          .DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE,
          .IsShareable = MPU_ACCESS_SHAREABLE,
          .IsCacheable = MPU_ACCESS_NOT_CACHEABLE,
          .IsBufferable = MPU_ACCESS_BUFFERABLE,
      },
  };
  HAL_MPU_Disable();
  for (int i = 0; i < nRegion; i++) {
    if (i < (int)(sizeof(configs) / sizeof(configs[0]))) {
      configs[i].Number = i;
      HAL_MPU_ConfigRegion(configs + i);
    } else {
      MPU->RNR = i;
      MPU->RBAR = 0;
      MPU->RASR = 0;
    }
  }
  SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;
  SCB->SHCSR |= SCB_SHCSR_BUSFAULTENA_Msk;
  NVIC_SetPriority(MemoryManagement_IRQn, -1);
  NVIC_SetPriority(BusFault_IRQn, -1);

  HAL_MPU_Enable(0);
  return 0;
}

/* ------------------------------------------------------------------ */
/* 时钟：HSE -> PLL -> SYSCLK 168MHz，PLLQ = 48MHz 给 USB OTG FS      */
/* 若你的板子是 25MHz 晶振，用 cmake -DHSE_VALUE=25000000 重新配置    */
/* ------------------------------------------------------------------ */
static void SystemClock_Config(void) {
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_ON;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = (HSE_VALUE / 1000000U); /* 8MHz -> 8, 25MHz -> 25，得到 1MHz */
  osc.PLL.PLLN = 336;                    /* VCO = 336MHz */
  osc.PLL.PLLP = RCC_PLLP_DIV2;          /* SYSCLK = 168MHz */
  osc.PLL.PLLQ = 7;                      /* USB/RNG = 48MHz */
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;  /* HCLK  = 168MHz */
  clk.APB1CLKDivider = RCC_HCLK_DIV4;   /* PCLK1 =  42MHz (TIM6 x2 = 84MHz) */
  clk.APB2CLKDivider = RCC_HCLK_DIV2;   /* PCLK2 =  84MHz */
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) Error_Handler();
}

static void MX_RNG_Init(void) {
  __HAL_RCC_RNG_CLK_ENABLE();
  hrng.Instance = RNG;
  if (HAL_RNG_Init(&hrng) != HAL_OK) Error_Handler();
}

#ifdef DEBUG_OUTPUT
static void MX_USART1_UART_Init(void) {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_USART1_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOA, &gpio);

  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
}
#endif

static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /* FK407M1 用户按键 SW2 接 PA15。注意（STM32F4 与 F1 的关键区别）：
   * PA15 复位后默认是 JTAG 的 JTDI（MODER=10，AF0 模式），但 F4 没有 F1 的
   * AFIO_MAPR.SWJ_CFG 寄存器，也不需要"释放 JTAG"——只要像下面这样把 MODER
   * 配成输入，JTAG 复用即被让出，PA15 就是普通 GPIO（SWD 烧录/调试不受影响）。
   * （网上大量"F407 用 __HAL_AFIO_REMAP_SWJ_NOJTAG() 释放 PA15"的文章是照搬
   *  F1 代码，该宏在 F4 HAL 里不存在，编译直接报错。） */

  /* LED */
  gpio.Pin = LED_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &gpio);
  led_off();

  /* 用户确认按键 */
  gpio.Pin = KEY_Pin;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = KEY_ACTIVE_HIGH ? GPIO_PULLDOWN : GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(KEY_GPIO_Port, &gpio);
}

/* USB OTG FS: PA11 = DM, PA12 = DP */
static void MX_USB_GPIO_Init(void) {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF10_OTG_FS;
  HAL_GPIO_Init(GPIOA, &gpio);

  /* TinyUSB 的 dwc2 驱动不碰 STM32 自己的 GCCFG，
   * 这两步必须自己做，否则 PHY 不上电、设备永远枚举不上：
   *   1) PWRDWN 置 1 = 打开内嵌 FS transceiver
   *   2) NOVBUSSENS 置 1 = 关闭 VBUS 检测（纯设备，PA9 未必接到 VBUS）
   */
  __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
  USB_OTG_FS->GCCFG |= USB_OTG_GCCFG_PWRDWN;
  USB_OTG_FS->GCCFG &= ~(USB_OTG_GCCFG_VBUSASEN | USB_OTG_GCCFG_VBUSBSEN);
  USB_OTG_FS->GCCFG |= USB_OTG_GCCFG_NOVBUSSENS;
}

int main(void) {
  /* 双保险：SystemInit() 理论上已经把 VTOR 指到 0x08010000，
   * 这里再显式设一次，必须在 HAL_Init() 启动 SysTick 之前完成，
   * 否则 SysTick 中断会跳到 sector 0 的 boot stub 区里。 */
  SCB->VTOR = APP_BASE;
  __DSB();
  __ISB();

  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_RNG_Init();
#ifdef DEBUG_OUTPUT
  MX_USART1_UART_Init();
#endif
  // SetupMPU();  /* 点亮阶段先关掉，确认可用后再打开 */

  DBG_MSG("CanoKey on STM32F407 starting...\r\n");
  DBG_MSG("Init FS\r\n");
  littlefs_init();

  DBG_MSG("Init applets\r\n");
  applets_install();
  init_apdu_buffer();

  DBG_MSG("Init USB\r\n");
  MX_USB_GPIO_Init();
  HAL_NVIC_SetPriority(OTG_FS_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
  usb_device_init();
  device_loop_enable = 1;

  DBG_MSG("Main Loop\r\n");
  for (;;) {
    device_loop(1);
  }
}

void Error_Handler(void) {
  ERR_MSG("Error_Handler\r\n");
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {
  (void)file;
  (void)line;
}
#endif
