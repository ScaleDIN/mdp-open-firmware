#include <string.h>
#include "board.h"
#include "config.h"

#define CONFIG_ADDR  0x08040000u   /* first 2 KB page above 256 KB, unused by stock */
#define CONFIG_MAGIC 0x434C304Du   /* 'M0LC' */

bool config_load(link_cfg_t *c)
{
    memcpy(c, (const void *)CONFIG_ADDR, sizeof *c);
    return c->magic == CONFIG_MAGIC && c->channel <= 125;
}

static void wait_bsy(void) { while (FLASH->SR & FLASH_SR_BSY) { } }

bool config_save(const link_cfg_t *in)
{
    link_cfg_t c = *in;
    c.magic = CONFIG_MAGIC;

    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123u;
        FLASH->KEYR = 0xCDEF89ABu;
    }
    wait_bsy();
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPRTERR;
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = CONFIG_ADDR;
    FLASH->CR |= FLASH_CR_STRT;
    wait_bsy();
    FLASH->CR &= ~FLASH_CR_PER;

    const uint16_t *src = (const uint16_t *)&c;
    volatile uint16_t *dst = (volatile uint16_t *)CONFIG_ADDR;
    FLASH->CR |= FLASH_CR_PG;
    for (unsigned i = 0; i < sizeof c / 2; i++) {
        dst[i] = src[i];
        wait_bsy();
    }
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;

    return memcmp((const void *)CONFIG_ADDR, &c, sizeof c) == 0;
}
