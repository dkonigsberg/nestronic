#include "nes_player.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_types.h>
#include <sys/unistd.h>
#include <sys/param.h>
#include <string.h>

#include "board_config.h"
#include "i2c_util.h"
#include "nes.h"
#include "display.h"
#include "vgm_player.h"
#include "nsf_player.h"

static const char *TAG = "nes_player";

static xQueueHandle nes_player_event_queue = NULL;
static EventGroupHandle_t nes_player_event_group = NULL;
static TimerHandle_t nes_player_idle_timer = 0;

typedef enum {
    NES_PLAYER_PLAY_EFFECT,
    NES_PLAYER_PLAY_VGM,
    NES_PLAYER_PLAY_NSF,
    NES_PLAYER_BENCHMARK_DATA
} nes_player_command_t;

typedef struct {
    nes_player_command_t command;
    nes_playback_cb_t playback_cb;
    union {
        nes_player_effect_t effect;
        vgm_player_t *vgm_player;
        nsf_player_t *nsf_player;
    };
    nes_playback_repeat_t repeat;
} nes_player_event_t;

static void nes_player_play_effect_impl(nes_player_effect_t effect);
static void nes_player_play_effect_chime();
static void nes_player_play_effect_blip();
static void nes_player_play_effect_credit();
static void nes_player_run_benchmark_data();

static void nes_player_idle_timer_callback(TimerHandle_t xTimer)
{
    i2c_mutex_lock(I2C_P0_NUM);
    nes_set_amplifier_enabled(I2C_P0_NUM, false);
    i2c_mutex_unlock(I2C_P0_NUM);
}

