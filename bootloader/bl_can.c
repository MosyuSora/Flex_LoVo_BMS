/* STM32F103 bxCAN(CAN1)500k,PA11=RX / PA12=TX(默认映射),扩展帧,接收全放行。 */
#include "bl.h"

#define RCC_APB1ENR REG(0x40021000U + 0x1C)
#define RCC_APB2ENR REG(0x40021000U + 0x18)
#define GPIOA_CRH   REG(0x40010800U + 0x04)

#define CAN1 0x40006400U
#define CAN_MCR  REG(CAN1 + 0x000)
#define CAN_MSR  REG(CAN1 + 0x004)
#define CAN_TSR  REG(CAN1 + 0x008)
#define CAN_RF0R REG(CAN1 + 0x00C)
#define CAN_BTR  REG(CAN1 + 0x01C)
#define CAN_TI0R  REG(CAN1 + 0x180)
#define CAN_TDT0R REG(CAN1 + 0x184)
#define CAN_TDL0R REG(CAN1 + 0x188)
#define CAN_TDH0R REG(CAN1 + 0x18C)
#define CAN_RI0R  REG(CAN1 + 0x1B0)
#define CAN_RDT0R REG(CAN1 + 0x1B4)
#define CAN_RDL0R REG(CAN1 + 0x1B8)
#define CAN_RDH0R REG(CAN1 + 0x1BC)
#define CAN_FMR   REG(CAN1 + 0x200)
#define CAN_FM1R  REG(CAN1 + 0x204)
#define CAN_FS1R  REG(CAN1 + 0x20C)
#define CAN_FFA1R REG(CAN1 + 0x214)
#define CAN_FA1R  REG(CAN1 + 0x21C)
#define CAN_F0R1  REG(CAN1 + 0x240)
#define CAN_F0R2  REG(CAN1 + 0x244)

void can_init(void)
{
    RCC_APB2ENR |= (1U << 2) | (1U << 0);  /* IOPAEN + AFIOEN */
    RCC_APB1ENR |= (1U << 25);             /* CAN1EN */

    /* PA11 输入浮空(RX)/ PA12 复用推挽 50MHz(TX);默认映射,无需 remap。 */
    GPIOA_CRH = (GPIOA_CRH & ~((0xFU << 12) | (0xFU << 16))) |
                (0x4U << 12) | (0xBU << 16);

    CAN_MCR &= ~(1U << 1);                  /* 退出 sleep */
    CAN_MCR |= (1U << 0);                   /* INRQ:进 init */
    while ((CAN_MSR & (1U << 0)) == 0U) {   /* 等 INAK */
    }

    /* 500k @ PCLK1=36MHz:BRP=3(prescaler4),BS1=14,BS2=3,SJW=1 → 18tq,采样点 83% */
    CAN_BTR = 0x002D0003U;
    CAN_MCR |= (1U << 6);                   /* ABOM:自动 bus-off 恢复 */

    /* 过滤器 bank0:32 位掩码模式、掩码 0 = 全放行 → FIFO0 */
    CAN_FMR |= (1U << 0);                   /* FINIT */
    CAN_FA1R &= ~(1U << 0);                 /* 先关 bank0 */
    CAN_FS1R |= (1U << 0);                  /* 32 位 */
    CAN_FM1R &= ~(1U << 0);                 /* 掩码模式 */
    CAN_F0R1 = 0U;                          /* id */
    CAN_F0R2 = 0U;                          /* 掩码 0 = 不比较 */
    CAN_FFA1R &= ~(1U << 0);                /* → FIFO0 */
    CAN_FA1R |= (1U << 0);                  /* 激活 bank0 */
    CAN_FMR &= ~(1U << 0);                  /* 退出 filter init */

    CAN_MCR &= ~(1U << 0);                  /* 退出 init */
    while ((CAN_MSR & (1U << 0)) != 0U) {   /* 等 INAK 清 */
    }
}

int can_rx(can_frame_t *f)
{
    if ((CAN_RF0R & 0x3U) == 0U) {
        return 0; /* FIFO0 空 */
    }
    uint32_t rir = CAN_RI0R;

    f->id = (rir & (1U << 2)) ? (rir >> 3) : (rir >> 21); /* 扩展取[31:3],标准取[31:21] */
    f->dlc = (uint8_t)(CAN_RDT0R & 0xFU);
    uint32_t lo = CAN_RDL0R, hi = CAN_RDH0R;

    f->data[0] = (uint8_t)lo; f->data[1] = (uint8_t)(lo >> 8);
    f->data[2] = (uint8_t)(lo >> 16); f->data[3] = (uint8_t)(lo >> 24);
    f->data[4] = (uint8_t)hi; f->data[5] = (uint8_t)(hi >> 8);
    f->data[6] = (uint8_t)(hi >> 16); f->data[7] = (uint8_t)(hi >> 24);

    CAN_RF0R |= (1U << 5); /* RFOM0:释放 */
    return 1;
}

void can_tx(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    while ((CAN_TSR & (1U << 26)) == 0U) {  /* 等邮箱0空(TME0) */
    }
    CAN_TI0R = (id << 3) | (1U << 2);       /* 扩展 ID + 数据帧(不含 TXRQ) */
    CAN_TDT0R = dlc & 0xFU;
    CAN_TDL0R = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    CAN_TDH0R = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
    CAN_TI0R |= (1U << 0);                   /* TXRQ:发 */
}
