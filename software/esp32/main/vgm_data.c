#include "vgm_data.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_types.h>
#include <sys/param.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "nes.h"
#include "uthash.h"
#include "utlist.h"

static const char *TAG = "vgm_data";

struct vgm_data_t {
    uint8_t raw_data[32768];
    uint32_t block_sample_time[512];
};

typedef struct {
    uint32_t sample_time;
    uint16_t block;
} vgm_data_block_group_key_t;

struct vgm_data_block_group_t {
    vgm_data_block_group_key_t key;
    vgm_data_block_ref_node_t *block_refs_head;
    uint16_t block_size;
    uint16_t byte_size;
    uint8_t *raw_data;
    uint8_t loaded_block;
    UT_hash_handle hh;
};

typedef struct vgm_data_block_ref_t {
    uint32_t sample_time;
    vgm_data_block_group_t *block_group;
    uint16_t byte_size;
    bool detached;
} vgm_data_block_ref_t;

typedef struct vgm_data_block_ref_node_t {
    vgm_data_block_ref_t *block_ref;
    struct vgm_data_block_ref_node_t *next, *prev;
} vgm_data_block_ref_node_t;

struct vgm_data_state_t {
    struct vgm_data_block_group_t *block_groups;
    vgm_data_block_ref_node_t *block_refs_head;
};

static esp_err_t vgm_data_load_impl(vgm_data_t *vgm_data, uint32_t sample_time,
        uint16_t addr, const uint8_t *data, size_t len);

vgm_data_t* vgm_data_create()
{
    vgm_data_t *vgm_data = malloc(sizeof(struct vgm_data_t));
    if (!vgm_data) {
        return NULL;
    }

    bzero(vgm_data, sizeof(struct vgm_data_t));

    return vgm_data;
}

