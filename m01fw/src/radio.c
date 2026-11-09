/* Si24R1 radio on SPI1 (remapped): SCK PB3, MISO PB4, MOSI PB5, CSN PA15, CE PD2, IRQ PD6.
 * Register settings mirror the stock M01 firmware (v2.03 @0x08024F0A..0x08024FA4). */
#include <string.h>
#include "board.h"
#include "radio.h"

#define CSN_L() (GPIOA->BRR = 1u << 15)
#define CSN_H() (GPIOA->BSRR = 1u << 15)
#define CE_L()  (GPIOD->BRR = 1u << 2)
#define CE_H()  (GPIOD->BSRR = 1u << 2)

enum { CONFIG = 0x00, EN_AA = 0x01, EN_RXADDR = 0x02, SETUP_AW = 0x03, SETUP_RETR = 0x04,
       RF_CH = 0x05, RF_SETUP = 0x06, STATUS = 0x07, RX_ADDR_P0 = 0x0A, TX_ADDR = 0x10,
       RX_PW_P0 = 0x11, FIFO_STATUS = 0x17, DYNPD = 0x1C, FEATURE = 0x1D };
enum { R_REG = 0x00, W_REG = 0x20, R_RX_PAYLOAD = 0x61, W_TX_PAYLOAD = 0xA0,
       FLUSH_TX = 0xE1, FLUSH_RX = 0xE2, NOP = 0xFF };
enum { RX_DR = 0x40, TX_DS = 0x20, MAX_RT = 0x10 };
#define CFG_TX 0x0E   /* PWR_UP | CRC 2 bytes, PRIM_TX */
#define CFG_RX 0x0F   /* same + PRIM_RX */

static void udelay(uint32_t us)
{
    uint32_t start = DWT->CYCCNT, n = us * (SystemCoreClock / 1000000u);
    while (DWT->CYCCNT - start < n) { }
}

static uint8_t xfer(uint8_t b)
{
    while (!(SPI1->SR & SPI_SR_TXE)) { }
    *(volatile uint8_t *)&SPI1->DR = b;
    while (!(SPI1->SR & SPI_SR_RXNE)) { }
    return (uint8_t)SPI1->DR;
}

static uint8_t cmd(uint8_t c)
{
    CSN_L(); uint8_t s = xfer(c); CSN_H();
    return s;
}

uint8_t radio_read_reg(uint8_t reg)
{
    CSN_L(); xfer(R_REG | reg); uint8_t v = xfer(NOP); CSN_H();
    return v;
}

static void wreg(uint8_t reg, uint8_t v)
{
    CSN_L(); xfer(W_REG | reg); xfer(v); CSN_H();
}

static void wbuf(uint8_t c, const uint8_t *p, unsigned n)
{
    CSN_L(); xfer(c); while (n--) xfer(*p++); CSN_H();
}

static void rbuf(uint8_t c, uint8_t *p, unsigned n)
{
    CSN_L(); xfer(c); while (n--) *p++ = xfer(NOP); CSN_H();
}

void radio_set_channel(uint8_t ch) { wreg(RF_CH, ch & 0x7F); }

void radio_set_address(const uint8_t a[5])
{
    wbuf(W_REG | TX_ADDR, a, 5);
    wbuf(W_REG | RX_ADDR_P0, a, 5);   /* pipe 0 receives the ACKs and the replies */
}

bool radio_init(uint8_t ch)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    /* SPI1 remap; SWJ_CFG reads back undefined, so always write JTAG-off/SWD-on */
    AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE | AFIO_MAPR_SPI1_REMAP;

    CSN_H(); CE_L();
    gpio_cfg(GPIOA, 15, GPIO_OUT_PP);
    gpio_cfg(GPIOD, 2, GPIO_OUT_PP);
    gpio_cfg(GPIOB, 3, GPIO_AF_PP);
    gpio_cfg(GPIOB, 5, GPIO_AF_PP);
    gpio_set(GPIOB, 4); gpio_cfg(GPIOB, 4, GPIO_IN_PULL);
    gpio_set(GPIOD, 6); gpio_cfg(GPIOD, 6, GPIO_IN_PULL);

    /* master, mode 0, 72 MHz / 16 = 4.5 MHz (Si24R1 max 10 MHz), software NSS */
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_BR_0 | SPI_CR1_BR_1 | SPI_CR1_SSM | SPI_CR1_SSI;
    SPI1->CR1 |= SPI_CR1_SPE;

    delay_ms(5);
    wreg(CONFIG, 0x0C);          /* power down while configuring */
    wreg(EN_AA, 0x01);
    wreg(EN_RXADDR, 0x01);
    wreg(SETUP_AW, 0x03);        /* 5-byte addresses */
    wreg(SETUP_RETR, 0x1A);      /* 500 us, 10 retries */
    wreg(RF_SETUP, 0x0F);        /* 2 Mbps, max power */
    wreg(RX_PW_P0, RADIO_PAYLOAD);
    wreg(DYNPD, 0);
    wreg(FEATURE, 0);
    radio_set_channel(ch);
    wreg(STATUS, RX_DR | TX_DS | MAX_RT);
    cmd(FLUSH_TX); cmd(FLUSH_RX);
    wreg(CONFIG, CFG_TX);
    delay_ms(2);                 /* power-up */

    return radio_read_reg(RF_CH) == (ch & 0x7F) && radio_read_reg(SETUP_RETR) == 0x1A;
}

void radio_idle(void) { CE_L(); }

bool radio_tx(const uint8_t p[RADIO_PAYLOAD])
{
    CE_L();
    wreg(CONFIG, CFG_TX);
    wreg(STATUS, RX_DR | TX_DS | MAX_RT);
    cmd(FLUSH_TX);
    wbuf(W_TX_PAYLOAD, p, RADIO_PAYLOAD);
    udelay(150);                 /* RX->TX settling */
    CE_H(); udelay(15); CE_L();

    uint32_t t0 = millis();
    uint8_t s;
    do { s = cmd(NOP); } while (!(s & (TX_DS | MAX_RT)) && millis() - t0 < 20);
    wreg(STATUS, TX_DS | MAX_RT);
    if (!(s & TX_DS)) cmd(FLUSH_TX);
    return (s & TX_DS) != 0;
}

bool radio_rx(uint8_t p[RADIO_PAYLOAD], uint32_t timeout_ms)
{
    CE_L();
    wreg(CONFIG, CFG_RX);
    wreg(STATUS, RX_DR | TX_DS | MAX_RT);
    cmd(FLUSH_RX);
    CE_H();
    uint32_t t0 = millis();
    bool got = false;
    while (millis() - t0 < timeout_ms) {
        if (cmd(NOP) & RX_DR) { got = true; break; }
    }
    CE_L();
    if (got) {
        rbuf(R_RX_PAYLOAD, p, RADIO_PAYLOAD);
        wreg(STATUS, RX_DR);
    }
    return got;
}
