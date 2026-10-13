// SPDX-License-Identifier: Apache-2.0
/* STM32F407 硬件适配层：覆盖 canokey-core/src/device.c 中的弱函数 */
#include "device-config.h"
#include "main.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_tim.h"
#include "stm32f4xx_ll_usart.h"
#include <admin.h>
#include <device.h>
#include <usb_device.h>

#define TOUCH_GAP_TIME TOUCH_EXPIRE_TIME /* 两次触摸事件之间的间隔(ms) */
#define MIN_LONG_TOUCH_TIME 500
#define MIN_TOUCH_TIME 20

extern TIM_HandleTypeDef htim6;
extern UART_HandleTypeDef DBG_UART;

static void (*tim_callback)(void);

/* ------------------------------------------------------------------ */
/* 基础                                                                */
/* ------------------------------------------------------------------ */
void device_delay(int ms) { HAL_Delay(ms); }

uint32_t device_get_tick(void) { return HAL_GetTick(); }

void device_set_timeout(void (*callback)(void), uint16_t timeout) {
  const uint32_t prescaler = 4000;
  /* APB1 分频不为 1 时定时器时钟 = PCLK1 x 2 (F4: 42MHz x 2 = 84MHz) */
  uint32_t counting_freq =
      HAL_RCC_GetPCLK1Freq() * (LL_RCC_GetAPB1Prescaler() == LL_RCC_APB1_DIV_1 ? 1 : 2) / prescaler;

  if (timeout == 0) {
    HAL_TIM_Base_Stop_IT(&htim6);
    return;
  }
  tim_callback = callback;
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = prescaler - 1;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = counting_freq / 1000 * timeout - 1;
  if (htim6.Init.Period > 65535) {
    ERR_MSG("TIM6 period %u overflow!\n", (unsigned)htim6.Init.Period);
    htim6.Init.Period = 65535;
  }
  htim6.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK) Error_Handler();
  LL_TIM_ClearFlag_UPDATE(htim6.Instance);
  LL_TIM_SetCounter(htim6.Instance, 0);
  HAL_TIM_Base_Start_IT(&htim6);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  if (htim == &htim6) {
    HAL_TIM_Base_Stop_IT(&htim6);
    if (tim_callback) tim_callback();
  }
}

