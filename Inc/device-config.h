/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __DEVICE_CONFIG__H__
#define __DEVICE_CONFIG__H__

#define CANOKEY_STM32F4_VARIANT

enum hw_variant_t {
  CANOKEY_STM32F4_DIY = 0,
};

/* 无 NFC 芯片：只做 USB 令牌 */
#ifndef NFC_CHIP
#define NFC_CHIP NFC_CHIP_NA
#endif

#define USB_MAX_EP0_SIZE 64

#endif /* __DEVICE_CONFIG__H__ */
