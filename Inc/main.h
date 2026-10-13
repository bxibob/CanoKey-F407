/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include "common.h"

/* ---------------------------------------------------------------------------
 * 板级配置：FK407M1（反客科技，V1.0 原理图实测）
 *
 * 原理图证据（E:\aliyunpanDownload\FK407M1原理图.pdf）：
 *   LED1（黄绿光）阳极经 R6(360Ω) 接 3.3V，阴极接 PC13 → PC13 低电平点亮
 *   用户按键 SW2：一端网络标签 "KEY PA15"，另一端直接接 GND，无外部上拉
 *                → PA15 低电平有效，靠 STM32 内部上拉（KEY_ACTIVE_HIGH=0
 *                  时 MX_GPIO_Init 自动配 GPIO_PULLUP）
 *   注意 PA15 = JTAG 的 JTDI：复位后 MODER 默认是 AF 模式，但 STM32F4 没有
 *   F1 的 AFIO_MAPR.SWJ_CFG 寄存器、也不需要"释放 JTAG"——MX_GPIO_Init 把
 *   MODER 配成输入即让出 JTAG 复用，PA15 就是普通 GPIO（gdb 实测：拉低/拉高
 *   均能正常读出）。SWD 烧录/调试不受影响。
 *
 * 其他板子参考：
 *   正点原子 探索者/精英 (F407ZGT6): LED0=PF9 LED1=PF10, KEY0=PE4(按下低)
 *                                     KEY1=PE3 KEY2=PE2 WKUP=PA0(按下高)
 *   野火 霸道/指南者     (F407ZGT6): LED=PB0/PB1, KEY=PA0(按下高)/PC13
 *   USB 固定用 PA11/PA12(OTG FS)
 * ------------------------------------------------------------------------- */

/* --- LED：CanoKey 用闪烁提示"需要触摸确认" --- */
#define LED_GPIO_Port GPIOC
#define LED_Pin GPIO_PIN_13
#define LED_ACTIVE_HIGH 0 /* 阳极经限流电阻接 3.3V、阴极接 IO 时填 0（共阳接法） */

/* --- 用户确认按键：短按=确认，长按(>500ms)=长触摸 --- */
#define KEY_GPIO_Port GPIOA
#define KEY_Pin GPIO_PIN_15
#define KEY_ACTIVE_HIGH 0 /* 按下接地(KEY0 类)填 0；按下为高(WKUP 类)填 1 */

/* --- 调试串口（可选）：USART1 TX=PA9 RX=PA10，115200 8N1 --- */
#define DBG_UART huart1

/* ------------------------------------------------------------------------ */

#define APP_BASE 0x08000000U /* 应用程序链接地址，和 .ld 保持一致 */

void Error_Handler(void);

extern const char *fw_git_version;

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
