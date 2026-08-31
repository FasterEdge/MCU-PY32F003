// fe_port.c — FasterEdge MCU 平台移植层参考实现（PY32F003 / Cortex-M0+ 版）
// PY32F003 为 32 位 Cortex-M0+ 内核：24MHz、16KB Flash、2KB RAM、无硬件 EEPROM。
// 本文件为寄存器级参考实现：
//   UART  : USART1（RCC + GPIO + USART 寄存器）
//   GPIO  : GPIOA-D（CFGLR/CFGHR/INDR/OUTDR）
//   time  : SysTick（内核定时器）
//   EEPROM: 无硬件，注释留 DataFlash 模拟参考（PY32F003 用户区 1KB）
// 寄存器名/地址以 WCH 官方头文件（ch32v00x.h）为准，如有差异请微调。
#include "fe_port.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================
// 寄存器位/段定义（参考 ch32v00x.h；实际以官方头文件为准）
// ============================================================
// 内核 SysTick
#define SYST_CSR   (*(volatile u32 *)0xE000E010u)
#define SYST_RVR   (*(volatile u32 *)0xE000E014u)
#define SYST_CVR   (*(volatile u32 *)0xE000E018u)
// RCC
#define RCC_APB2PCENR (*(volatile u32 *)0x40021018u)
#define RCC_APB2PERST (*(volatile u32 *)0x4002100Cu)
// GPIOA
#define GPIOA_CFGLR (*(volatile u32 *)0x50000000u)
#define GPIOA_CFGHR (*(volatile u32 *)0x50000004u)
#define GPIOA_INDR  (*(volatile u32 *)0x50000008u)
#define GPIOA_OUTDR (*(volatile u32 *)0x5000000Cu)
#define GPIOA_BSHR  (*(volatile u32 *)0x50000010u)
#define GPIOA_BCR   (*(volatile u32 *)0x50000014u)
// USART1
#define USART1_STATR (*(volatile u32 *)0x40013800u)
#define USART1_DATAR (*(volatile u32 *)0x40013804u)
#define USART1_BRR   (*(volatile u32 *)0x40013808u)
#define USART1_CTLR1 (*(volatile u32 *)0x4001380Cu)
#define USART1_CTLR3 (*(volatile u32 *)0x40013814u)

// ============================================================
// 格式化输出
// ============================================================
int fe_snprintf(char *buf, u16 size, const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    if (n < 0) { buf[0] = 0; return 0; }
    if ((u16)n >= size) buf[size - 1] = 0;
    return n;
}

// ============================================================
// 串口（USART1，PA9=TX / PA10=RX）
// ============================================================
static fe_port_uart_rx_cb_t g_rx_cb = NULL;
static void *g_rx_user = NULL;

void fe_port_uart_init(u8 port, u32 baud, fe_port_uart_rx_cb_t rx_cb, void *user) {
    (void)port;
    g_rx_cb = rx_cb;
    g_rx_user = user;
    // 开启 GPIOA + USART1 时钟
    RCC_APB2PCENR |= (1u << 2) | (1u << 14);   // IOPAEN | USART1EN
    // PA9(输出50MHz复用) / PA10(输入)
    GPIOA_CFGHR &= ~(0xFFu << 4);              // 清 CNF9/MODE9/CNF10/MODE10
    GPIOA_CFGHR |=  (0xBu << 4) | (0x4u << 8); // PA9: AF推挽50M, PA10: 浮空输入
    // USART 配置：115200 @24MHz, 8N1
    USART1_BRR = (baud == 9600)  ? 312u
               : (baud == 57600) ? 52u
               : 26u;                          // 115200 → 13 (24M/16/115200)
    USART1_CTLR1 = (1u << 13) | (1u << 3) | (1u << 2); // UE | TE | RE
}

u16 fe_port_uart_write(u8 port, const u8 *data, u16 len) {
    u16 i;
    (void)port;
    for (i = 0; i < len; i++) {
        while (!(USART1_STATR & (1u << 7)));   // 等 TXE
        USART1_DATAR = data[i];
    }
    return len;
}

u8 fe_port_uart_available(u8 port) {
    (void)port;
    return (USART1_STATR & (1u << 5)) ? 1 : 0; // RXNE
}

int fe_port_uart_read(u8 port) {
    (void)port;
    if (!(USART1_STATR & (1u << 5))) return -1;
    return (int)(USART1_DATAR & 0xFFu);
}

void fe_port_uart_close(u8 port) {
    (void)port;
    USART1_CTLR1 &= ~((1u << 13) | (1u << 3) | (1u << 2)); // 关 UE/TE/RE
}

// ============================================================
// EEPROM（PY32F003 无硬件 EEPROM；用用户区 DataFlash 模拟 1KB）
// 用户区 DataFlash 基址按数据手册填写（16KB Flash 型号常见 0x08003C00），
// 共 1KB，按字节内存映射可读。
// ============================================================
#ifndef FE_DATAFLASH_BASE
#define FE_DATAFLASH_BASE 0x08003C00u
#endif

// 读：用户区按字节内存映射，直接解引用
static u8 dataflash_read(u16 addr) {
    return ((volatile u8 *)FE_DATAFLASH_BASE)[addr];
}

u8 fe_port_eeprom_get_str(u16 addr, char *out, u16 outlen) {
    u16 i;
    if (!out || outlen == 0) return FALSE;
    for (i = 0; i + 1 < outlen; i++) {
        u8 c = dataflash_read((u16)(addr + i));
        out[i] = (char)c;
        if (c == 0) return TRUE;
    }
    out[outlen - 1] = 0;
    return TRUE;
}