static void nes_player_prepare()
{
    xTimerStop(nes_player_idle_timer, portMAX_DELAY);
    xEventGroupClearBits(nes_player_event_group, BIT0);

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

static void nes_player_cleanup()
{
    xTimerStart(nes_player_idle_timer, portMAX_DELAY);
}

static void nes_player_task(void *pvParameters)
{
    ESP_LOGD(TAG, "nes_player_task");

    nes_player_idle_timer = xTimerCreate("nes_player_idle_timer", 1000 / portTICK_RATE_MS,
            pdFALSE, NULL, nes_player_idle_timer_callback);

    nes_player_event_t event;
    for(;;) {
        if(xQueueReceive(nes_player_event_queue, &event, portMAX_DELAY)) {
            if (event.command == NES_PLAYER_PLAY_EFFECT) {
                nes_player_prepare();
                if (event.repeat == NES_REPEAT_CONTINUOUS) {
                    while(true) {
                        if ((xEventGroupGetBits(nes_player_event_group) & BIT0) == BIT0) {
                            break;
                        }
                        nes_player_play_effect_impl(event.effect);
                        vTaskDelay(500 / portTICK_RATE_MS);
                    }
                } else {
                    nes_player_play_effect_impl(event.effect);
                }
                nes_player_cleanup();
            }
            else if (event.command == NES_PLAYER_PLAY_VGM || event.command == NES_PLAYER_PLAY_NSF) {
                ESP_LOGI(TAG, "RAM left %d", esp_get_free_heap_size());

                nes_player_prepare();

                if (event.playback_cb) {
                    event.playback_cb(NES_PLAYER_INIT);
                }

                if (event.command == NES_PLAYER_PLAY_VGM) {
                    do {
                        if (vgm_player_prepare(event.vgm_player) != ESP_OK) {
                            break;
                        }
                        if (event.playback_cb) {
                            event.playback_cb(NES_PLAYER_STARTED);
                        }
                        if (vgm_player_play_loop(event.vgm_player) != ESP_OK) {
                            break;
                        }
                    } while(0);
                    vgm_player_free(event.vgm_player);
                    event.vgm_player = NULL;
                } else if (event.command == NES_PLAYER_PLAY_NSF) {
                    do {
                        if (nsf_player_prepare(event.nsf_player) != ESP_OK) {
                            break;
                        }
                        if (event.playback_cb) {
                            event.playback_cb(NES_PLAYER_STARTED);
                        }
                        if (nsf_player_play_loop(event.nsf_player) != ESP_OK) {
                            break;
                        }
                    } while(0);
                    nsf_player_free(event.nsf_player);
                    event.nsf_player = NULL;
                }

                if (event.playback_cb) {
                    event.playback_cb(NES_PLAYER_FINISHED);
                }

                nes_player_cleanup();

                ESP_LOGI(TAG, "RAM left %d", esp_get_free_heap_size());
            }
            else if (event.command == NES_PLAYER_BENCHMARK_DATA) {
                nes_player_run_benchmark_data();
            }
        }
    }
}

esp_err_t nes_player_init()
{
    // Create the queue for player events
    nes_player_event_queue = xQueueCreate(10, sizeof(nes_player_event_t));
    if (!nes_player_event_queue) {
        return ESP_ERR_NO_MEM;
    }

    nes_player_event_group = xEventGroupCreate();
    if (!nes_player_event_group) {
        vQueueDelete(nes_player_event_queue);
        nes_player_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(nes_player_task, "nes_player_task", 4096, NULL, 5, NULL) != pdPASS) {
        vEventGroupDelete(nes_player_event_group);
        nes_player_event_group = NULL;
        vQueueDelete(nes_player_event_queue);
        nes_player_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t nes_player_play_vgm_file(const char *filename, nes_playback_repeat_t repeat, nes_playback_cb_t cb, const vgm_gd3_tags_t **tags)
{
    esp_err_t ret;
    nes_player_event_t event;
    vgm_player_t *player;

    // Initialize the player
    ret = vgm_player_init(&player, filename, cb, repeat, nes_player_event_group);
    if (ret != ESP_OK) {
        return ret;
    }

    if (tags) {
        *tags = vgm_player_get_gd3_tags(player);
    }

    // Start the playback
    bzero(&event, sizeof(nes_player_event_t));
    event.command = NES_PLAYER_PLAY_VGM;
    event.vgm_player = player;
    event.playback_cb = cb;
    event.repeat = repeat;
    if (xQueueSend(nes_player_event_queue, &event, 0) != pdTRUE) {
        vgm_player_free(player);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t nes_player_play_nsf_file(const char *filename, nes_playback_cb_t cb, const nsf_header_t **header)
{
    esp_err_t ret;
    nes_player_event_t event;
    nsf_player_t *player;

    // Initialize the player
    ret = nsf_player_init(&player, filename, cb, NES_REPEAT_NONE, nes_player_event_group);
    if (ret != ESP_OK) {
        return ret;
    }

    if (header) {
        *header = nsf_player_get_header(player);
    }

    // Start the playback
    bzero(&event, sizeof(nes_player_event_t));
    event.command = NES_PLAYER_PLAY_NSF;
    event.nsf_player = player;
    event.playback_cb = cb;
    event.repeat = NES_REPEAT_NONE;
    if (xQueueSend(nes_player_event_queue, &event, 0) != pdTRUE) {
        nsf_player_free(player);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t nes_player_play_effect(nes_player_effect_t effect, nes_playback_repeat_t repeat)
{
    nes_player_event_t event;

    // Start the playback
    bzero(&event, sizeof(nes_player_event_t));
    event.command = NES_PLAYER_PLAY_EFFECT;
    event.effect = effect;
    event.repeat = repeat;
    if (xQueueSend(nes_player_event_queue, &event, 0) != pdTRUE) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t nes_player_stop()
{
    xEventGroupSetBits(nes_player_event_group, BIT0);
    return ESP_OK;
}

esp_err_t nes_player_benchmark_data()
{
    nes_player_event_t event;

    bzero(&event, sizeof(nes_player_event_t));
    event.command = NES_PLAYER_BENCHMARK_DATA;
    if (xQueueSend(nes_player_event_queue, &event, 0) != pdTRUE) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

void nes_player_play_effect_impl(nes_player_effect_t effect)
{
    if (effect == NES_PLAYER_EFFECT_CHIME) {
        nes_player_play_effect_chime();
    } else if (effect == NES_PLAYER_EFFECT_BLIP) {
        nes_player_play_effect_blip();
    } else if (effect == NES_PLAYER_EFFECT_CREDIT) {
        nes_player_play_effect_credit();
    }
}

void nes_player_play_effect_chime()
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

void nes_player_play_effect_blip()
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

void nes_player_play_effect_credit()
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

void nes_player_run_benchmark_data()
{
    ESP_LOGI(TAG, "Benchmarking data writes");

    const int iterations = 4;
    uint8_t data[256] = {0};
    int64_t time0;
    int64_t time1;
    int64_t time_total0 = 0;
    int64_t time_total1 = 0;
    int64_t time_total2 = 0;
    int64_t time_total4 = 0;

    for (int i = 0; i < iterations; i++) {
        i2c_mutex_lock(I2C_P0_NUM);
        time0 = esp_timer_get_time();
        nes_data_write(I2C_P0_NUM, 8 + i, data, 32);
        time1 = esp_timer_get_time();
        i2c_mutex_unlock(I2C_P0_NUM);
        time_total0 += (time1 - time0);
    }

    for (int i = 0; i < iterations; i++) {
        i2c_mutex_lock(I2C_P0_NUM);
        time0 = esp_timer_get_time();
        nes_data_write(I2C_P0_NUM, 8 + i, data, 64);
        time1 = esp_timer_get_time();
        i2c_mutex_unlock(I2C_P0_NUM);
        time_total1 += (time1 - time0);
    }

    for (int i = 0; i < iterations; i++) {
        i2c_mutex_lock(I2C_P0_NUM);
        time0 = esp_timer_get_time();
        nes_data_write(I2C_P0_NUM, 8 + i, data, 128);
        time1 = esp_timer_get_time();
        i2c_mutex_unlock(I2C_P0_NUM);
        time_total2 += (time1 - time0);
    }

    for (int i = 0; i < iterations; i++) {
        i2c_mutex_lock(I2C_P0_NUM);
        time0 = esp_timer_get_time();
        nes_data_write(I2C_P0_NUM, 8 + i, data, 256);
        time1 = esp_timer_get_time();
        i2c_mutex_unlock(I2C_P0_NUM);
        time_total4 += (time1 - time0);
    }

    ESP_LOGI(TAG, "Block load: count=0.5, time=%lldms, rate=%d bps",
            (time_total0 / (iterations * 1000LL)),
            (int)((((32*iterations)*8) / (time_total0 * 1.0)) * 1000000));
    ESP_LOGI(TAG, "Block load: count=%d, time=%lldms, rate=%d bps",
            1, (time_total1 / (iterations * 1000LL)),
            (int)((((64*iterations)*8) / (time_total1 * 1.0)) * 1000000));
    ESP_LOGI(TAG, "Block load: count=%d, time=%lldms, rate=%d bps",
            2, (time_total2 / (iterations * 1000LL)),
            (int)((((128*iterations)*8) / (time_total2 * 1.0)) * 1000000));
    ESP_LOGI(TAG, "Block load: count=%d, time=%lldms, rate=%d bps",
            4, (time_total4 / (iterations * 1000LL)),
            (int)((((256*iterations)*8) / (time_total4 * 1.0)) * 1000000));

    const double sample_multiplier = 1000000.0/44100.0;
    int bits = ((256 * iterations) + (128 * iterations) + (64 * iterations)) * 8;
    int blocks = ((4 * iterations) + (2 * iterations) + (1 * iterations));
    double useconds = (time_total1 + time_total2 + time_total4);
    int bps = (int)((bits / useconds) * 1000000);
    ESP_LOGI(TAG, "Block load rate: %d bps", bps);
    ESP_LOGI(TAG, "Time per block: %dms, samples=%d",
            (int)((useconds / blocks) / 1000),
            (int)((useconds / blocks) / sample_multiplier));
}
