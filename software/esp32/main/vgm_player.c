#include "vgm_player.h"

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
#include "vgm.h"
#include "display.h"
#include "vgm_data.h"
#include "utarray.h"

static const char *TAG = "vgm_player";

#define BLOCK_LOAD_MIN 8
#define BLOCK_LOAD_MAX 127

static xQueueHandle vgm_player_event_queue = NULL;
static EventGroupHandle_t vgm_player_event_group = NULL;
static TimerHandle_t vgm_player_idle_timer = 0;

typedef enum {
    VGM_PLAYER_PLAY_EFFECT,
    VGM_PLAYER_PLAY_VGM,
    VGM_PLAYER_BENCHMARK_DATA
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

typedef struct {
    vgm_file_t *vgm_file;
    vgm_playback_cb_t playback_cb;
    bool looping_enabled;
    bool has_data_block;
    vgm_data_state_t *data_state;
} vgm_player_state_t;

typedef struct {
    uint8_t loaded_block;
    uint16_t block_size;
    uint32_t cost;
} vgm_data_block_segment_t;

UT_icd vgm_data_block_segment_icd = {sizeof(vgm_data_block_segment_t), NULL, NULL, NULL};
UT_icd uint8_icd = {sizeof(uint8_t), NULL, NULL, NULL};

static void vgm_player_play_effect_chime();
static void vgm_player_play_effect_blip();
static void vgm_player_play_effect_credit();
static void vgm_player_scan_vgm(vgm_player_state_t *state);
static void vgm_player_play_vgm(vgm_player_state_t *state);
static void vgm_player_run_benchmark_data();

static UT_array* vgm_player_build_segment_list(
        vgm_player_state_t *state, vgm_data_block_ref_t *last_block_ref,
        uint32_t sample_time, vgm_data_block_group_t *load_map[]);
static UT_array* vgm_player_find_eviction_candiates(
        UT_array *segments, uint32_t block_size);

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
                vgm_player_state_t state = {
                        .vgm_file = event.vgm_file,
                        .playback_cb = event.playback_cb,
                        .looping_enabled = event.enable_looping,
                        .data_state = NULL
                };

                ESP_LOGI(TAG, "RAM left %d", esp_get_free_heap_size());

                vgm_player_prepare();

                vgm_player_scan_vgm(&state);
                vgm_player_play_vgm(&state);

                vgm_free(state.vgm_file);
                vgm_data_state_free(state.data_state);

                vgm_player_cleanup();

                ESP_LOGI(TAG, "RAM left %d", esp_get_free_heap_size());
            }
            else if (event.command == VGM_PLAYER_BENCHMARK_DATA) {
                vgm_player_run_benchmark_data();
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

void vgm_player_scan_vgm(vgm_player_state_t *state)
{
    ESP_LOGI(TAG, "Scanning file");

    vgm_command_t command;

    state->has_data_block = false;
    uint32_t sample_time = 0;
    uint16_t current_block = 0;
    uint16_t current_len = 0;
    bool mod_dirty = false;

    // Allocate the state structure for playback VGM data blocks
    state->data_state = vgm_data_state_create();
    if (!state->data_state) {
        ESP_LOGE(TAG, "Unable to allocate VGM data state");
        return;
    }

    // Allocate the temporary memory space for collected VGM data blocks.
    vgm_data_t *vgm_data = vgm_data_create();
    if (!vgm_data) {
        ESP_LOGE(TAG, "Unable to allocate VGM data map");
        return;
    }

    while(true) {
        if ((xEventGroupGetBits(vgm_player_event_group) & BIT0) == BIT0) {
            break;
        }

        if (vgm_next_command(state->vgm_file, &command, /*load_data*/true) != ESP_OK) {
            break;
        }

        // Collect info from VGM commands relevant to the APU data loading
        if (command.type == VGM_CMD_DATA_BLOCK) {
            do {
                if (!command.info.data_block.data) {
                    ESP_LOGE(TAG, "Missing block data");
                    break;
                }

                if ((command.info.data_block.addr & 0xFFC0) != command.info.data_block.addr) {
                    ESP_LOGE(TAG, "Invalid block address");
                    break;
                }

                if (command.info.data_block.len == 0) {
                    ESP_LOGE(TAG, "Invalid block length");
                    break;
                }

                if (vgm_data_load(vgm_data, sample_time,
                        command.info.data_block.addr,
                        command.info.data_block.data,
                        command.info.data_block.len) != ESP_OK) {
                    ESP_LOGE(TAG, "Unable to load data block into map");
                    break;
                }
                state->has_data_block = true;
            } while (0);

            free(command.info.data_block.data);
        }
        else if (command.type == VGM_CMD_NES_APU) {
            if (command.info.nes_apu.reg == NES_APU_MODADDR) {
                current_block = command.info.nes_apu.dat;
                mod_dirty = true;
            }
            else if (command.info.nes_apu.reg == NES_APU_MODLEN) {
                current_len = (command.info.nes_apu.dat * 16) /*+ 1*/;
                mod_dirty = true;
            }
        }

        // At the end of a command group, collect state changes
        if ((command.type == VGM_CMD_WAIT || command.type == VGM_CMD_DONE)
                && mod_dirty && current_len > 0) {
#if 0
            uint16_t current_end_block = current_block + (nes_len_to_apu_blocks(current_len) - 1);
            ESP_LOGI(TAG, "Sample reference: time=%d, blocks={%d-%d}, bytes=%d",
                    sample_time, current_block, current_end_block, current_len);
#endif

            mod_dirty = false;

            // Check and update the block groups
            if (vgm_data_state_add_ref(state->data_state, vgm_data, sample_time, current_block, current_len) != ESP_OK) {
                ESP_LOGI(TAG, "Unable to add sample reference");
                break;
            }
        }

        // Handle commands that should be processed after a command group
        if (command.type == VGM_CMD_WAIT) {
            sample_time += command.info.wait.samples;
        }
        else if (command.type == VGM_CMD_DONE) {
            ESP_LOGI(TAG, "At end of data tag");
            break;
        }
    }

    vgm_data_free(vgm_data);

    if (!vgm_data_state_has_refs(state->data_state)) {
        if (state->has_data_block) {
            ESP_LOGI(TAG, "VGM has unreferenced sample data");
        }
        state->has_data_block = false;
        vgm_data_state_free(state->data_state);
        state->data_state = NULL;
    }
    else {
        // Log collected data for debugging
        vgm_data_state_log_block_groups(state->data_state);
    }

    vgm_seek_restart(state->vgm_file);
}

static bool vgm_player_load_block_group(const vgm_data_block_group_t *block_group, uint8_t starting_block)
{
    uint8_t block = starting_block;
    size_t load_len = vgm_data_block_group_byte_size(block_group);
    uint8_t *load_data = vgm_data_block_group_raw_data(block_group);
    i2c_mutex_lock(I2C_P0_NUM);
    while(load_len > 0) {
        size_t len = MIN(load_len, 256);
        if (nes_data_write(I2C_P0_NUM, block, load_data, len) != ESP_OK) {
            ESP_LOGE(TAG, "Unable to load data block");
            break;
        }
        load_data += len;
        load_len -= len;
        block += 4;
    }
    i2c_mutex_unlock(I2C_P0_NUM);

#if 1
    uint16_t load_address = (((uint16_t)starting_block) << 6) | 0xC000;
    ESP_LOGI(TAG, "Loaded %d bytes into $%04X [%d]",
            vgm_data_block_group_byte_size(block_group),
            load_address, starting_block);
#endif

    return load_len == 0;
}

static bool vgm_player_load_block_group_increment(const vgm_data_block_group_t *block_group,
        uint8_t starting_block, uint16_t blocks_loaded, uint8_t block_load_limit)
{
    uint8_t block = starting_block + blocks_loaded;
    size_t byte_size = vgm_data_block_group_byte_size(block_group);
    size_t load_offset = blocks_loaded * 64;

    if (load_offset >= byte_size) {
        ESP_LOGE(TAG, "Offset is past end of data");
        return false;
    }

    size_t load_len = MIN(byte_size - load_offset, block_load_limit * 64);
    uint8_t *load_data = vgm_data_block_group_raw_data(block_group) + load_offset;

    i2c_mutex_lock(I2C_P0_NUM);
    while(load_len > 0) {
        size_t len = MIN(load_len, 256);
        if (nes_data_write(I2C_P0_NUM, block, load_data, len) != ESP_OK) {
            ESP_LOGE(TAG, "Unable to load data block");
            break;
        }
        load_data += len;
        load_len -= len;
        block += 4;
    }
    i2c_mutex_unlock(I2C_P0_NUM);

#if 0
    uint16_t load_address = (((uint16_t)starting_block) << 6) | 0xC000;
    ESP_LOGI(TAG, "Loaded %d bytes into $%04X [%d]",
            vgm_data_block_group_byte_size(block_group),
            load_address, starting_block);
#endif

    return load_len == 0;
}

void vgm_player_play_vgm(vgm_player_state_t *state)
{
    vgm_data_block_group_t *load_map[128] = { 0 };
    vgm_data_block_ref_t *block_ref = NULL;

    uint8_t inc_load_start = 0;
    uint16_t inc_blocks_loaded = 0;

    // Pre-load block groups up to available memory
    if (state->has_data_block) {
        ESP_LOGI(TAG, "Preloading data blocks");
        const uint8_t max_blocks = (BLOCK_LOAD_MAX - BLOCK_LOAD_MIN) + 1;
        uint8_t block_offset = BLOCK_LOAD_MIN;
        uint8_t remaining_blocks = max_blocks;
        vgm_data_block_ref_node_t *node = vgm_data_state_ref_list(state->data_state);
        while (node) {
            block_ref = vgm_data_block_ref_list_element(node);

            vgm_data_block_group_t *block_group = vgm_data_block_ref_block_group(block_ref);
            if (vgm_data_block_group_get_loaded_block(block_group) == 0) {
                uint16_t group_block_size = vgm_data_block_group_block_size(block_group);
                if (group_block_size <= remaining_blocks) {
                    if (!vgm_player_load_block_group(block_group, block_offset)) {
                        ESP_LOGI(TAG, "Block loading error");
                        break;
                    }
                    vgm_data_block_group_set_loaded_block(block_group, block_offset);
                    for (uint8_t i = block_offset; i < block_offset + group_block_size; i++) {
                        load_map[i] = block_group;
                    }
                    block_offset += group_block_size;
                    remaining_blocks -= group_block_size;
                } else {
                    ESP_LOGI(TAG, "No more space for preloading");
                    break;
                }
            }

            node = vgm_data_block_ref_list_next(node);
        }
        block_ref = vgm_data_state_take_next_ref(state->data_state);
    }

    ESP_LOGI(TAG, "Starting playback");

    vgm_command_t command;
    const double wait_multiplier = 1000000.0/44100.0;
    int64_t last_write_time = 0;
    uint32_t sample_time = 0;

    while(true) {
        if ((xEventGroupGetBits(vgm_player_event_group) & BIT0) == BIT0) {
            break;
        }

        if (vgm_next_command(state->vgm_file, &command, /*load_data*/false) != ESP_OK) {
            break;
        }

        if (command.type == VGM_CMD_NES_APU) {
            if ((command.info.nes_apu.reg == NES_APU_MODCTRL ||
                    command.info.nes_apu.reg == NES_APU_MODADDR ||
                    command.info.nes_apu.reg == NES_APU_MODLEN)
                    && !state->has_data_block) {
                // Skip DMC commands until we can handle them
                ESP_LOGI(TAG, "Unsupported DMC command: $%04X, $%02X", command.info.nes_apu.reg, command.info.nes_apu.dat);
                continue;
            }

            if (command.info.nes_apu.reg == NES_APU_MODADDR &&
                    block_ref && vgm_data_block_ref_sample_time(block_ref) == sample_time) {
                vgm_data_block_group_t *block_group = vgm_data_block_ref_block_group(block_ref);
                uint8_t loaded_block = vgm_data_block_group_get_loaded_block(block_group);
                if (loaded_block == 0) {
#if 1
                    ESP_LOGI(TAG, "Referenced block not loaded: [%d] $%04X",
                            command.info.nes_apu.dat,
                            (((uint16_t)command.info.nes_apu.dat) << 6) | 0xC000);
#endif
                } else if (inc_load_start > 0) {
#if 1
                    ESP_LOGI(TAG, "Referenced block partially loaded: [%d] $%04X (%d)",
                            command.info.nes_apu.dat,
                            (((uint16_t)command.info.nes_apu.dat) << 6) | 0xC000,
                            inc_blocks_loaded * 64);
#endif
                } else {
#if 0
                    ESP_LOGI(TAG, "NES_APU_MODADDR: [%d->%d] $%04X->$%04X t=%u",
                            command.info.nes_apu.dat, loaded_block,
                            (((uint16_t)command.info.nes_apu.dat) << 6) | 0xC000,
                            (((uint16_t)loaded_block) << 6) | 0xC000, sample_time);
#endif
                    command.info.nes_apu.dat = loaded_block;
                }

            }

            int64_t time0 = esp_timer_get_time();
            i2c_mutex_lock(I2C_P0_NUM);
            nes_apu_write(I2C_P0_NUM, command.info.nes_apu.reg, command.info.nes_apu.dat);
            i2c_mutex_unlock(I2C_P0_NUM);
            int64_t time1 = esp_timer_get_time();
            last_write_time += (time1 - time0);
        }
        else if (command.type == VGM_CMD_WAIT) {
            if (block_ref && vgm_data_block_ref_sample_time(block_ref) == sample_time) {
                int64_t time0 = esp_timer_get_time();
                vgm_data_block_ref_t *last_block_ref = block_ref;
                block_ref = vgm_data_state_take_next_ref(state->data_state);
                if (block_ref) {
                    vgm_data_block_group_t *block_group = vgm_data_block_ref_block_group(block_ref);
                    uint8_t loaded_block = vgm_data_block_group_get_loaded_block(block_group);
                    if (loaded_block == 0) {
#if 0
                        ESP_LOGI(TAG, "Need to load block, size=%d, wait=%d",
                                vgm_data_block_group_byte_size(block_group),
                                command.info.wait.samples);
#endif

                        // Build the list of loaded data segments and eviction costs
                        UT_array *segments = vgm_player_build_segment_list(state, last_block_ref, sample_time, load_map);

                        // Find the loaded segments to evict
                        UT_array *evict = vgm_player_find_eviction_candiates(segments, vgm_data_block_group_block_size(block_group));
                        utarray_free(segments);

                        uint8_t load_segment = 0;
                        if (utarray_len(evict) > 0) {
                            // Evict the chosen segments
                            load_segment = *(uint8_t*)utarray_front(evict); // make safer
                            uint8_t *evict_segment;
                            for(evict_segment = (uint8_t*)utarray_front(evict);
                                    evict_segment != NULL;
                                    evict_segment = (uint8_t*)utarray_next(evict, evict_segment)) {
                                vgm_data_block_group_t *evict_group = load_map[*evict_segment];
                                if (evict_group) {
                                    vgm_data_block_group_set_loaded_block(evict_group, 0);
                                    for (uint8_t i = *evict_segment;
                                            i < *evict_segment + vgm_data_block_group_block_size(evict_group);
                                            i++) {
                                        load_map[i] = NULL;
                                    }
                                }
                            }
                        } else {
                            ESP_LOGI(TAG, "Nothing to evict!");
                        }
                        utarray_free(evict);

                        if (load_segment > 0) {
                            // Set block as if it was loaded
                            vgm_data_block_group_set_loaded_block(block_group, load_segment);

                            // Set state variables for incremental loading
                            inc_load_start = load_segment;
                            inc_blocks_loaded = 0;
                        }
                    }

                    vgm_data_block_ref_free(last_block_ref);
                }
                else {
                    ESP_LOGI(TAG, "End of block references");
                }
                int64_t time1 = esp_timer_get_time();
                last_write_time += (time1 - time0);
            }

            // Figure out how long we need to wait
            int64_t wait = (command.info.wait.samples * wait_multiplier) - last_write_time;

            // If a block group needs to be loaded, then incrementally load
            // until complete.
            if (block_ref && inc_load_start > 0 && wait > 0) {
                // 3000-3500 uS per block
                uint8_t block_load_limit = MIN(wait / 3500, 120);

                if (block_load_limit > 0) {
                    int64_t time0 = esp_timer_get_time();

                    vgm_data_block_group_t *block_group = vgm_data_block_ref_block_group(block_ref);
                    if (!vgm_player_load_block_group_increment(block_group, inc_load_start, inc_blocks_loaded, block_load_limit)) {
                        ESP_LOGE(TAG, "Incremental block load error");
                        inc_load_start = 0;
                        inc_blocks_loaded = 0;
                    }
                    else {
                        inc_blocks_loaded += block_load_limit;
                        // Check if the load is complete
                        if (inc_blocks_loaded >= vgm_data_block_group_block_size(block_group)) {
                            inc_load_start = 0;
                            inc_blocks_loaded = 0;
                        }
                    }

                    // Update the wait time based on block loading
                    int64_t time1 = esp_timer_get_time();
                    wait -= (time1 - time0);
                    last_write_time = 0;
                }
            }

            if (wait > 0) {
                last_write_time = 0;
                // Need to use this because vTaskDelay() only has 1ms resolution
                usleep(wait);
            }

            // Update the sample time
            sample_time += command.info.wait.samples;
        }
        else if (command.type == VGM_CMD_DONE) {
            ESP_LOGI(TAG, "At end of data tag");
            if (state->looping_enabled && vgm_has_loop(state->vgm_file)) {
                ESP_LOGI(TAG, "Seeking to start of loop");
                vgm_seek_loop(state->vgm_file);
            } else {
                break;
            }
        }
    }

    vgm_data_block_ref_free(block_ref);

    // Reset the APU in case we bailed early
    i2c_mutex_lock(I2C_P0_NUM);
    nes_apu_init(I2C_P0_NUM);
    i2c_mutex_unlock(I2C_P0_NUM);

    ESP_LOGI(TAG, "Finished playback");

    if (state->playback_cb) {
        state->playback_cb(VGM_PLAYER_FINISHED);
    }
}

UT_array* vgm_player_build_segment_list(
        vgm_player_state_t *state, vgm_data_block_ref_t *last_block_ref,
        uint32_t sample_time, vgm_data_block_group_t *load_map[])
{
    UT_array *segments;
    utarray_new(segments, &vgm_data_block_segment_icd);
    vgm_data_block_group_t *active_group = vgm_data_block_ref_block_group(last_block_ref);
    vgm_data_block_group_t *current_group = load_map[BLOCK_LOAD_MIN];
    int q = BLOCK_LOAD_MIN;
    for (int i = BLOCK_LOAD_MIN + 1; i <= BLOCK_LOAD_MAX; i++) {
        if (load_map[i] != current_group || i == BLOCK_LOAD_MAX) {
            if (!current_group) {
                #if 0
                ESP_LOGI(TAG, "Block: <empty>, location = %d, len=%d",
                    q, i - q);
                #endif
                vgm_data_block_segment_t segment = {
                    .loaded_block = q,
                    .block_size = i - q,
                    .cost = 0
                };
                utarray_push_back(segments, &segment);
            } else {
                uint32_t cost;

                if (active_group == current_group) {
                    cost = UINT32_MAX;
                } else {
                    vgm_data_block_ref_node_t *ref_node = vgm_data_block_group_ref_list(current_group);
                    if (ref_node) {
                        vgm_data_block_ref_t *next_ref = vgm_data_block_ref_list_element(ref_node);
                        uint32_t next_ref_time = vgm_data_block_ref_sample_time(next_ref);
                        cost = INT32_MAX - (next_ref_time - sample_time);
                    } else {
                        cost = 0;
                    }
                }

                #if 0
                ESP_LOGI(TAG, "Block: location = %d, len=%d, cost=%u",
                    q, i - q, cost);
                #endif

                vgm_data_block_segment_t segment = {
                    .loaded_block = q,
                    .block_size = i - q,
                    .cost = cost
                };
                utarray_push_back(segments, &segment);

                current_group = load_map[i];
                q = i;
            }
        }
    }
    return segments;
}

UT_array* vgm_player_find_eviction_candiates(
        UT_array *segments, uint32_t block_size)
{
    vgm_data_block_segment_t *candidate_p = NULL;
    vgm_data_block_segment_t *candidate_q = NULL;
    uint32_t candidate_size = 0;
    uint32_t candidate_cost = 0;

    vgm_data_block_segment_t *p, *q;
    for(p = (vgm_data_block_segment_t*)utarray_front(segments);
        p != NULL;
        p = (vgm_data_block_segment_t*)utarray_next(segments, p)) {

        int elements = 0;
        uint32_t range_size = 0;
        uint32_t range_cost = 0;
        for(q = p;
            q != NULL;
            q = (vgm_data_block_segment_t*)utarray_next(segments, q)) {
            range_size += p->block_size;
            if (range_cost + p->cost < range_cost) {
                range_cost = UINT32_MAX;
            } else {
                range_cost += p->cost;
            }
            elements++;
            if (range_size >= block_size) {
                break;
            }
        }

        if (p && q && (!candidate_p || range_cost < candidate_cost
            || (range_cost == candidate_cost && range_size < candidate_size))) {
            candidate_p = p;
            candidate_q = q;
            candidate_size = range_size;
            candidate_cost = range_cost;
        }

#if 0
        printf("Elements=%d, size=%u, cost=%u\n", elements, range_size, range_cost);

        printf("Block: location = %d, len=%d, cost=%u\n",
            p->loaded_block, p->block_size, p->cost);
#endif

    }

#if 0
    printf("Block range: location = %d, len=%d, cost=%u\n",
            candidate_p->loaded_block, candidate_size, candidate_cost);
#endif

    UT_array *evict;
    utarray_new(evict, &uint8_icd);

    candidate_q = (vgm_data_block_segment_t*)utarray_next(segments, candidate_q);
    for(p = candidate_p; p != candidate_q;
        p = (vgm_data_block_segment_t*)utarray_next(segments, p)) {
        uint8_t val = p->loaded_block;
        utarray_push_back(evict, &val);
    }

    return evict;
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

esp_err_t vgm_player_benchmark_data()
{
    vgm_player_event_t event;

    bzero(&event, sizeof(vgm_player_event_t));
    event.command = VGM_PLAYER_BENCHMARK_DATA;
    if (xQueueSend(vgm_player_event_queue, &event, 0) != pdTRUE) {
        return ESP_FAIL;
    }

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

void vgm_player_run_benchmark_data()
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
