// SPDX-License-Identifier: Apache-2.0

#include "device-config.h"
#include "device-stm32.h"
#include "git-rev.h"
#include "main.h"
#include <admin.h>
#include <device.h>
#include <usb_device.h>

#define VENDOR_STACK_TEST 0x03
#define VENDOR_RDP 0x55
#define VENDOR_DFU 0x22

void EnableRDP(uint32_t level);
void EnterDFUBootloader(void);

/* 覆盖 admin.c 里的弱实现 */

int admin_vendor_hw_variant(const CAPDU *capdu, RAPDU *rapdu) {
  (void)capdu;
  const char *s = "CanoKey DIY (STM32F407)";
  size_t len = strlen(s);
  memcpy(RDATA, s, len);
  LL = len;
  if (LL > LE) LL = LE;
  return 0;
}

int admin_vendor_version(const CAPDU *capdu, RAPDU *rapdu) {
  (void)capdu;
  size_t len = strlen(GIT_REV);
  memcpy(RDATA, GIT_REV, len);
  memcpy(RDATA + len, "-O", 2);
  LL = len + 2;
  if (LL > LE) LL = LE;
  return 0;
}

extern uint32_t _stack_boundary;

static int stack_test(const CAPDU *capdu, RAPDU *rapdu) {
  (void)capdu;
  uint8_t ref;
  uint8_t *cur = &ref;
  uint8_t *bot = (uint8_t *)&_stack_boundary;
  DBG_MSG("bottom = %p\r\n", bot);
  if (P2 == 0x55) {
    memset(bot, 0xcc, cur - bot - 0x20);
  } else if (P2 == 0x66) {
    for (uint8_t *p = bot; p < cur; p++)
      if (*p != 0xcc) {
        *(uintptr_t *)RDATA = (uintptr_t)p;
        LL = sizeof(uintptr_t);
        return 0;
      }
    EXCEPT(SW_CHECKING_ERROR);
  }
  return 0;
}

int admin_vendor_specific(const CAPDU *capdu, RAPDU *rapdu) {
  switch (P1) {
  case VENDOR_STACK_TEST:
    return stack_test(capdu, rapdu);

  case VENDOR_RDP:
    DBG_MSG("Enable RDP level %ld\r\n", (long)P2);
    if (P2 == 1)
      EnableRDP(OB_RDP_LEVEL_1);
    else if (P2 == 2)
      EnableRDP(OB_RDP_LEVEL_2);
    else
      EXCEPT(SW_WRONG_P1P2);
    break;

  case VENDOR_DFU:
    if (P2 != VENDOR_DFU) EXCEPT(SW_WRONG_P1P2);
    DBG_MSG("Entering DFU\r\n");
    EnterDFUBootloader();
    ERR_MSG("Failed to enter DFU\r\n");
    for (;;)
      ;

  default:
    EXCEPT(SW_WRONG_P1P2);
  }
  return 0;
}

void EnableRDP(uint32_t level) {
  FLASH_OBProgramInitTypeDef ob;
  HAL_FLASHEx_OBGetConfig(&ob);
  ob.RDPLevel = level;
  ob.OptionType = OPTIONBYTE_RDP;
  HAL_FLASH_Unlock();
  HAL_FLASH_OB_Unlock();
  if (HAL_FLASHEx_OBProgram(&ob) != HAL_OK) ERR_MSG("OBProgram failed\r\n");
  HAL_FLASH_OB_Lock();
  HAL_FLASH_Lock();
}

void EnterDFUBootloader(void) {
  typedef void (*pFunction)(void);
  pFunction JumpToApplication;

  HAL_RCC_DeInit();
  for (int i = 0; i < 8; i++) { /* 关掉并清空所有 NVIC 中断 */
    NVIC->ICER[i] = 0xFFFFFFFF;
    NVIC->ICPR[i] = 0xFFFFFFFF;
  }
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;
  __disable_irq();
  usb_device_deinit();
  HAL_MPU_Disable();
  __DSB();
  __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
  __DSB();
  __ISB();
  __set_MSP(*(__IO uint32_t *)0x1FFF0000);
  JumpToApplication = (pFunction)(*(uint32_t *)(0x1FFF0000 + 4));
  JumpToApplication();
}
