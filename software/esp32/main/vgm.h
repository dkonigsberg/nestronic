#ifndef VGM_H
#define VGM_H

#include <esp_err.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "nes.h"

typedef struct {
    uint32_t eof_offset;
    int version_minor;
    int version_major;
    uint32_t gd3_offset;
    uint32_t total_samples;
    uint32_t loop_offset;
    uint32_t loop_samples;
    uint32_t rate;
    uint32_t data_offset;
    uint32_t nes_apu_clock;
    int nes_apu_fds;
} vgm_header_t;

typedef struct {
    uint32_t version;
    char *track_name;
    char *game_name;
    char *system_name;
    char *track_author;
    char *game_release;
    char *vgm_author;
    char *notes;
} vgm_gd3_tags_t;

typedef enum {
    VGM_CMD_UNKNOWN = 0,
    VGM_CMD_DONE,
    VGM_CMD_WAIT,
    VGM_CMD_NES_APU,
    VGM_CMD_DATA_BLOCK,
    VGM_CMD_MAX
} vgm_command_type_t;

typedef struct {
    uint16_t samples;
} vgm_command_wait_t;

typedef struct {
    nes_apu_register_t reg;
    uint8_t dat;
} vgm_command_nes_apu_t;

typedef struct {
    uint16_t addr;
    uint32_t len;
    uint8_t *data;
} vgm_command_data_block_t;

typedef struct {
    union {
        vgm_command_wait_t wait;
        vgm_command_nes_apu_t nes_apu;
        vgm_command_data_block_t data_block;
    };
} vgm_command_info_t;

typedef struct {
    uint32_t sample_index;
    vgm_command_type_t type;
    vgm_command_info_t info;
} vgm_command_t;

typedef struct vgm_file_t vgm_file_t;

esp_err_t vgm_open(vgm_file_t **vgm_file, const char *filename);

const vgm_header_t *vgm_get_header(const vgm_file_t *vgm_file);
void vgm_log_header_fields(const vgm_file_t *vgm_file);
bool vgm_has_loop(const vgm_file_t *vgm_file);

esp_err_t vgm_read_gd3_tags(vgm_gd3_tags_t **tags, vgm_file_t *vgm_file);
void vgm_free_gd3_tags(vgm_gd3_tags_t *tags);

esp_err_t vgm_seek_start(vgm_file_t *vgm_file);
esp_err_t vgm_seek_restart(vgm_file_t *vgm_file);
esp_err_t vgm_seek_loop(vgm_file_t *vgm_file);
esp_err_t vgm_next_command(vgm_file_t *vgm_file, vgm_command_t *command, bool load_data);

void vgm_free(vgm_file_t *vgm_file);

#endif /* VGM_H */
