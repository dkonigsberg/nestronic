/*
 * VGM File Player
 */

#ifndef VGM_PLAYER_H
#define VGM_PLAYER_H

#include <esp_err.h>
#include <esp_types.h>

#include "vgm.h"

typedef enum {
    VGM_PLAYER_STARTED,
    VGM_PLAYER_FINISHED
} vgm_playback_state_t;

typedef void (*vgm_playback_cb_t)(vgm_playback_state_t state);

esp_err_t vgm_player_init();

esp_err_t vgm_player_play_file(const char *filename, bool enable_looping, vgm_playback_cb_t cb, vgm_gd3_tags_t **tags);
esp_err_t vgm_player_play_chime();
esp_err_t vgm_player_play_blip();
esp_err_t vgm_player_play_credit();
esp_err_t vgm_player_stop();

#endif /* VGM_PLAYER_H */
