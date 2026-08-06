/*
 * bl_s2: stage-2 bootloader (bare-metal, SDK ch32v307_sdk-v2.7).
 *
 * Starts from flash offset 448K (0x00070000) per part_table.md; the startup
 * assembly relocates .text/.rodata/.data to SRAM and main() runs from there.
 * Prints "bl_s2" on the app's console UART (USART1 @ 115200), waits 2s,
 * then jumps to app_s1 at 2K (0x00000800). If the factory RAM_CODE_MOD is
 * not CODE-224KB + RAM-96KB, the option bytes are reprogrammed and the
 * chip reboots after 5s so the new split takes effect.
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

/* Print FLASH_OBR RAM_CODE_MOD[2:0] (bit[9:7]) and its decoded CODE/RAM split. */
static void print_ram_code_mode(void)
{
    /* Index by the full 3-bit field; SDK masks omit bit 7, so mask manually. */
    static const char *const cfg[8] = {
        "CODE-192KB + RAM-128KB", "CODE-192KB + RAM-128KB",
        "CODE-224KB + RAM-96KB",  "CODE-224KB + RAM-96KB",
        "CODE-256KB + RAM-64KB",  "CODE-256KB + RAM-64KB",
        "CODE-128KB + RAM-192KB", "CODE-288KB + RAM-32KB",
    };
    uint32_t mode = (FLASH->OBR >> 7) & 0x7u;
    char bits[4] = {
        '0' + ((mode >> 2) & 1u), '0' + ((mode >> 1) & 1u), '0' + (mode & 1u),
        '\0',
    };

    uart_write_str("bl_s2: RAM_CODE_MOD=0b");
    uart_write_str(bits);
    uart_write_str(" -> ");
    uart_write_str(cfg[mode]);
    uart_write_str("\r\n");
}

/* If the factory RAM_CODE_MOD is not 224K+96K (USER[7:5] = 010), rewrite the
 * option bytes and reboot; OBR only reloads after a system reset, so the
 * updated value is verified by reading the option-byte region directly.
 *
 * Option-byte layout (manual table 32-4, little-endian): the USER byte lives
 * at 0x1FFFF802 inside the halfword 0x1FFFF802 (nUSER:USER). FLASH_
 * ProgramOptionByteData indexes halfwords by (addr-0x1FFFF800)/2, so the
 * address must be 0x1FFFF802, not the byte offset 0x1FFFF801 (nRDPR). */
static void ensure_ram_code_mode(void)
{
    uint32_t mode = (FLASH->OBR >> 7) & 0x7u;
    uint8_t user, new_user;

    if ((mode & 0x6) == 0x2) /* already 01x: CODE-224KB + RAM-96KB */
        return;

    user = (uint8_t)((FLASH->OBR >> 2) & 0xFF); /* USER byte: OBR[9:0] >> 2 */
    new_user = (user & 0x1F) | 0x40;            /* keep [4:0], set [7:5] = 010 */

    uart_write_str("bl_s2: RAM_CODE_MOD != 224K+96K, reprogramming option bytes\r\n");
    FLASH_Unlock();
    if (FLASH_ProgramOptionByteData(0x1FFFF802, new_user) == FLASH_COMPLETE &&
        (*(volatile uint8_t *)0x1FFFF802 & 0xE0) == 0x40)
        uart_write_str("bl_s2: option bytes updated OK, reboot in 5s\r\n");
    else
        uart_write_str("bl_s2: option bytes update FAILED, reboot in 5s\r\n");

    Delay_Ms(5000);
    NVIC_SystemReset();
    for (;;) { }
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
    print_ram_code_mode();
    ensure_ram_code_mode();    /* calibrate RAM_CODE_MOD to 224K+96K if needed */
    uart_write_str("bl_s2: app will boot in 2s\r\n");

    Delay_Ms(2000);            /* 2s recovery window before handing over */

    jump_to_app();
    return 0;
}
