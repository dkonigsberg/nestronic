#include "vgm_player.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_types.h>
#include <sys/unistd.h>
#include <string.h>

#include "board_config.h"
#include "i2c_util.h"
#include "nes.h"
#include "vgm.h"
#include "display.h"
#include "vpool.h"

static const char *TAG = "vgm_player";

static xQueueHandle vgm_player_event_queue = NULL;
static EventGroupHandle_t vgm_player_event_group = NULL;
static TimerHandle_t vgm_player_idle_timer = 0;

static void vgm_player_play_effect_chime();
static void vgm_player_play_effect_blip();
static void vgm_player_play_effect_credit();
static void vgm_player_play_vgm(vgm_file_t *vgm_file, bool enable_looping, vgm_playback_cb_t playback_cb);

typedef enum {
    VGM_PLAYER_PLAY_EFFECT,
    VGM_PLAYER_PLAY_VGM
} vgm_player_command_t;

typedef enum {
    VGM_PLAYER_EFFECT_CHIME = 0,
    VGM_PLAYER_EFFECT_BLIP,
    VGM_PLAYER_EFFECT_CREDIT
} vgm_player_effect_t;

typedef struct {
    vgm_player_command_t command;
    vgm_playback_cb_t playback_cb;
    union {
        vgm_player_effect_t effect;
        vgm_file_t *vgm_file;
    };
    bool enable_looping;
} vgm_player_event_t;

static void vgm_player_idle_timer_callback(TimerHandle_t xTimer)
{
    i2c_mutex_lock(I2C_P0_NUM);
    nes_set_amplifier_enabled(I2C_P0_NUM, false);
    i2c_mutex_unlock(I2C_P0_NUM);
}

static void vgm_player_prepare()
{
    xTimerStop(vgm_player_idle_timer, portMAX_DELAY);
    xEventGroupClearBits(vgm_player_event_group, BIT0);

    i2c_mutex_lock(I2C_P0_NUM);

    bool amplifier_enabled;
    if (nes_get_amplifier_enabled(I2C_P0_NUM, &amplifier_enabled) != ESP_OK) {
        amplifier_enabled = false;
    }

    if (!amplifier_enabled) {
        nes_set_amplifier_enabled(I2C_P0_NUM, true);
        nes_apu_init(I2C_P0_NUM);
        i2c_mutex_unlock(I2C_P0_NUM);
        vTaskDelay(250 / portTICK_RATE_MS);
    } else {
        i2c_mutex_unlock(I2C_P0_NUM);
    }
}

static void vgm_player_cleanup()
{
    xTimerStart(vgm_player_idle_timer, portMAX_DELAY);
}

static void vgm_player_task(void *pvParameters)
{
    ESP_LOGD(TAG, "vgm_player_task");

    vgm_player_idle_timer = xTimerCreate("vgm_player_idle_timer", 1000 / portTICK_RATE_MS,
            pdFALSE, NULL, vgm_player_idle_timer_callback);

    vgm_player_event_t event;
    for(;;) {
        if(xQueueReceive(vgm_player_event_queue, &event, portMAX_DELAY)) {
            if (event.command == VGM_PLAYER_PLAY_EFFECT) {
                vgm_player_prepare();
                if (event.effect == VGM_PLAYER_EFFECT_CHIME) {
                    vgm_player_play_effect_chime();
                } else if (event.effect == VGM_PLAYER_EFFECT_BLIP) {
                    vgm_player_play_effect_blip();
                } else if (event.effect == VGM_PLAYER_EFFECT_CREDIT) {
                    vgm_player_play_effect_credit();
                }
                vgm_player_cleanup();
            }
            else if (event.command == VGM_PLAYER_PLAY_VGM) {
                vgm_player_prepare();
                vgm_player_play_vgm(event.vgm_file, event.enable_looping, event.playback_cb);
                vgm_player_cleanup();
            }
        }
    }
}

