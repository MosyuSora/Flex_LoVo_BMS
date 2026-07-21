/*
 * STM32F103C8T6 Bootloader 链接脚本。
 * Flash 只给 Bootloader 划前 4KB(0x08000000..0x08001000);其后是 App 头(0x08001000)
 * 和 App 镜像(0x08001400)——Bootloader 不占、也绝不擦那部分。
 */
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 4K
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);   /* 栈顶 */

SECTIONS
{
    .isr_vector :
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* .data 的加载地址在 FLASH,运行地址在 RAM;Reset_Handler 负责搬运 */
    .data : AT(_etext)
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* 4KB 上限由 MEMORY 的 FLASH LENGTH 强制:超了 ld 会报 region overflowed。 */
}
