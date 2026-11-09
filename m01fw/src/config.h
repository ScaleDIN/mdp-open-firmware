#pragma once
/* Persistent settings in our own flash page (0x08040000, outside the stock
 * firmware/settings/logo areas): radio link + UI settings (presets, sound). */
#include <stdint.h>
#include <stdbool.h>
#include "ui.h"

#define LINK_MAGIC 0x434C304Du   /* 'M0LC': set when paired */

typedef struct {
    uint32_t magic;       /* link part valid */
    uint8_t addr[5];      /* radio register order (LSB first) */
    uint8_t channel;
    uint8_t id[4];        /* P906 device ID from pairing */
    uint8_t pad[2];
    uint32_t ui_magic;    /* ui part valid (added later; old pages lack it) */
    ui_settings_t ui;
} link_cfg_t;

/* returns true if the link part is valid; fills ui defaults if that part isn't */
bool config_load(link_cfg_t *c);
bool config_save(const link_cfg_t *c);