esp_err_t vgm_data_load(vgm_data_t *vgm_data, uint32_t sample_time,
        uint16_t addr, const uint8_t *data, size_t len)
{
    esp_err_t ret = ESP_OK;
    size_t remaining;

    if (!vgm_data || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (addr < 0x8000) {
        if (addr + len <= 0x8000) {
            return ESP_ERR_INVALID_ARG;
        }
        remaining = 0x8000 - addr;
        addr = 0x8000;
        data += remaining;
        len -= remaining;
    }

    remaining = 0;

    if (addr + len > 0x10000) {
        remaining = len;
        len = 0x10000 - addr;
        remaining -= len;
    }

    ret = vgm_data_load_impl(vgm_data, sample_time, addr, data, len);
    if (ret != ESP_OK) {
        return ret;
    }

    if (remaining > 0) {
        if (remaining > 0x8000) {
            remaining = 0x8000;
        }
        ret = vgm_data_load_impl(vgm_data, sample_time, 0x8000, data + len, remaining);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ret;
}

esp_err_t vgm_data_load_impl(vgm_data_t *vgm_data, uint32_t sample_time,
        uint16_t addr, const uint8_t *data, size_t len)
{
    // Validate the inputs, so we don't have to worry about them later on
    if (!vgm_data || !data || addr < 0x8000 || len == 0 || ((addr + len) - 1) > 0xFFFF) {
        return ESP_ERR_INVALID_ARG;
    }

    // Copy the data into our internal buffer
    memcpy(vgm_data->raw_data + (addr - 0x8000), data, len);

    // Convert memory addresses into APU blocks
    uint16_t apu_block = nes_addr_to_apu_block(addr);
    uint16_t block_count = nes_len_to_apu_blocks(len);

    // Figure out the APU block ranges that correspond to the loaded data.
    // Note: This can be two disjoint block ranges, if there is data
    // both before and after address 0xC000.
    uint16_t start_block1 = apu_block;
    uint16_t end_block1 = start_block1 + (block_count - 1);
    uint16_t start_block2 = 0;
    uint16_t end_block2 = 0;
    bool has_block2 = false;

    if (end_block1 > 511) {
         start_block2 = 0;
         end_block2 = end_block1 - 512;
         end_block1 = 511;
         has_block2 = true;
    }

    // Populate the sample time array for the loaded blocks
    for (uint16_t i = start_block1; i <= end_block1; i++) {
        vgm_data->block_sample_time[i] = sample_time;
    }
    if (has_block2) {
        for (uint16_t i = start_block2; i <= end_block2; i++) {
            vgm_data->block_sample_time[i] = sample_time;
        }
    }

    // Log the results of what we just did
    if (has_block2) {
        ESP_LOGI(TAG, "[%d] Data block: $%04X + $%04X (%d-%d)(%d-%d) %d", sample_time,
            addr, len, start_block2, end_block2, start_block1, end_block1, block_count);
    } else {
        ESP_LOGI(TAG, "[%d] Data block: $%04X + $%04X (%d-%d) %d", sample_time,
            addr, len, start_block1, end_block1, block_count);
    }

    return ESP_OK;
}

esp_err_t vgm_data_get_sample_time(const vgm_data_t *vgm_data, uint16_t block, size_t len, uint32_t *sample_time)
{
    if (!vgm_data || block >= 256 || !sample_time) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t block_count = nes_len_to_apu_blocks(len);
    if (block + block_count >= 512) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t val = 0;
    for (uint16_t i = block; i < block + block_count; i++) {
        val = MAX(val, vgm_data->block_sample_time[i]);
    }

    *sample_time = val;

    return ESP_OK;
}

esp_err_t vgm_data_get_data(const vgm_data_t *vgm_data, uint16_t block, size_t len, uint8_t *data)
{
    if (!vgm_data || block >= 256 || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    // Copy the initial segment
    uint16_t addr = 0xC000 + (block * 64);
    size_t copy_len = MIN(len, (0xFFFF - addr) + 1);
    memcpy(data, vgm_data->raw_data + (addr - 0x8000), copy_len);

    // Handle wrap-around
    if (copy_len < len) {
        memcpy(data + copy_len, vgm_data->raw_data, len - copy_len);
    }

    return ESP_OK;
}

void vgm_data_free(vgm_data_t *vgm_data)
{
    free(vgm_data);
}

vgm_data_state_t* vgm_data_state_create()
{
    vgm_data_state_t *vgm_data_state = malloc(sizeof(struct vgm_data_state_t));
    if (!vgm_data_state) {
        return NULL;
    }

    bzero(vgm_data_state, sizeof(struct vgm_data_state_t));

    return vgm_data_state;
}

esp_err_t vgm_data_state_add_ref(vgm_data_state_t *vgm_data_state, const vgm_data_t *vgm_data,
        uint32_t sample_time, uint16_t block, size_t len)
{
    esp_err_t ret;

    uint32_t data_sample_time;
    ret = vgm_data_get_sample_time(vgm_data, block, len, &data_sample_time);
    if (ret != ESP_OK) {
        return ret;
    }

    // Get the saved block group, keyed on a combination of the block
    // identifier and the most recent sample time
    struct vgm_data_block_group_t *group;
    vgm_data_block_group_key_t group_key = {
            .sample_time = data_sample_time,
            .block = block
    };
    HASH_FIND(hh, vgm_data_state->block_groups, &group_key, sizeof(vgm_data_block_group_key_t), group);

    // Create and insert a new block group if a saved one did not exist
    if (!group) {
        group = malloc(sizeof(struct vgm_data_block_group_t));
        if (!group) {
            return ESP_ERR_NO_MEM;
        }
        bzero(group, sizeof(struct vgm_data_block_group_t));
        group->key.sample_time = data_sample_time;
        group->key.block = block;
        HASH_ADD(hh, vgm_data_state->block_groups, key, sizeof(vgm_data_block_group_key_t), group);
    }

    // Update the group data, if its unpopulated or shorter than the
    // referenced data
    if (group->byte_size < len) {
        uint8_t *raw_data = realloc(group->raw_data, len);
        if (!raw_data) {
            return ESP_ERR_NO_MEM;
        }
        ret = vgm_data_get_data(vgm_data, block, len, raw_data);
        if (ret != ESP_OK) {
            free(raw_data);
            return ret;
        }
        group->raw_data = raw_data;
        group->byte_size = len;
        group->block_size = nes_len_to_apu_blocks(len);
    }

    // Create a block reference
    vgm_data_block_ref_t *block_ref = malloc(sizeof(vgm_data_block_ref_t));
    if (!block_ref) {
        return ESP_ERR_NO_MEM;
    }
    bzero(block_ref, sizeof(vgm_data_block_ref_t));
    block_ref->sample_time = sample_time;
    block_ref->block_group = group;
    block_ref->byte_size = len;

    // Insert the block reference into the main reference list
    vgm_data_block_ref_node_t *ref_node = malloc(sizeof(vgm_data_block_ref_node_t));
    if (!ref_node) {
        free (block_ref);
        return ESP_ERR_NO_MEM;
    }
    bzero(ref_node, sizeof(vgm_data_block_ref_node_t));
    ref_node->block_ref = block_ref;
    DL_APPEND(vgm_data_state->block_refs_head, ref_node);

    // Insert the block reference into the group's reference list
    vgm_data_block_ref_node_t *group_ref_node = malloc(sizeof(vgm_data_block_ref_node_t));
    if (!group_ref_node) {
        return ESP_ERR_NO_MEM;
    }
    bzero(group_ref_node, sizeof(vgm_data_block_ref_node_t));
    group_ref_node->block_ref = block_ref;
    DL_APPEND(group->block_refs_head, group_ref_node);

    return ESP_OK;
}

bool vgm_data_state_has_refs(const vgm_data_state_t *vgm_data_state)
{
    return vgm_data_state->block_refs_head ? true : false;
}

vgm_data_block_ref_node_t* vgm_data_state_ref_list(const vgm_data_state_t *vgm_data_state)
{
    return vgm_data_state->block_refs_head;
}

vgm_data_block_ref_t* vgm_data_state_next_ref(const vgm_data_state_t *vgm_data_state)
{
    return vgm_data_state->block_refs_head->block_ref;
}

vgm_data_block_ref_t* vgm_data_state_take_next_ref(vgm_data_state_t *vgm_data_state)
{
    vgm_data_block_ref_node_t *node = vgm_data_state->block_refs_head;
    if (!node) {
        return NULL;
    }

    vgm_data_block_ref_t *block_ref = node->block_ref;

    // Remove the ref node from the main ref list
    DL_DELETE(vgm_data_state->block_refs_head, node);
    free(node);

    // Remove the ref node from the block group's ref list,
    // assuming its the first element
    vgm_data_block_group_t *block_group = block_ref->block_group;
    node = block_group->block_refs_head;
    if (!node || block_group->block_refs_head->block_ref != block_ref) {
        ESP_LOGE(TAG, "Block group constraint failed!");
        return block_ref;
    }
    node = block_group->block_refs_head;
    DL_DELETE(block_group->block_refs_head, node);
    free(node);

    block_ref->detached = true;

    return block_ref;
}

void vgm_data_state_log_block_groups(const vgm_data_state_t *vgm_data_state)
{
    struct vgm_data_block_group_t *group;

    for(group = vgm_data_state->block_groups; group != NULL; group = group->hh.next) {
        uint16_t block_address = (((uint16_t)group->key.block) << 6) | 0xC000;

        int ref_count = 0;
        vgm_data_block_ref_node_t *current_ref;
        DL_COUNT(group->block_refs_head, current_ref, ref_count);

        ESP_LOGI(TAG, "Block Group: [t=%u, $%04X~%03d] refs=%d, blocks=%d, bytes=%d",
                group->key.sample_time, block_address, group->key.block,
                ref_count, group->block_size, group->byte_size);
    }
}

void vgm_data_state_free(vgm_data_state_t *vgm_data_state)
{
    struct vgm_data_block_group_t *current_group, *tmp_group;
    vgm_data_block_ref_node_t *current_ref, *tmp_ref;

    if (!vgm_data_state) {
        return;
    }

    // Delete the hash table of block groups
    HASH_ITER(hh, vgm_data_state->block_groups, current_group, tmp_group) {
        HASH_DEL(vgm_data_state->block_groups, current_group);

        // Free the block refs list, only deleting the nodes.
        // The actual ref data will be deleted as part of the
        // main ref list.
        DL_FOREACH_SAFE(current_group->block_refs_head, current_ref, tmp_ref) {
            DL_DELETE(current_group->block_refs_head, current_ref);
            free(current_ref);
        }

        free(current_group->raw_data);
        free(current_group);
    }

    // Free the main block refs list, along with ref data
    DL_FOREACH_SAFE(vgm_data_state->block_refs_head, current_ref, tmp_ref) {
        DL_DELETE(vgm_data_state->block_refs_head, current_ref);
        free(current_ref->block_ref);
        free(current_ref);
    }

    free(vgm_data_state);
}

vgm_data_block_ref_t* vgm_data_block_ref_list_element(const vgm_data_block_ref_node_t *list)
{
    return list->block_ref;
}

vgm_data_block_ref_node_t* vgm_data_block_ref_list_next(const vgm_data_block_ref_node_t *list)
{
    return list->next;
}

uint32_t vgm_data_block_ref_sample_time(const vgm_data_block_ref_t *block_ref)
{
    return block_ref->sample_time;
}

vgm_data_block_group_t* vgm_data_block_ref_block_group(const vgm_data_block_ref_t *block_ref)
{
    return block_ref->block_group;
}

uint16_t vgm_data_block_ref_byte_size(const vgm_data_block_ref_t *block_ref)
{
    return block_ref->byte_size;
}

void vgm_data_block_ref_free(vgm_data_block_ref_t *block_ref)
{
    if (block_ref && block_ref->detached) {
        free(block_ref);
    }
}

uint16_t vgm_data_block_group_block_size(const vgm_data_block_group_t *block_group)
{
    return block_group->block_size;
}

uint16_t vgm_data_block_group_byte_size(const vgm_data_block_group_t *block_group)
{
    return block_group->byte_size;
}

uint8_t *vgm_data_block_group_raw_data(const vgm_data_block_group_t *block_group)
{
    return block_group->raw_data;
}

uint8_t vgm_data_block_group_get_loaded_block(const vgm_data_block_group_t *block_group)
{
    return block_group->loaded_block;
}

void vgm_data_block_group_set_loaded_block(vgm_data_block_group_t *block_group, uint8_t loaded_block)
{
    block_group->loaded_block = loaded_block;
}

vgm_data_block_ref_node_t* vgm_data_block_group_ref_list(const vgm_data_block_group_t *block_group)
{
    return block_group->block_refs_head;
}
