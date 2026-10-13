// SPDX-License-Identifier: Apache-2.0
#include "stm32f4xx_it.h"
#include "device-stm32.h"
#include "main.h"
#include <device.h>
#include <tusb.h>

extern TIM_HandleTypeDef htim6;
extern uint8_t device_loop_enable;

void NMI_Handler(void) {
  while (1) {
  }
}

void HardFault_Handler(void) {
  ERR_MSG("HardFault\r\n");
  while (1) {
  }
}

void MemManage_Handler(void) {
  ERR_MSG("MemManage\r\n");
  while (1) {
  }
}

void BusFault_Handler(void) {
  ERR_MSG("BusFault\r\n");
  while (1) {
  }
}

void UsageFault_Handler(void) {
  while (1) {
  }
}

void SVC_Handler(void) {}

void DebugMon_Handler(void) {}

void PendSV_Handler(void) {}

void SysTick_Handler(void) {
  HAL_IncTick();
  if (device_loop_enable) {
    static uint32_t cnt = 0;
    cnt++;
    if (cnt % 10 == 0) { /* 每 10ms 扫一次按键 + 刷新 LED */
      device_periodic_task();
    }
  }
}

void TIM6_DAC_IRQHandler(void) { HAL_TIM_IRQHandler(&htim6); }

/* TinyUSB 只提供 dcd_int_handler()，ISR 名字要自己接上 */
void OTG_FS_IRQHandler(void) { tud_int_handler(0); }

void EXTI0_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0); }
