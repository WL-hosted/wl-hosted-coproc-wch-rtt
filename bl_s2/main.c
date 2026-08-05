/*
 * bl_s2: stage-2 bootloader (bare-metal, SDK ch32v307_sdk-v2.7).
 *
 * Runs from flash offset 448K (0x00070000) per part_table.md, inside the
 * non-zero-wait flash region. Prints "bl_s2" on the app's console UART
 * (USART1 @ 115200), waits 2s, then jumps to app_s1 at 2K (0x00000800).
 *
 * No interrupts are used: the whole boot path is polled, and global
 * machine interrupts (mstatus.MIE) stay off so the weak default
 * SysTick_Handler (an infinite loop) can never be entered while the
 * SDK Delay_Ms() polls SysTick COUNTFLAG.
 *
 * Console output uses a minimal polled uart_write_str() instead of newlib
 * printf — printf (via _write in debug.c) does not work in this bare-metal
 * -nostartfiles build. Formatting is planned for later via easyLogger.
 */
#include "debug.h"

extern char __app_start; /* defined in bl_s2.ld = 0x00000800 (app_s1) */

/* Polled USART1 string output (same console UART as the app). */
static void uart_write_str(const char *s)
{
    while (*s) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET) { }
        USART_SendData(USART1, *s++);
    }
}

__attribute__((noreturn)) static void jump_to_app(void)
{
    void (*entry)(void) = (void (*)(void))&__app_start;

    /* Hand over with interrupts disabled; the app startup re-inits mstatus. */
    __asm volatile ("csrci mstatus, 0x8");
    entry();
    for (;;) { }
}

int main(void)
{
    __asm volatile ("csrci mstatus, 0x8"); /* keep everything polled */

    Delay_Init();              /* SysTick delay base (144MHz) */
    USART_Printf_Init(115200); /* same console UART as the app: USART1 */
    uart_write_str("bl_s2: app will boot in 2s\r\n");

    Delay_Ms(2000);            /* 2s recovery window before handing over */

    jump_to_app();
    return 0;
}
