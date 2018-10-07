/*
 * VGM Player Implementation
 */

#ifndef VGM_PLAYER_H
#define VGM_PLAYER_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include "vgm.h"
#include "nes_player.h"

typedef struct vgm_player_t vgm_player_t;

esp_err_t vgm_player_init(vgm_player_t **player,
        const char *filename,
        nes_playback_cb_t playback_cb,
        nes_playback_repeat_t repeat,
        EventGroupHandle_t event_group);

const vgm_gd3_tags_t *vgm_player_get_gd3_tags(const vgm_player_t *player);

esp_err_t vgm_player_prepare(vgm_player_t *player);
esp_err_t vgm_player_play_loop(vgm_player_t *player);

void vgm_player_free(vgm_player_t *player);

#endif /* VGM_PLAYER_H */
