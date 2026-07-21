/*
 * 极简启动:向量表 + Reset_Handler(搬 .data、清 .bss、跳 bl_main)。
 * 不用 CMSIS/HAL,保持 Bootloader 体量最小。
 */
#include <stdint.h>

extern uint32_t _sdata, _edata, _sbss, _ebss, _etext, _estack;
extern int bl_main(void);

void Reset_Handler(void);
static void Default_Handler(void);

/* 只需最小向量表:App 起来后会重定位 VTOR 到自己的表,这里够 Bootloader 自用即可。 */
__attribute__((section(".isr_vector"), used))
void (*const g_vectors[])(void) = {
    (void (*)(void))(&_estack), /* 初始 MSP */
    Reset_Handler,              /* Reset */
    Default_Handler,            /* NMI */
    Default_Handler,            /* HardFault */
    Default_Handler,            /* MemManage */
    Default_Handler,            /* BusFault */
    Default_Handler,            /* UsageFault */
};

void Reset_Handler(void)
{
    /* .data: 从 flash(_etext 之后)搬到 RAM */
    uint32_t *src = &_etext;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }
    /* .bss 清零 */
    for (dst = &_sbss; dst < &_ebss; ++dst) {
        *dst = 0U;
    }
    (void)bl_main();
    for (;;) {
    }
}

static void Default_Handler(void)
{
    for (;;) {
    }
}
