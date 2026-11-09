#pragma once
/* Persistent link settings in our own flash page (0x08040000, outside the stock
 * firmware/settings/logo areas). */
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t magic;       /* CONFIG_MAGIC */
    uint8_t addr[5];      /* radio register order (LSB first) */
    uint8_t channel;
    uint8_t id[4];        /* P906 device ID from pairing */
    uint8_t pad[2];
} link_cfg_t;

bool config_load(link_cfg_t *c);
bool config_save(const link_cfg_t *c);
