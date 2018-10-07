#include "nsf_player.h"

#include <esp_err.h>
#include <esp_log.h>
#include <string.h>
#include <unistd.h>

#include "board_config.h"
#include "i2c_util.h"
#include "nes.h"

static const char *TAG = "nsf_player";

typedef struct nsf_player_t {
    nsf_file_t *nsf_file;
    nes_playback_cb_t playback_cb;
    nes_playback_repeat_t repeat;
    EventGroupHandle_t event_group;
} nsf_player_t;

esp_err_t nsf_player_init(nsf_player_t **player,
        const char *filename,
        nes_playback_cb_t playback_cb,
        nes_playback_repeat_t repeat,
        EventGroupHandle_t event_group)
{
    esp_err_t ret = ESP_OK;
    nsf_player_t *player_result = NULL;

    do {
        player_result = malloc(sizeof(nsf_player_t));
        if (!player_result) {
            ret = ESP_ERR_NO_MEM;
            break;
        }

        bzero(player_result, sizeof(nsf_player_t));
        player_result->playback_cb = playback_cb;
        player_result->repeat = repeat;
        player_result->event_group = event_group;

        ESP_LOGI(TAG, "Opening file: %s", filename);
        ret = nsf_open(&player_result->nsf_file, filename);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open NSF file");
            return ESP_FAIL;
        }

        nsf_log_header_fields(player_result->nsf_file);

        //TODO check for bankswitch and FDS, fail if necessary

        ESP_LOGI(TAG, "At start of data\n");
    } while (0);

    if (ret == ESP_OK) {
        *player = player_result;
    } else {
        nsf_player_free(player_result);
    }

    return ret;
}

const nsf_header_t *nsf_player_get_header(const nsf_player_t *player)
{
    if (player && player->nsf_file) {
        return nsf_get_header(player->nsf_file);
    } else {
        return NULL;
    }
}

static void vgm_player_nsf_apu_write(nes_apu_register_t reg, uint8_t dat)
{
    if (reg == NES_APU_MODCTRL || reg == NES_APU_MODADDR || reg == NES_APU_MODLEN) {
        // Skip DMC commands until we can handle them
        //ESP_LOGI(TAG, "Unsupported DMC command: $%04X, $%02X", reg, dat);
    } else {
        i2c_mutex_lock(I2C_P0_NUM);
        nes_apu_write(I2C_P0_NUM, reg, dat);
        i2c_mutex_unlock(I2C_P0_NUM);
    }
}

esp_err_t nsf_player_prepare(nsf_player_t *player)
{
    ESP_LOGI(TAG, "Preparing for playback");
    const nsf_header_t *header = nsf_get_header(player->nsf_file);

    if (nsf_playback_init(player->nsf_file, header->starting_song - 1, vgm_player_nsf_apu_write) != ESP_OK) {
        ESP_LOGE(TAG, "NSF initialization failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t nsf_player_play_loop(nsf_player_t *player)
{
    ESP_LOGI(TAG, "Starting playback");
    const nsf_header_t *header = nsf_get_header(player->nsf_file);

    while(true) {
        if ((xEventGroupGetBits(player->event_group) & BIT0) == BIT0) {
            break;
        }

        int64_t time0 = esp_timer_get_time();
        if (nsf_playback_frame(player->nsf_file) != ESP_OK) {
            ESP_LOGE(TAG, "NSF frame playback failed");
            break;
        }
        int64_t time1 = esp_timer_get_time();

        int64_t time_remaining = header->play_speed_ntsc - (time1 - time0);

        if (time_remaining > 0 && time_remaining <= header->play_speed_ntsc) {
            usleep(time_remaining);
        }
    }

    // Reset the APU
    i2c_mutex_lock(I2C_P0_NUM);
    nes_apu_init(I2C_P0_NUM);
    i2c_mutex_unlock(I2C_P0_NUM);

    ESP_LOGI(TAG, "Finished playback");

    return ESP_OK;
}

void nsf_player_free(nsf_player_t *player)
{
    if (player) {
        nsf_free(player->nsf_file);
        free(player);
    }
}
