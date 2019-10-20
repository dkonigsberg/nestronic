#ifndef DISPLAY_ASSETS_H
#define DISPLAY_ASSETS_H

#include <stdint.h>

typedef struct {
    uint8_t *bits;
    uint16_t width;
    uint16_t height;
} asset_info_t;

typedef enum {
    ASSET_NESTRONIC,
    ASSET_SEG_A,
    ASSET_SEG_B,
    ASSET_SEG_C,
    ASSET_SEG_D,
    ASSET_SEG_E,
    ASSET_SEG_F,
    ASSET_SEG_G,
    ASSET_SEG_SEP,
    ASSET_TSEG_CH_A,
    ASSET_TSEG_CH_P,
    ASSET_TSEG_CH_M,
    ASSET_CLOCK_ICON,
    ASSET_SNOOZE_ICON
} asset_name_t;

uint8_t display_asset_get(asset_info_t *asset_info, asset_name_t asset_name);

#endif /* DISPLAY_ASSETS_H */
