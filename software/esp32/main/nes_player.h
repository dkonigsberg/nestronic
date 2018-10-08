/*
 * NES Based Music Player
 *
 * This module is a front-end for VGM and NSF playback, as well as
 * sound effects and general NES APU control.
 */

#ifndef NES_PLAYER_H
#define NES_PLAYER_H

#include <esp_err.h>
#include <esp_types.h>

#include "vgm.h"
#include "nsf.h"

typedef enum {
    NES_PLAYER_INIT,
    NES_PLAYER_STARTED,
    NES_PLAYER_FINISHED
} nes_playback_state_t;

typedef enum {
    NES_REPEAT_NONE,
    NES_REPEAT_LOOP,
    NES_REPEAT_CONTINUOUS,
} nes_playback_repeat_t;

typedef enum {
    NES_PLAYER_EFFECT_CHIME = 0,
    NES_PLAYER_EFFECT_BLIP,
    NES_PLAYER_EFFECT_CREDIT
} nes_player_effect_t;

typedef void (*nes_playback_cb_t)(nes_playback_state_t state);

esp_err_t nes_player_init();

esp_err_t nes_player_play_vgm_file(const char *filename, nes_playback_repeat_t repeat, nes_playback_cb_t cb, const vgm_gd3_tags_t **tags);
esp_err_t nes_player_play_nsf_file(const char *filename, uint8_t song, nes_playback_cb_t cb, const nsf_header_t **header);
esp_err_t nes_player_play_effect(nes_player_effect_t effect, nes_playback_repeat_t repeat);
esp_err_t nes_player_stop();
esp_err_t nes_player_benchmark_data();

#endif /* NES_PLAYER_H */
