// SPDX-License-Identifier: Apache-2.0
#include "main.h"
#include "stm32f4xx_ll_usart.h"

extern UART_HandleTypeDef DBG_UART;

/* Retargeting functions for gcc-arm-embedded
 *
 * 两个必须有的防护，否则板子会在启动阶段直接"假死"：
 *  1) 串口初始化之前就调用 printf（比如 SystemClock_Config 失败时 Error_Handler
 *     会打印），此时 huart1.Instance 还是 NULL，写 DR 会 HardFault。
 *  2) 等待 TC 标志时死循环：如果 PA9/PA10 没接、或被 USB VBUS 等电路占用，
 *     TC 可能永远不来，整个固件就卡在第一条 printf 上，USB 也就永远枚举不了。
 */

#define TX_TIMEOUT_LOOPS 200000u /* 168MHz 下约 6ms，一个字符只需 ~87us */

static uint8_t uart_alive = 1;

void _ttywrch(int ch) {
  if (!uart_alive) return;
  if (DBG_UART.Instance == NULL) return;
  if (DBG_UART.gState == HAL_UART_STATE_RESET) return; /* 还没初始化过 */

  LL_USART_TransmitData8(DBG_UART.Instance, ch);
  for (uint32_t i = 0; i < TX_TIMEOUT_LOOPS; ++i) {
    if (LL_USART_IsActiveFlag_TC(DBG_UART.Instance)) return;
  }
  /* 串口发不出去，判定为不可用，后续输出直接丢弃，不再拖慢固件 */
  uart_alive = 0;
}

int _write(int fd, char *ptr, int len) {
  (void)fd;
  for (int i = 0; i < len; ++i) _ttywrch(ptr[i]);
  return len;
}

int _read(int fd, char *ptr, int len) {
  (void)fd;
  (void)ptr;
  return len;
}