/* ------------------------------------------------------------------ */
/* LED                                                                 */
/* ------------------------------------------------------------------ */
void led_on(void) {
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, LED_ACTIVE_HIGH ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void led_off(void) {
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, LED_ACTIVE_HIGH ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* ------------------------------------------------------------------ */
/* 用户确认：用板载按键代替电容触摸                                    */
/* ------------------------------------------------------------------ */
static uint8_t key_pressed(void) {
  GPIO_PinState idle = KEY_ACTIVE_HIGH ? GPIO_PIN_RESET : GPIO_PIN_SET;
  return HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) != idle;
}

/* 电容触摸校准在按键方案下不需要 */
void GPIO_Touch_Calibrate(void) {}

#ifdef DIAG
/* 诊断模式用的事件位与全局。core 里的 ctap_hid_set_report_cb / wait_for_user_presence
 * 会在关键里程碑调用 diag_event()，board 这边负责记录并在 LED 上编码出来。 */
#define DIAG_OUT      (1u << 0)   // HID OUT 帧经中断 OUT 端点到达 (report_type==INVALID)
#define DIAG_WAIT     (1u << 1)   // 进入 wait_for_user_presence(=命令已收并解析成功)
#define DIAG_OUT_CTRL (1u << 2)   // HID OUT 帧经 SET_REPORT 控制传输到达 (report_type==OUTPUT)
#define DIAG_MOUNT    (1u << 3)   // USB 枚举完成(挂载)
uint32_t diag_events = 0;
void diag_event(uint8_t e) { diag_events |= e; }

/* 把 diag_events 编码成 LED 闪烁，便于在无调试器的 PC 上判断卡在哪一步：
 *   - 触摸被识别(PA15 按下)        -> 常亮
 *   - 进入 wait_for_user_presence -> 快闪(100ms)：命令已收到并解析，正在等触摸
 *   - OUT 帧经中断 OUT 端点到达     -> 中闪(300ms)：Windows 走 HID 中断 OUT 端点
 *   - OUT 帧经 SET_REPORT 控制传输  -> 慢闪(600ms)：Windows 走 SET_REPORT 控制传输
 *   - 仅枚举完成、HID 从未收到帧    -> 极慢闪(1000ms)：Windows 没把命令发给本设备
 *   - 以上都无                    -> 不亮
 * 优先级：触摸 > WAIT > OUT > OUT_CTRL > MOUNT
 */
static void diag_led_update(void) {
  static uint32_t last = 0;
  static uint8_t on = 0;
  uint32_t tick = HAL_GetTick();
  uint32_t interval;

  if (get_touch_result() != TOUCH_NO) { led_on(); return; }
  if (diag_events & DIAG_WAIT)           interval = 100;
  else if (diag_events & DIAG_OUT)       interval = 300;
  else if (diag_events & DIAG_OUT_CTRL)  interval = 600;
  else if (diag_events & DIAG_MOUNT)     interval = 1000;
  else { led_off(); return; }

  if ((uint32_t)(tick - last) >= interval) {
    last = tick;
    on = on ? 0 : 1;
    if (on) led_on(); else led_off();
  }
}
#endif /* DIAG */

/* 正常触摸状态机（DIAG 模式也复用，保证真实按键仍能触发触摸） */
static void run_touch_fsm(void) {
  enum {
    TOUCH_STATE_IDLE,
    TOUCH_STATE_DOWN,
    TOUCH_STATE_ASSERT,
    TOUCH_STATE_DEASSERT,
  };
  static uint32_t event_tick, fsm = TOUCH_STATE_IDLE;
  uint32_t tick = HAL_GetTick();

  switch (fsm) {
  case TOUCH_STATE_IDLE:
#ifdef DEBUG_OUTPUT
    /* 调试：串口收到 T / L 等价于短触摸 / 长触摸 */
    if (LL_USART_IsActiveFlag_RXNE(DBG_UART.Instance)) {
      int data = LL_USART_ReceiveData8(DBG_UART.Instance);
      if ('T' == data || 'L' == data) {
        set_touch_result('T' == data ? TOUCH_SHORT : TOUCH_LONG);
        fsm = TOUCH_STATE_ASSERT;
        event_tick = tick;
      }
    }
#endif
    if (key_pressed()) {
      fsm = TOUCH_STATE_DOWN;
      event_tick = tick;
    }
    break;

  case TOUCH_STATE_DOWN:
    if (!key_pressed() || tick - event_tick > MIN_LONG_TOUCH_TIME) {
      if (tick - event_tick >= MIN_TOUCH_TIME) {
        set_touch_result(tick - event_tick > MIN_LONG_TOUCH_TIME ? TOUCH_LONG : TOUCH_SHORT);
        fsm = TOUCH_STATE_ASSERT;
        event_tick = tick;
      } else {
        fsm = TOUCH_STATE_IDLE; /* 抖动，忽略 */
      }
    }
    break;

  case TOUCH_STATE_ASSERT:
    if (tick - event_tick >= TOUCH_GAP_TIME) {
      set_touch_result(TOUCH_NO);
      fsm = TOUCH_STATE_DEASSERT;
    }
    break;

  case TOUCH_STATE_DEASSERT:
    if (!key_pressed()) fsm = TOUCH_STATE_IDLE;
    break;

  default:
    break;
  }

  device_update_led();
}

void device_periodic_task(void) {
#ifdef BTN_TEST
  /* 诊断模式：PC13 LED 直接镜像 PA15 电平（按钮按下=PA15 低=LED 亮）。
   * 用来确认物理按键到底有没有把 PA15 拉到地，与 FSM/触摸逻辑无关。 */
  if (key_pressed())
    led_on();
  else
    led_off();
  return;
#endif

#ifdef DIAG
  /* 诊断模式：照常跑 FSM（真实按键仍可触发触摸），再把里程碑编码到 LED */
  run_touch_fsm();
  diag_led_update();
  return;
#endif

  run_touch_fsm();
}

/* ------------------------------------------------------------------ */
/* 硬件版本：DIY 板只有一种                                            */
/* ------------------------------------------------------------------ */
uint8_t stm32_hw_variant_probe(void) { return CANOKEY_STM32F4_DIY; }

/* ------------------------------------------------------------------ */
/* 原子操作 / 自旋锁                                                   */
/* ------------------------------------------------------------------ */
int device_atomic_compare_and_swap(volatile uint32_t *var, uint32_t expect, uint32_t update) {
  int status = 0;
  do {
    if (__LDREXW(var) != expect) return -1;
    status = __STREXW(update, var);
  } while (status != 0);
  __DMB();
  return 0;
}

int device_spinlock_lock(volatile uint32_t *lock, uint32_t blocking) {
  int status = 0;
  do {
    while (__LDREXW(lock) != 0)
      if (!blocking)
        return -1;
    status = __STREXW(1, lock);
  } while (status != 0);
  __DMB();
  return 0;
}

void device_spinlock_unlock(volatile uint32_t *lock) {
  __DMB();
  *lock = 0;
}
