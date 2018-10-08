/*
 * NSF Player Implementation
 */

#ifndef NSF_PLAYER_H
#define NSF_PLAYER_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include "nsf.h"
#include "nes_player.h"

typedef struct nsf_player_t nsf_player_t;

esp_err_t nsf_player_init(nsf_player_t **player,
        const char *filename,
        nes_playback_cb_t playback_cb,
        nes_playback_repeat_t repeat,
        EventGroupHandle_t event_group);

const nsf_header_t *nsf_player_get_header(const nsf_player_t *player);

esp_err_t nsf_player_prepare(nsf_player_t *player, uint8_t song);
esp_err_t nsf_player_play_loop(nsf_player_t *player);

void nsf_player_free(nsf_player_t *player);

#endif /* NSF_PLAYER_H */
