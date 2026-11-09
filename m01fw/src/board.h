/* MDP-M01 V1.3 board definitions. See notes/m01-hardware.md. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "stm32f1xx.h"

/* GPIO config nibbles (CNF:MODE) for CRL/CRH */
#define GPIO_IN_ANALOG  0x0u
#define GPIO_IN_FLOAT   0x4u
#define GPIO_IN_PULL    0x8u   /* pull-up/down chosen by ODR bit */
#define GPIO_OUT_PP     0x3u   /* 50 MHz */
#define GPIO_OUT_PP_2M  0x2u
#define GPIO_AF_PP      0xBu   /* 50 MHz */

static inline void gpio_cfg(GPIO_TypeDef *p, unsigned pin, uint32_t cfg)
{
    volatile uint32_t *r = pin < 8 ? &p->CRL : &p->CRH;
    unsigned sh = (pin & 7u) * 4u;
    *r = (*r & ~(0xFu << sh)) | (cfg << sh);
}
static inline void gpio_set(GPIO_TypeDef *p, unsigned pin) { p->BSRR = 1u << pin; }
static inline void gpio_clr(GPIO_TypeDef *p, unsigned pin) { p->BRR = 1u << pin; }
static inline bool gpio_get(GPIO_TypeDef *p, unsigned pin) { return (p->IDR >> pin) & 1u; }

/* LCD (FSMC bank 1 NE1, RS on A18) */
#define LCD_RST_PORT  GPIOB
#define LCD_RST_PIN   1    /* active low */
#define LCD_BL_PORT   GPIOB
#define LCD_BL_PIN    11   /* active high */

/* Keys A..E, active low with pull-up */
#define KEY_PORT GPIOE
/* index 0..4 = A..E -> PE6..PE2 */
#define KEY_PIN(i) (6u - (i))

/* Encoders (pull-up) */
#define ENC_MENU_A_PORT  GPIOC
#define ENC_MENU_A_PIN   15
#define ENC_MENU_B_PORT  GPIOC
#define ENC_MENU_B_PIN   13
#define ENC_VAL_A_PORT   GPIOC
#define ENC_VAL_A_PIN    0
#define ENC_VAL_B_PORT   GPIOC
#define ENC_VAL_B_PIN    1

/* LEDs (active low) and buzzer (active high) */
#define LED_BLUE_PIN 6   /* PB6 */
#define LED_RED_PIN  8   /* PB8 */
#define BUZZER_PIN   9   /* PB9 */

/* system.c */
extern volatile uint32_t sys_ticks;   /* 4 kHz */
extern uint32_t SystemCoreClock;
void system_init(void);
uint32_t millis(void);
void delay_ms(uint32_t ms);
void beep(uint32_t ms);
