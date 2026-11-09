/* Startup for the M01 app at 0x08008000, entered from the stock bootloader. */
#include "board.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);

void Default_Handler(void) { for (;;) { } }

#define WEAK __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK;
void HardFault_Handler(void) WEAK;
void MemManage_Handler(void) WEAK;
void BusFault_Handler(void) WEAK;
void UsageFault_Handler(void) WEAK;
void SVC_Handler(void) WEAK;
void DebugMon_Handler(void) WEAK;
void PendSV_Handler(void) WEAK;
void SysTick_Handler(void) WEAK;
void EXTI0_IRQHandler(void) WEAK;
void EXTI1_IRQHandler(void) WEAK;
void EXTI15_10_IRQHandler(void) WEAK;
void Reset_Handler(void);

/* 16 core vectors + 60 device IRQs (high-density F103 layout); device IRQs
 * all fall through to Default_Handler until a driver claims them. */
#pragma GCC diagnostic ignored "-Woverride-init"   /* per-IRQ entries override the default range */
__attribute__((section(".isr_vector"), used))
void (*const vector_table[16 + 60])(void) = {
    (void (*)(void))&_estack,
    Reset_Handler, NMI_Handler, HardFault_Handler, MemManage_Handler,
    BusFault_Handler, UsageFault_Handler, 0, 0, 0, 0, SVC_Handler,
    DebugMon_Handler, 0, PendSV_Handler, SysTick_Handler,
    [16 ... 75] = Default_Handler,
    [16 + EXTI0_IRQn] = EXTI0_IRQHandler,
    [16 + EXTI1_IRQn] = EXTI1_IRQHandler,
    [16 + EXTI15_10_IRQn] = EXTI15_10_IRQHandler,
};

static void reset_c(void)
{
    /* The bootloader may leave interrupts, SysTick or USB running. */
    SCB->VTOR = 0x08008000u;
    SysTick->CTRL = 0;
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
    RCC->APB1RSTR = RCC_APB1RSTR_USBRST;
    RCC->APB1RSTR = 0;

    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;

    system_init();
    __enable_irq();
    main();
    for (;;) { }
}

__attribute__((naked)) void Reset_Handler(void)
{
    __asm volatile(
        "cpsid i        \n"
        "ldr r0, =_estack\n"
        "mov sp, r0     \n"
        "b reset_c_tramp\n");
}

/* Trampoline with C linkage so the naked asm can branch to it. */
__attribute__((used, noreturn)) void reset_c_tramp(void)
{
    reset_c();
    for (;;) { }
}
