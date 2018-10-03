/*
 * VGM File Player
 */

#ifndef VGM_PLAYER_H
#define VGM_PLAYER_H

#include <esp_err.h>
#include <esp_types.h>

#include "vgm.h"
#include "nsf.h"

typedef enum {
    VGM_PLAYER_STARTED,
    VGM_PLAYER_FINISHED
} vgm_playback_state_t;

typedef enum {
    VGM_REPEAT_NONE,
    VGM_REPEAT_LOOP,
    VGM_REPEAT_CONTINUOUS,
} vgm_playback_repeat_t;

typedef enum {
    VGM_PLAYER_EFFECT_CHIME = 0,
    VGM_PLAYER_EFFECT_BLIP,
    VGM_PLAYER_EFFECT_CREDIT
} vgm_player_effect_t;

typedef void (*vgm_playback_cb_t)(vgm_playback_state_t state);

esp_err_t vgm_player_init();

esp_err_t vgm_player_play_vgm_file(const char *filename, vgm_playback_repeat_t repeat, vgm_playback_cb_t cb, vgm_gd3_tags_t **tags);
esp_err_t vgm_player_play_nsf_file(const char *filename, vgm_playback_cb_t cb, nsf_header_t *header);
esp_err_t vgm_player_play_effect(vgm_player_effect_t effect, vgm_playback_repeat_t repeat);
esp_err_t vgm_player_stop();
esp_err_t vgm_player_benchmark_data();

#endif /* VGM_PLAYER_H */
