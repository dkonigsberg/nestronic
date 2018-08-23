/*
 * VGM Data Block Handling
 */

#ifndef VGM_DATA_H
#define VGM_DATA_H

#include <esp_err.h>
#include <esp_types.h>

typedef struct vgm_data_t vgm_data_t;
typedef struct vgm_data_state_t vgm_data_state_t;
typedef struct vgm_data_block_group_t vgm_data_block_group_t;
typedef struct vgm_data_block_ref_t vgm_data_block_ref_t;
typedef struct vgm_data_block_ref_node_t vgm_data_block_ref_node_t;

vgm_data_t* vgm_data_create();

esp_err_t vgm_data_load(vgm_data_t *vgm_data, uint32_t sample_time,
        uint16_t addr, const uint8_t *data, size_t len);
esp_err_t vgm_data_get_sample_time(const vgm_data_t *vgm_data, uint16_t block, size_t len, uint32_t *sample_time);
esp_err_t vgm_data_get_data(const vgm_data_t *vgm_data, uint16_t block, size_t len, uint8_t *data);

void vgm_data_free(vgm_data_t *vgm_data);


vgm_data_state_t* vgm_data_state_create();

esp_err_t vgm_data_state_add_ref(vgm_data_state_t *vgm_data_state, const vgm_data_t *vgm_data,
        uint32_t sample_time, uint16_t block, size_t len);
bool vgm_data_state_has_refs(const vgm_data_state_t *vgm_data_state);
vgm_data_block_ref_node_t* vgm_data_state_ref_list(const vgm_data_state_t *vgm_data_state);
vgm_data_block_ref_t* vgm_data_state_next_ref(const vgm_data_state_t *vgm_data_state);
vgm_data_block_ref_t* vgm_data_state_take_next_ref(vgm_data_state_t *vgm_data_state);

void vgm_data_state_log_block_groups(const vgm_data_state_t *vgm_data_state);

void vgm_data_state_free(vgm_data_state_t *vgm_data_state);

vgm_data_block_ref_t* vgm_data_block_ref_list_element(const vgm_data_block_ref_node_t *list);
vgm_data_block_ref_node_t* vgm_data_block_ref_list_next(const vgm_data_block_ref_node_t *list);

uint32_t vgm_data_block_ref_sample_time(const vgm_data_block_ref_t *block_ref);
vgm_data_block_group_t* vgm_data_block_ref_block_group(const vgm_data_block_ref_t *block_ref);
uint16_t vgm_data_block_ref_byte_size(const vgm_data_block_ref_t *block_ref);
void vgm_data_block_ref_free(vgm_data_block_ref_t *block_ref);

uint16_t vgm_data_block_group_block_size(const vgm_data_block_group_t *block_group);
uint16_t vgm_data_block_group_byte_size(const vgm_data_block_group_t *block_group);
uint8_t *vgm_data_block_group_raw_data(const vgm_data_block_group_t *block_group);
uint8_t vgm_data_block_group_get_loaded_block(const vgm_data_block_group_t *block_group);
void vgm_data_block_group_set_loaded_block(vgm_data_block_group_t *block_group, uint8_t loaded_block);
vgm_data_block_ref_node_t* vgm_data_block_group_ref_list(const vgm_data_block_group_t *block_group);

#endif /* VGM_DATA_H */
