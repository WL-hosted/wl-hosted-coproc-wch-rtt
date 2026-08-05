/* bl_s1: stage-1 bootloader, runs from flash offset 0x00000000.
 *
 * Per part_table.md, bl_s1 owns the first 2K of on-chip flash and hands
 * over to the next stage. The jump target is defined in bl_s1.ld
 * (__jump_target) so the flash layout stays in one place:
 *   - default: bl_s2 @ 448K (0x00070000)
 *   - debug:   app_s1 @ 2K (0x00000800) to bypass bl_s2
 */
    .section .init,"ax",@progbits
    .global _start
    .align  1
_start:
    la      t0, __jump_target
    jr      t0
