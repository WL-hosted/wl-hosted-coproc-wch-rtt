/* bl_s1 linker script.
 *
 * Flash layout per flash_table.md:
 *   bl_s1  onchip 0     2K   jump to next stage
 *   bl_s2  onchip 448K  32K  actual bootloader
 *
 * The stage-1 image is pure assembly (a single jump), so it needs no
 * RAM sections or stack.
 */
ENTRY( _start )

/* Jump target used by bl_s1.S via `la t0, __jump_target`.
 * Normal: bl_s2 @ 448K (flash_table.md). Debug: app_s1 @ 2K to bypass bl_s2. */
__jump_target = 0x00070000;   /* bl_s2 @ 448K (flash_table.md) */

MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 2K
}

SECTIONS
{
    .init :
    {
        KEEP(*(SORT_NONE(.init)))
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH
}