esp_err_t vgm_player_init()
{
    // Create the queue for player events
    vgm_player_event_queue = xQueueCreate(10, sizeof(vgm_player_event_t));
    if (!vgm_player_event_queue) {
        return ESP_ERR_NO_MEM;
    }

    vgm_player_event_group = xEventGroupCreate();
    if (!vgm_player_event_group) {
        vQueueDelete(vgm_player_event_queue);
        vgm_player_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(vgm_player_task, "vgm_player_task", 4096, NULL, 5, NULL) != pdPASS) {
        vEventGroupDelete(vgm_player_event_group);
        vgm_player_event_group = NULL;
        vQueueDelete(vgm_player_event_queue);
        vgm_player_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void vgm_player_play_vgm(vgm_file_t *vgm_file, bool enable_looping, vgm_playback_cb_t playback_cb)
{
    ESP_LOGI(TAG, "Starting playback");
    if (playback_cb) {
        playback_cb(VGM_PLAYER_STARTED);
    }

    vgm_command_t command;
    const double wait_multiplier = 1000000.0/44100.0;
    int64_t last_write_time = 0;
    while(true) {
        if ((xEventGroupGetBits(vgm_player_event_group) & BIT0) == BIT0) {
            break;
        }

        if (vgm_next_command(vgm_file, &command) != ESP_OK) {
            break;
        }

        if (command.type == VGM_CMD_NES_APU) {
            if (command.reg == NES_APU_MODCTRL ||
                    command.reg == NES_APU_MODADDR ||
                    command.reg == NES_APU_MODLEN) {
                // Skip DMC commands until we can handle them
                ESP_LOGI(TAG, "Unsupported DMC command: $%04X, $%02X", command.reg, command.dat);
                continue;
            }
            int64_t time0 = esp_timer_get_time();
            i2c_mutex_lock(I2C_P0_NUM);
            nes_apu_write(I2C_P0_NUM, command.reg, command.dat);
            i2c_mutex_unlock(I2C_P0_NUM);
            int64_t time1 = esp_timer_get_time();
            last_write_time += (time1 - time0);
        }
        else if (command.type == VGM_CMD_WAIT) {
            int64_t wait = (command.wait * wait_multiplier) - last_write_time;
            if (wait > 0) {
                last_write_time = 0;
                // Need to use this because vTaskDelay() only has 1ms resolution
                usleep(wait);
            }
        }
        else if (command.type == VGM_CMD_DONE) {
            ESP_LOGI(TAG, "At end of data tag");
            if (enable_looping && vgm_has_loop(vgm_file)) {
                ESP_LOGI(TAG, "Seeking to start of loop");
                vgm_seek_loop(vgm_file);
            } else {
                break;
            }
        }
    }

    // Reset the APU in case we bailed early
    i2c_mutex_lock(I2C_P0_NUM);
    nes_apu_init(I2C_P0_NUM);
    i2c_mutex_unlock(I2C_P0_NUM);

    ESP_LOGI(TAG, "Finished playback");
    vgm_free(vgm_file);

    if (playback_cb) {
        playback_cb(VGM_PLAYER_FINISHED);
    }
}

esp_err_t vgm_player_play_file(const char *filename, bool enable_looping, vgm_playback_cb_t cb, vgm_gd3_tags_t **tags)
{
    esp_err_t ret;
    vgm_player_event_t event;
    vgm_file_t *vgm_file;
    vgm_gd3_tags_t *tags_result = 0;

    ESP_LOGI(TAG, "Opening file: %s", filename);
    ret = vgm_open(&vgm_file, filename);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open VGM file");
        return ESP_FAIL;
    }

    vgm_log_header_fields(vgm_file);

    if (vgm_get_header(vgm_file)->nes_apu_fds) {
        ESP_LOGE(TAG, "FDS Add-on is not supported");
        vgm_free(vgm_file);
        return ESP_FAIL;
    }

    if (tags) {
        if (vgm_read_gd3_tags(&tags_result, vgm_file) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read GD3 tags");
            vgm_free(vgm_file);
            return ESP_FAIL;
        }
    }

    ret = vgm_seek_start(vgm_file);
    if (ret != ESP_OK) {
        vgm_free_gd3_tags(tags_result);
        vgm_free(vgm_file);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "At start of data\n");

    // Start the playback
    bzero(&event, sizeof(vgm_player_event_t));
    event.command = VGM_PLAYER_PLAY_VGM;
    event.vgm_file = vgm_file;
    event.enable_looping = enable_looping;
    event.playback_cb = cb;
    if (xQueueSend(vgm_player_event_queue, &event, 0) != pdTRUE) {
        vgm_free_gd3_tags(tags_result);
        vgm_free(vgm_file);
        return ESP_FAIL;
    }

    if (tags_result) {
        *tags = tags_result;
    }

    return ESP_OK;
}

esp_err_t vgm_player_enqueue_effect(vgm_player_effect_t effect)
{
    vgm_player_event_t event;

    // Start the playback
    bzero(&event, sizeof(vgm_player_event_t));
    event.command = VGM_PLAYER_PLAY_EFFECT;
    event.effect = effect;
    if (xQueueSend(vgm_player_event_queue, &event, 0) != pdTRUE) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t vgm_player_play_chime()
{
    return vgm_player_enqueue_effect(VGM_PLAYER_EFFECT_CHIME);
}

esp_err_t vgm_player_play_blip()
{
    return vgm_player_enqueue_effect(VGM_PLAYER_EFFECT_BLIP);
}

esp_err_t vgm_player_play_credit()
{
    return vgm_player_enqueue_effect(VGM_PLAYER_EFFECT_CREDIT);
}

esp_err_t vgm_player_stop()
{
    xEventGroupSetBits(vgm_player_event_group, BIT0);
    return ESP_OK;
}

void vgm_player_play_effect_chime()
{
    i2c_mutex_lock(I2C_P0_NUM);
    nes_apu_write(I2C_P0_NUM, 0x06, 0x32);
    nes_apu_write(I2C_P0_NUM, 0x07, 0x08);
    nes_apu_write(I2C_P0_NUM, 0x05, 0x7F);
    nes_apu_write(I2C_P0_NUM, 0x04, 0x86);
    i2c_mutex_unlock(I2C_P0_NUM);

    vTaskDelay(165 / portTICK_RATE_MS);

    i2c_mutex_lock(I2C_P0_NUM);
    nes_apu_write(I2C_P0_NUM, 0x06, 0x21);
    nes_apu_write(I2C_P0_NUM, 0x07, 0x08);
    nes_apu_write(I2C_P0_NUM, 0x05, 0x7F);
    nes_apu_write(I2C_P0_NUM, 0x04, 0x86);
    i2c_mutex_unlock(I2C_P0_NUM);

    vTaskDelay(330 / portTICK_RATE_MS);

    i2c_mutex_lock(I2C_P0_NUM);
    nes_apu_write(I2C_P0_NUM, 0x04, 0x90);
    nes_apu_write(I2C_P0_NUM, 0x07, 0x18);
    nes_apu_write(I2C_P0_NUM, 0x06, 0x00);
    i2c_mutex_unlock(I2C_P0_NUM);
}

void vgm_player_play_effect_blip()
{
    i2c_mutex_lock(I2C_P0_NUM);
    nes_apu_write(I2C_P0_NUM, 0x15, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x00, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x01, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x02, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x03, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x04, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x05, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x06, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x07, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x08, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x09, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x0A, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x0B, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x0C, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x0D, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x0E, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x0F, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x10, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x11, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x12, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x13, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x15, 0x0F);
    nes_apu_write(I2C_P0_NUM, 0x17, 0xC0);
    nes_apu_write(I2C_P0_NUM, 0x17, 0xC0);
    nes_apu_write(I2C_P0_NUM, 0x17, 0x40);
    nes_apu_write(I2C_P0_NUM, 0x17, 0xC0);
    nes_apu_write(I2C_P0_NUM, 0x15, 0x0F);
    nes_apu_write(I2C_P0_NUM, 0x17, 0xC0);
    nes_apu_write(I2C_P0_NUM, 0x00, 0x9A);
    nes_apu_write(I2C_P0_NUM, 0x02, 0x8E);
    nes_apu_write(I2C_P0_NUM, 0x03, 0x08);
    nes_apu_write(I2C_P0_NUM, 0x01, 0x7F);
    i2c_mutex_unlock(I2C_P0_NUM);

    vTaskDelay(50 / portTICK_RATE_MS);

    i2c_mutex_lock(I2C_P0_NUM);
    nes_apu_write(I2C_P0_NUM, 0x17, 0xC0);
    nes_apu_write(I2C_P0_NUM, 0x02, 0x47);
    nes_apu_write(I2C_P0_NUM, 0x03, 0x08);
    nes_apu_write(I2C_P0_NUM, 0x01, 0x7F);
    i2c_mutex_unlock(I2C_P0_NUM);

    vTaskDelay(48 / portTICK_RATE_MS);

    i2c_mutex_lock(I2C_P0_NUM);
    nes_apu_write(I2C_P0_NUM, 0x17, 0xC0);
    nes_apu_write(I2C_P0_NUM, 0x00, 0x90);
    nes_apu_write(I2C_P0_NUM, 0x03, 0x18);
    nes_apu_write(I2C_P0_NUM, 0x02, 0x00);
    i2c_mutex_unlock(I2C_P0_NUM);
}

void vgm_player_play_effect_credit()
{
    i2c_mutex_lock(I2C_P0_NUM);
    nes_apu_write(I2C_P0_NUM, 0x15, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x00, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x01, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x02, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x03, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x04, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x05, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x06, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x07, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x08, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x09, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x0A, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x0B, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x0C, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x0D, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x0E, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x0F, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x10, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x11, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x12, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x13, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x15, 0x0F);
    nes_apu_write(I2C_P0_NUM, 0x17, 0xC0);
    nes_apu_write(I2C_P0_NUM, 0x17, 0xFF);
    nes_apu_write(I2C_P0_NUM, 0x15, 0x0F);
    nes_apu_write(I2C_P0_NUM, 0x04, 0x8D);
    nes_apu_write(I2C_P0_NUM, 0x05, 0x7F);
    nes_apu_write(I2C_P0_NUM, 0x06, 0x71);
    nes_apu_write(I2C_P0_NUM, 0x07, 0x08);
    nes_apu_write(I2C_P0_NUM, 0x11, 0x00);
    i2c_mutex_unlock(I2C_P0_NUM);

    vTaskDelay(83 / portTICK_RATE_MS);

    i2c_mutex_lock(I2C_P0_NUM);
    nes_apu_write(I2C_P0_NUM, 0x17, 0xFF);
    nes_apu_write(I2C_P0_NUM, 0x15, 0x0F);
    nes_apu_write(I2C_P0_NUM, 0x06, 0x54);
    nes_apu_write(I2C_P0_NUM, 0x11, 0x00);
    i2c_mutex_unlock(I2C_P0_NUM);

    vTaskDelay(765 / portTICK_RATE_MS);

    i2c_mutex_lock(I2C_P0_NUM);
    nes_apu_write(I2C_P0_NUM, 0x17, 0xFF);
    nes_apu_write(I2C_P0_NUM, 0x15, 0x0F);
    nes_apu_write(I2C_P0_NUM, 0x15, 0x0D);
    nes_apu_write(I2C_P0_NUM, 0x15, 0x0F);
    nes_apu_write(I2C_P0_NUM, 0x11, 0x00);
    nes_apu_write(I2C_P0_NUM, 0x17, 0xFF);
    nes_apu_write(I2C_P0_NUM, 0x15, 0x0F);
    nes_apu_write(I2C_P0_NUM, 0x11, 0x00);
    i2c_mutex_unlock(I2C_P0_NUM);
}
