#include "vgm_player.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_types.h>
#include <sys/param.h>
#include <sys/unistd.h>

#include "vgm.h"
#include "nes_player.h"
#include "vgm_data.h"
#include "utarray.h"
#include "board_config.h"
#include "i2c_util.h"
#include "nes.h"

static const char *TAG = "vgm_player";

#define BLOCK_LOAD_MIN 8
#define BLOCK_LOAD_MAX 127

typedef struct vgm_player_t {
    vgm_file_t *vgm_file;
    vgm_gd3_tags_t *tags;
    nes_playback_cb_t playback_cb;
    nes_playback_repeat_t repeat;
    EventGroupHandle_t event_group;
    bool has_data_block;
    vgm_data_state_t *data_state;
} vgm_player_t;

typedef struct {
    uint8_t loaded_block;
    uint16_t block_size;
    uint32_t cost;
} vgm_data_block_segment_t;

UT_icd vgm_data_block_segment_icd = {sizeof(vgm_data_block_segment_t), NULL, NULL, NULL};
UT_icd uint8_icd = {sizeof(uint8_t), NULL, NULL, NULL};

static UT_array* vgm_player_build_segment_list(
        vgm_data_block_ref_t *last_block_ref,
        uint32_t sample_time, vgm_data_block_group_t *load_map[]);
static UT_array* vgm_player_find_eviction_candiates(
        UT_array *segments, uint32_t block_size);

esp_err_t vgm_player_init(vgm_player_t **player,
        const char *filename,
        nes_playback_cb_t playback_cb,
        nes_playback_repeat_t repeat,
        EventGroupHandle_t event_group)
{
    esp_err_t ret = ESP_OK;
    vgm_player_t *player_result = NULL;

    do {
        player_result = malloc(sizeof(vgm_player_t));
        if (!player_result) {
            ret = ESP_ERR_NO_MEM;
            break;
        }

        bzero(player_result, sizeof(vgm_player_t));
        player_result->playback_cb = playback_cb;
        player_result->repeat = repeat;
        player_result->event_group = event_group;

        ESP_LOGI(TAG, "Opening file: %s", filename);
        ret = vgm_open(&player_result->vgm_file, filename);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open VGM file");
            break;
        }

        vgm_log_header_fields(player_result->vgm_file);

        ret = vgm_read_gd3_tags(&player_result->tags, player_result->vgm_file);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read GD3 tags");
            break;
        }

        ret = vgm_seek_start(player_result->vgm_file);
        if (ret != ESP_OK) {
            break;
        }
        ESP_LOGI(TAG, "At start of data\n");
    } while (0);

    if (ret == ESP_OK) {
        *player = player_result;
    } else {
        vgm_player_free(player_result);
    }

    return ret;
}

const vgm_gd3_tags_t *vgm_player_get_gd3_tags(const vgm_player_t *player)
{
    return player->tags;
}

esp_err_t vgm_player_prepare(vgm_player_t *player)
{
    ESP_LOGI(TAG, "Scanning file");

    vgm_command_t command;

    player->has_data_block = false;
    uint32_t sample_time = 0;
    uint16_t current_block = 0;
    uint16_t current_len = 0;
    bool mod_dirty = false;

    // Allocate the state structure for playback VGM data blocks
    player->data_state = vgm_data_state_create();
    if (!player->data_state) {
        ESP_LOGE(TAG, "Unable to allocate VGM data state");
        return ESP_ERR_NO_MEM;
    }

    // Allocate the temporary memory space for collected VGM data blocks.
    vgm_data_t *vgm_data = vgm_data_create();
    if (!vgm_data) {
        ESP_LOGE(TAG, "Unable to allocate VGM data map");
        return ESP_ERR_NO_MEM;
    }

    while(true) {
        if ((xEventGroupGetBits(player->event_group) & BIT0) == BIT0) {
            break;
        }

        if (vgm_next_command(player->vgm_file, &command, /*load_data*/true) != ESP_OK) {
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
                player->has_data_block = true;
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
            if (vgm_data_state_add_ref(player->data_state, vgm_data, sample_time, current_block, current_len) != ESP_OK) {
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

    if (!vgm_data_state_has_refs(player->data_state)) {
        if (player->has_data_block) {
            ESP_LOGI(TAG, "VGM has unreferenced sample data");
        }
        player->has_data_block = false;
        vgm_data_state_free(player->data_state);
        player->data_state = NULL;
    }
    else {
        // Log collected data for debugging
        vgm_data_state_log_block_groups(player->data_state);
    }

    vgm_seek_restart(player->vgm_file);

    return ESP_OK;
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

esp_err_t vgm_player_play_loop(vgm_player_t *player)
{
    vgm_data_block_group_t *load_map[128] = { 0 };
    vgm_data_block_ref_t *block_ref = NULL;

    uint8_t inc_load_start = 0;
    uint16_t inc_blocks_loaded = 0;

    // Pre-load block groups up to available memory
    if (player->has_data_block) {
        ESP_LOGI(TAG, "Preloading data blocks");
        const uint8_t max_blocks = (BLOCK_LOAD_MAX - BLOCK_LOAD_MIN) + 1;
        uint8_t block_offset = BLOCK_LOAD_MIN;
        uint8_t remaining_blocks = max_blocks;
        vgm_data_block_ref_node_t *node = vgm_data_state_ref_list(player->data_state);
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
        block_ref = vgm_data_state_take_next_ref(player->data_state);
    }

    ESP_LOGI(TAG, "Starting playback");

    vgm_command_t command;
    const double wait_multiplier = 1000000.0/44100.0;
    int64_t last_write_time = 0;
    uint32_t sample_time = 0;

    while(true) {
        if ((xEventGroupGetBits(player->event_group) & BIT0) == BIT0) {
            break;
        }

        if (vgm_next_command(player->vgm_file, &command, /*load_data*/false) != ESP_OK) {
            break;
        }

        if (command.type == VGM_CMD_NES_APU) {
            if ((command.info.nes_apu.reg == NES_APU_MODCTRL ||
                    command.info.nes_apu.reg == NES_APU_MODADDR ||
                    command.info.nes_apu.reg == NES_APU_MODLEN)
                    && !player->has_data_block) {
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
                block_ref = vgm_data_state_take_next_ref(player->data_state);
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
                        UT_array *segments = vgm_player_build_segment_list(last_block_ref, sample_time, load_map);

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
            if (player->repeat == NES_REPEAT_LOOP && vgm_has_loop(player->vgm_file)) {
                ESP_LOGI(TAG, "Seeking to start of loop");
                vgm_seek_loop(player->vgm_file);
            } else if (player->repeat == NES_REPEAT_CONTINUOUS) {
                ESP_LOGI(TAG, "Seeking to start of file");

                // Reset APU for a clean state
                i2c_mutex_lock(I2C_P0_NUM);
                nes_apu_init(I2C_P0_NUM);
                i2c_mutex_unlock(I2C_P0_NUM);

                // Small delay
                vTaskDelay(500 / portTICK_RATE_MS);

                // Seek to start of file
                vgm_seek_restart(player->vgm_file);
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

    return ESP_OK;
}

UT_array* vgm_player_build_segment_list(
        vgm_data_block_ref_t *last_block_ref,
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

void vgm_player_free(vgm_player_t *player)
{
    if (player) {
        vgm_data_state_free(player->data_state);
        vgm_free_gd3_tags(player->tags);
        vgm_free(player->vgm_file);
        free(player);
    }
}