u8 fe_port_eeprom_get_u32(u16 addr, u32 *out) {
    u8 i;
    u32 v = 0;
    if (!out) return FALSE;
    for (i = 0; i < 4; i++)
        v |= (u32)dataflash_read((u16)(addr + i)) << (8 * i);
    *out = v;
    return TRUE;
}

// 写：DataFlash 需经厂商 FLASH 控制器解锁 + 扇区擦除 + 编程。
// 本仓库为寄存器级参考实现，未内嵌厂商固件库；接入厂商库时在此实现：
//   REQUIRED_PORT_HOOK(dataflash_write)：
//     FLASH_Unlock();
//     FLASH_Erase...(FE_DATAFLASH_BASE + addr);
//     FLASH_Program...(FE_DATAFLASH_BASE + addr, val);
//     FLASH_Lock();
u8 fe_port_eeprom_set_str(u16 addr, const char *value) {
    (void)addr; (void)value;
    return FALSE;   // REQUIRED_PORT_HOOK：接入厂商 DataFlash 写序列后返回 TRUE
}

u8 fe_port_eeprom_set_u32(u16 addr, u32 value) {
    (void)addr; (void)value;
    return FALSE;   // REQUIRED_PORT_HOOK：同 set_str，写 4 字节（小端）
}

// ============================================================
// 系统时间（SysTick 1ms）
// ============================================================
static volatile u32 s_tick_ms = 0;

void fe_port_timer0_init(void) {
    // SysTick: 24MHz / 1000 = 24000 周期
    SYST_RVR = 24000u - 1u;
    SYST_CVR = 0;
    SYST_CSR = (1u << 2) | (1u << 0);          // HCLK | ENABLE
}

u32 fe_port_time_now(void) {
    return s_tick_ms / 1000UL;
}

void fe_port_time_set(u32 epoch) {
    // 基准秒 = epoch - now，内部 s_tick_ms 只计毫秒差值，见注释：
    // 简化实现：以 SysTick 中断为基准，需在中断中 s_tick_ms++。
    (void)epoch;
}

// ============================================================
// 随机数（LCG）
// ============================================================
static u32 s_seed = 0x2F6E2B1u;

void fe_port_random_fill(u8 *buf, u16 len) {
    u16 i;
    for (i = 0; i < len; i++) {
        s_seed = s_seed * 1664525u + 1013904223u;
        buf[i] = (u8)(s_seed >> 24);
    }
}

// ============================================================
// GPIO（引脚 0-19 → GPIOA/GPIOB/GPIOC 映射，参考 Arduino 引脚表）
// ============================================================
static u8 gpio_pin_reg(u8 pin, u32 *base, u32 *bit) {
    // 简化映射：pin 0-7 → PA0-7, 8-15 → PB0-7, 16-19 → PC0-3
    if (pin < 8)  { *base = 0x50000000u; *bit = pin; }
    else if (pin < 16) { *base = 0x50000400u; *bit = pin - 8; }
    else { *base = 0x50000800u; *bit = pin - 16; }
    return 1;
}

int fe_port_gpio_set_mode(u8 pin, const char *mode) {
    u32 base, bit;
    u32 *cfg, outdr;
    u8 is_output = 0, pullup = 0;
    if (!gpio_pin_reg(pin, &base, &bit)) return -1;
    if (strcmp(mode, "output") == 0) is_output = 1;
    else if (strcmp(mode, "input_pullup") == 0) pullup = 1;
    else if (strcmp(mode, "input") == 0) { /* 默认浮空 */ }
    else return -1;
    // 使能端口时钟（简化：直接置位）
    RCC_APB2PCENR |= (1u << 2);                // IOPAEN（GPIOB/C 同理）
    cfg = (u32 *)(base + (bit < 8 ? 0x00u : 0x04u));
    outdr = *(u32 *)(base + 0x0Cu);
    if (is_output) {
        *cfg = (*cfg & ~(0xFu << ((bit & 7) * 4))) | (0x3u << ((bit & 7) * 4)); // 推挽输出2M
    } else {
        *cfg = (*cfg & ~(0xFu << ((bit & 7) * 4))) | (0x4u << ((bit & 7) * 4)); // 浮空输入
    }
    if (pullup && !is_output) {
        outdr |= (1u << bit);
        *(u32 *)(base + 0x0Cu) = outdr;
    }
    return 0;
}

int fe_port_gpio_write(u8 pin, u8 level) {
    u32 base, bit;
    if (!gpio_pin_reg(pin, &base, &bit)) return -1;
    if (level) *(u32 *)(base + 0x10u) = (1u << bit);   // BSHR
    else       *(u32 *)(base + 0x14u) = (1u << bit);   // BCR
    return 0;
}

int fe_port_gpio_read(u8 pin) {
    u32 base, bit;
    if (!gpio_pin_reg(pin, &base, &bit)) return -1;
    return (*(u32 *)(base + 0x08u) & (1u << bit)) ? 1 : 0;  // INDR
}

// ============================================================
// 芯片信息
// ============================================================
void fe_port_chip_info(char *out, u16 outlen) {
    fe_snprintf(out, outlen,
                "{\"chip\":\"PY32F003\",\"arch\":\"Cortex-M0+\","
                "\"ramBytes\":2048,\"flashBytes\":16384,\"eepromBytes\":1024,"
                "\"freqMHz\":24}");
}

// ============================================================
// 延时（软件循环，24MHz 约 1us/次）
// ============================================================
void fe_port_delay_ms(u32 ms) {
    volatile u32 i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 12000u; j++) ;
}
