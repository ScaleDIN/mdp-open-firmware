/* Clocks, GPIO base setup, SysTick (4 kHz) and buzzer. */
#include "board.h"
#include "input.h"

uint32_t SystemCoreClock = 8000000u;
volatile uint32_t sys_ticks;
static volatile uint32_t beep_ticks;

static void clock_init(void)
{
    /* Run from HSI while reconfiguring: the bootloader may have left the PLL on. */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) { }
    RCC->CFGR &= ~RCC_CFGR_SW;
    while (RCC->CFGR & RCC_CFGR_SWS) { }
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY) { }
    RCC->CIR = 0x009F0000u;   /* clear ready flags, no clock interrupts */

    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    RCC->CR |= RCC_CR_HSEON;
    uint32_t t = 200000;
    while (!(RCC->CR & RCC_CR_HSERDY) && --t) { }

    if (t) {   /* 8 MHz crystal x9 = 72 MHz, APB1 36 MHz, USB 72/1.5 = 48 MHz */
        RCC->CFGR = RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9 | RCC_CFGR_PPRE1_DIV2;
        SystemCoreClock = 72000000u;
    } else {   /* no crystal: HSI/2 x16 = 64 MHz */
        RCC->CR &= ~RCC_CR_HSEON;
        RCC->CFGR = RCC_CFGR_PLLMULL16 | RCC_CFGR_PPRE1_DIV2;
        SystemCoreClock = 64000000u;
    }
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }
}

void system_init(void)
{
    clock_init();

    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN |
                    RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPDEN | RCC_APB2ENR_IOPEEN;

    /* JTAG off, SWD ON (SWJ_CFG=010). Frees PB3/PB4/PA15 for the radio.
     * NEVER select 100: SWD shares the USB D+/D- lines and is our only
     * debug access without opening the case. */
    AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;

    /* LEDs off (active low), buzzer off */
    gpio_set(GPIOB, LED_BLUE_PIN);
    gpio_set(GPIOB, LED_RED_PIN);
    gpio_clr(GPIOB, BUZZER_PIN);
    gpio_cfg(GPIOB, LED_BLUE_PIN, GPIO_OUT_PP_2M);
    gpio_cfg(GPIOB, LED_RED_PIN, GPIO_OUT_PP_2M);
    gpio_cfg(GPIOB, BUZZER_PIN, GPIO_OUT_PP_2M);

    input_init();

    SysTick_Config(SystemCoreClock / 4000u);
    NVIC_SetPriority(SysTick_IRQn, 2);
}

void SysTick_Handler(void)
{
    sys_ticks++;
    input_sample();
    if (beep_ticks) {
        beep_ticks--;
        if (beep_ticks) GPIOB->ODR ^= 1u << BUZZER_PIN;   /* 2 kHz square */
        else gpio_clr(GPIOB, BUZZER_PIN);
    }
}

uint32_t millis(void) { return sys_ticks / 4u; }

void delay_ms(uint32_t ms)
{
    uint32_t start = sys_ticks;
    while (sys_ticks - start < ms * 4u) { }
}

void beep(uint32_t ms) { beep_ticks = ms * 4u; }
