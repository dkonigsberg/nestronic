#include "vgm.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/unistd.h>
#include <errno.h>

#include <esp_err.h>
#include <esp_log.h>

#include "zlib.h"

static const char *TAG = "vgm";

#define UINT32_FROM_BYTES(buf, n) \
    (uint32_t)(buf[n+3] << 24 | buf[n+2] << 16 | buf[n+1] << 8 | buf[n])
#define UINT16_FROM_BYTES(buf, n) \
    (uint16_t)(buf[n+1] << 8 | buf[n])

struct vgm_file_t {
    gzFile file;
    vgm_header_t header;
    bool at_vgm_data;
    uint32_t sample_index;
};

static int bcd_to_decimal(unsigned char x);
static esp_err_t vgm_read_header(vgm_file_t *vgm_file);

int bcd_to_decimal(unsigned char x)
{
    return x - 6 * (x >> 4);
}

esp_err_t vgm_open(vgm_file_t **vgm_file, const char *filename)
{
    esp_err_t ret = ESP_OK;
    vgm_file_t *vgm = NULL;

    do {
        vgm = malloc(sizeof(struct vgm_file_t));
        if (!vgm) {
            ret = ESP_ERR_NO_MEM;
            break;
        }

        bzero(vgm, sizeof(struct vgm_file_t));

        vgm->file = gzopen(filename, "rb");
        if (!vgm->file) {
            ESP_LOGE(TAG, "Failed to open file for reading: %s", strerror(errno));
            ret = ESP_FAIL;
            break;
        }

        ret = vgm_read_header(vgm);
        if (ret != ESP_OK) {
            break;
        }

    } while (0);

    if (ret == ESP_OK) {
        *vgm_file = vgm;
    } else {
        vgm_free(vgm);
    }

    return ret;
}

esp_err_t vgm_read_header(vgm_file_t *vgm_file)
{
    size_t n;
    uint8_t buf[256];

    // Assume we are at the start of the file, since this function should
    // only be called from vgm_open().

    // Read the header to a buffer
    n = gzread(vgm_file->file, buf, sizeof(buf));
    if (n < 0x38) {
        int errnum;
        const char *msg = gzerror(vgm_file->file, &errnum);
        ESP_LOGE(TAG, "gzread: %s [%d]", msg, errnum);
        return ESP_FAIL;
    }

    // Validate the magic at the start of the header
    if (memcmp(buf, "Vgm ", 4) != 0) {
        ESP_LOGE(TAG, "File is not VGM");
        return ESP_FAIL;
    }

    // Parse the relevant fields out of the header
    uint32_t eof_offset = UINT32_FROM_BYTES(buf, 0x04) + 0x04;

    int version_minor = bcd_to_decimal(buf[0x08]);
    int version_major = bcd_to_decimal(buf[0x09]);

    uint32_t gd3_offset = UINT32_FROM_BYTES(buf, 0x14);
    if (gd3_offset > 0) { gd3_offset += 0x14; }

    uint32_t total_samples = UINT32_FROM_BYTES(buf, 0x18);

    uint32_t loop_offset = UINT32_FROM_BYTES(buf, 0x1C);
    if (loop_offset > 0) { loop_offset += 0x1C; }

    uint32_t loop_samples = UINT32_FROM_BYTES(buf, 0x20);
    uint32_t rate = UINT32_FROM_BYTES(buf, 0x24);

    uint32_t data_offset = UINT32_FROM_BYTES(buf, 0x34);
    if (data_offset > 0) { data_offset += 0x34; }
    else { data_offset = 0x40; }

    uint32_t nes_apu_clock = UINT32_FROM_BYTES(buf, 0x84);
    int nes_apu_fds;
    if (nes_apu_clock & 0x80000000) {
        nes_apu_fds = 1;
        nes_apu_clock &= 0x7FFFFFFF;
    } else {
        nes_apu_fds = 0;
    }

    // Validate that the header is for a file containing NES APU data
    if (nes_apu_clock < 1000000 || nes_apu_clock > 2000000 || data_offset < 0x38) {
        ESP_LOGE(TAG, "File does not contain valid NES APU data");
        return ESP_FAIL;
    }

    // Populate the header fields
    vgm_header_t *header = &vgm_file->header;
    memset(header, 0, sizeof(vgm_header_t));
    header->eof_offset = eof_offset;
    header->version_minor = version_minor;
    header->version_major = version_major;
    header->gd3_offset = gd3_offset;
    header->total_samples = total_samples;
    header->loop_offset = loop_offset;
    header->loop_samples = loop_samples;
    header->rate = rate;
    header->data_offset = data_offset;
    header->nes_apu_clock = nes_apu_clock;
    header->nes_apu_fds = nes_apu_fds;

    return ESP_OK;
}

const vgm_header_t *vgm_get_header(const vgm_file_t *vgm_file)
{
    return &vgm_file->header;
}

void vgm_log_header_fields(const vgm_file_t *vgm_file)
{
    const vgm_header_t *header = &vgm_file->header;

    ESP_LOGI(TAG, "VGM File Header");
    ESP_LOGI(TAG, "---------------");
    ESP_LOGI(TAG, "EoF offset: %d", header->eof_offset);
    ESP_LOGI(TAG, "Version: %d.%d", header->version_major, header->version_minor);
    ESP_LOGI(TAG, "GD3 offset: %d", header->gd3_offset);
    ESP_LOGI(TAG, "Total samples: %d", header->total_samples);
    ESP_LOGI(TAG, "Loop offset: %d", header->loop_offset);
    ESP_LOGI(TAG, "Loop samples: %d", header->loop_samples);
    ESP_LOGI(TAG, "Rate: %d", header->rate);
    ESP_LOGI(TAG, "Data offset: %d", header->data_offset);
    ESP_LOGI(TAG, "NES APU clock: %f MHz", header->nes_apu_clock / 1000000.0);
    if (header->nes_apu_fds) {
        ESP_LOGI(TAG, "FDS Add-on\n");
    }
}

bool vgm_has_loop(const vgm_file_t *vgm_file)
{
    return vgm_file->header.loop_offset > 0;
}

static char *vgm_parse_gd3_string(uint8_t *input, int len)
{
    if (len <= 2 || (len % 2) != 0) {
        return NULL;
    }

    char *buf = malloc(len / 2);
    if (!buf) {
        return NULL;
    }

    bool has_text = false;
    int j = 0;
    for (int i = 0; i < len; i += 2) {
        if (input[i] != '\0') {
            has_text = true;
        }
        buf[j++] = input[i];
    }

    if (!has_text) {
        free(buf);
        buf = NULL;
    }

    return buf;
}

esp_err_t vgm_read_gd3_tags(vgm_gd3_tags_t **tags, vgm_file_t *vgm_file)
{
    vgm_gd3_tags_t *parsed_tags;

    if (vgm_file->header.gd3_offset == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Calling this function moves us off valid data
    vgm_file->at_vgm_data = false;

    // Seek to the start of the GD3 data
    if (gzseek(vgm_file->file, vgm_file->header.gd3_offset, SEEK_SET) < 0) {
        int errnum;
        const char *msg = gzerror(vgm_file->file, &errnum);
        ESP_LOGE(TAG, "gzseek: %s [%d]", msg, errnum);
        return ESP_FAIL;
    }

    // Read the GD3 header bytes
    uint8_t gd3_header[12];
    if (gzread(vgm_file->file, gd3_header, sizeof(gd3_header)) != sizeof(gd3_header)) {
        int errnum;
        const char *msg = gzerror(vgm_file->file, &errnum);
        ESP_LOGE(TAG, "gzread: %s [%d]", msg, errnum);
        return ESP_FAIL;
    }

    // Validate the magic at the start of the header
    if (memcmp(gd3_header, "Gd3 ", 4) != 0) {
        ESP_LOGE(TAG, "GD3 data not found at offset");
        return ESP_FAIL;
    }

    uint32_t version = UINT32_FROM_BYTES(gd3_header, 4);
    uint32_t gd3_size = UINT32_FROM_BYTES(gd3_header, 8);

    uint8_t *buf = malloc(gd3_size);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    if ((gd3_size % 2) != 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (gzread(vgm_file->file, buf, gd3_size) != gd3_size) {
        int errnum;
        const char *msg = gzerror(vgm_file->file, &errnum);
        ESP_LOGE(TAG, "gzseek: %s [%d]", msg, errnum);
        return ESP_FAIL;
    }

    parsed_tags = malloc(sizeof(vgm_gd3_tags_t));
    if (!parsed_tags) {
        return ESP_ERR_NO_MEM;
    }

    parsed_tags->version = version;

    int index = 0;
    int offset = 0;
    for (int i = 0; i < gd3_size; i += 2) {
        if (buf[i] == 0 && buf[i + 1] == 0) {
            uint8_t *p = buf + offset;
            int p_len = (i - offset) + 2;

            if (index == 0) {
                /* Track name (in English characters) */
                parsed_tags->track_name = vgm_parse_gd3_string(p, p_len);
            } else if (index == 2) {
                /* Game name (in English characters) */
                parsed_tags->game_name = vgm_parse_gd3_string(p, p_len);
            } else if (index == 4) {
                /* System name (in English characters) */
                parsed_tags->system_name = vgm_parse_gd3_string(p, p_len);
            } else if (index == 6) {
                /* Name of Original Track Author (in English characters) */
                parsed_tags->track_author = vgm_parse_gd3_string(p, p_len);
            } else if (index == 8) {
                /* Date of game's release */
                parsed_tags->game_release = vgm_parse_gd3_string(p, p_len);
            } else if (index == 9) {
                /* Name of person who converted it to a VGM file */
                parsed_tags->vgm_author = vgm_parse_gd3_string(p, p_len);
            } else if (index == 10) {
                /* Notes */
                parsed_tags->notes = vgm_parse_gd3_string(p, p_len);
            }
            index++;

            offset = i + 2;
        }
    }

    free(buf);

    *tags = parsed_tags;

    return ESP_OK;
}

void vgm_free_gd3_tags(vgm_gd3_tags_t *tags)
{
    if (tags) {
        if (tags->track_name) {
            free(tags->track_name);
        }
        if (tags->game_name) {
            free(tags->game_name);
        }
        if (tags->system_name) {
            free(tags->system_name);
        }
        if (tags->track_author) {
            free(tags->track_author);
        }
        if (tags->game_release) {
            free(tags->game_release);
        }
        if (tags->vgm_author) {
            free(tags->vgm_author);
        }
        if (tags->notes) {
            free(tags->notes);
        }
        free(tags);
    }
}

esp_err_t vgm_seek_start(vgm_file_t *vgm_file)
{
    if (!vgm_file->at_vgm_data) {
        if (gzseek(vgm_file->file, vgm_file->header.data_offset, SEEK_SET) < 0) {
            int errnum;
            const char *msg = gzerror(vgm_file->file, &errnum);
            ESP_LOGE(TAG, "gzseek: %s [%d]", msg, errnum);
            return ESP_FAIL;
        }
        vgm_file->at_vgm_data = true;
        vgm_file->sample_index = 0;
    }
    return ESP_OK;
}

esp_err_t vgm_seek_restart(vgm_file_t *vgm_file)
{
    vgm_file->at_vgm_data = false;
    return vgm_seek_start(vgm_file);
}

esp_err_t vgm_seek_loop(vgm_file_t *vgm_file)
{
    if (vgm_file->header.loop_offset == 0) {
        ESP_LOGE(TAG, "file does not have a loop offset");
        return ESP_FAIL;
    }

    if (gzseek(vgm_file->file, vgm_file->header.loop_offset, SEEK_SET) < 0) {
        int errnum;
        const char *msg = gzerror(vgm_file->file, &errnum);
        ESP_LOGE(TAG, "gzseek: %s [%d]", msg, errnum);
        return ESP_FAIL;
    }
    vgm_file->at_vgm_data = true;
    return ESP_OK;
}

esp_err_t vgm_next_command(vgm_file_t *vgm_file, vgm_command_t *command, bool load_data)
{
    uint8_t cmd;

    if (vgm_seek_start(vgm_file) != ESP_OK) {
        return ESP_FAIL;
    }

    memset(command, 0, sizeof(vgm_command_t));
    command->sample_index = vgm_file->sample_index;

    if(gzread(vgm_file->file, &cmd, sizeof(cmd)) != sizeof(cmd)) {
        int errnum;
        const char *msg = gzerror(vgm_file->file, &errnum);
        ESP_LOGE(TAG, "gzread: %s [%d]", msg, errnum);
        return ESP_FAIL;
    }

    if (cmd == 0x61) {
        /* Wait n samples, n can range from 0 to 65535 */
        uint8_t buf[2];
        if(gzread(vgm_file->file, &buf, sizeof(buf)) != sizeof(buf)) {
            int errnum;
            const char *msg = gzerror(vgm_file->file, &errnum);
            ESP_LOGE(TAG, "gzread: %s [%d]", msg, errnum);
            return ESP_FAIL;
        }
        command->type = VGM_CMD_WAIT;
        command->info.wait.samples = UINT16_FROM_BYTES(buf, 0);
        vgm_file->sample_index += command->info.wait.samples;
    }
    else if (cmd == 0x62) {
        /* Wait 735 samples */
        command->type = VGM_CMD_WAIT;
        command->info.wait.samples = 735;
        vgm_file->sample_index += command->info.wait.samples;
    }
    else if (cmd == 0x63) {
        /* Wait 882 samples */
        command->type = VGM_CMD_WAIT;
        command->info.wait.samples = 882;
        vgm_file->sample_index += command->info.wait.samples;
    }
    else if (cmd == 0x66) {
        /* End of sound data */
        command->type = VGM_CMD_DONE;
    }
    else if (cmd == 0x67) {
        /* Data block */
        uint8_t header[6];
        if(gzread(vgm_file->file, &header, sizeof(header)) != sizeof(header)) {
            int errnum;
            const char *msg = gzerror(vgm_file->file, &errnum);
            ESP_LOGE(TAG, "gzread: %s [%d]", msg, errnum);
            return ESP_FAIL;
        }

        if (header[0] != 0x66) {
            ESP_LOGE(TAG, "Unknown value at start of data block: %02X", header[0]);
            command->type = VGM_CMD_UNKNOWN;
            return -1;
        }

#if 0
        if (header[1] < 0x3F) {
            ESP_LOGI(TAG, "Data of recorded streams (uncompressed)");
        }
        else if (header[1] >= 0x40 && header[1] <= 0x7E) {
            ESP_LOGI(TAG, "Data of recorded streams (compressed)");
        }
        else if (header[1] == 0x7F) {
            ESP_LOGI(TAG, "Decompression table");
        }
        else if (header[1] >= 0x80 && header[1] <= 0xBF) {
            ESP_LOGI(TAG, "ROM/RAM image dumps");
        }
        else if (header[1] >= 0xC0 && header[1] <= 0xDF) {
            ESP_LOGI(TAG, "RAM writes (for RAM with up to 64 KB)");
        }
        else {
            ESP_LOGI(TAG, "RAM writes (for RAM with more than 64 KB)");
        }
#endif

        uint32_t data_size = UINT32_FROM_BYTES(header, 2);
        uint16_t start_addr = 0;
        uint8_t *data_buf = 0;

        if (header[1] >= 0xC0 && header[1] <= 0xDF) {
            /* RAM writes (for RAM with up to 64 KB) */
            uint8_t addr_buf[2];

            if(gzread(vgm_file->file, addr_buf, 2) != 2) {
                int errnum;
                const char *msg = gzerror(vgm_file->file, &errnum);
                ESP_LOGE(TAG, "gzread: %s [%d]", msg, errnum);
                return ESP_FAIL;
            }

            data_size -= 2;
            start_addr = UINT16_FROM_BYTES(addr_buf, 0);

            ESP_LOGI(TAG, "Data start address: $%04X", start_addr);

            if (load_data) {
                data_buf = malloc(data_size);
                if (!data_buf) {
                    return ESP_ERR_NO_MEM;
                }

                if(gzread(vgm_file->file, data_buf, data_size) != data_size) {
                    free(data_buf);
                    int errnum;
                    const char *msg = gzerror(vgm_file->file, &errnum);
                    ESP_LOGE(TAG, "gzread: %s [%d]", msg, errnum);
                    return ESP_FAIL;
                }

            } else {
                if (gzseek(vgm_file->file, data_size, SEEK_CUR) < 0) {
                    int errnum;
                    const char *msg = gzerror(vgm_file->file, &errnum);
                    ESP_LOGE(TAG, "gzseek: %s [%d]", msg, errnum);
                    return ESP_FAIL;
                }
            }
        }
        else {
            ESP_LOGE(TAG, "Unsupported data block type: %02X", header[1]);
        }

        if (start_addr > 0) {
            command->type = VGM_CMD_DATA_BLOCK;
            command->info.data_block.addr = start_addr;
            command->info.data_block.len = data_size;
            command->info.data_block.data = data_buf;
        } else {
            ESP_LOGE(TAG, "Unsupported data block");
            command->type = VGM_CMD_UNKNOWN;
            if (load_data) {
                free(data_buf);
            }
        }
    }
    else if (cmd >= 0x70 && cmd <= 0x7F) {
        /* Wait n+1 samples, n can range from 0 to 15 */
        command->type = VGM_CMD_WAIT;
        command->info.wait.samples = (cmd & 0x0F) + 1;
    }
    else if (cmd == 0xB4) {
        /* NES APU, write value dd to register aa */
        uint8_t buf[2];
        if(gzread(vgm_file->file, &buf, sizeof(buf)) != sizeof(buf)) {
            int errnum;
            const char *msg = gzerror(vgm_file->file, &errnum);
            ESP_LOGE(TAG, "gzread: %s [%d]", msg, errnum);
            return ESP_FAIL;
        }

        uint8_t reg_l = 0;
        if (buf[0] <= 0x1F) {
            /* Registers $00-$1F equal NES address $4000-$401F */
            reg_l = buf[0];
        }
        else if (buf[0] >= 0x20 && buf[0] <= 0x3E) {
            /* Registers $20-$3E equal NES address $4080-$409E */
            reg_l = 0x80 + (buf[0] - 0x20);
        }
        else if (buf[0] == 0x3F) {
            /* Register $3F equals NES address $4023 */
            reg_l = 0x23;
        }
        else if (buf[0] >= 0x40 && buf[0] <= 0x7F) {
           /* Registers $40-$7F equal NES address $4040-$407F */
           reg_l = 0x40 + (buf[0] - 0x40);
        }
        else {
            ESP_LOGE(TAG, "Unknown NES APU register: %02X", buf[0]);
            command->type = VGM_CMD_UNKNOWN;
            return 0;
        }

        command->type = VGM_CMD_NES_APU;
        command->info.nes_apu.reg = 0x4000 + reg_l; /* Write to 0x4000 + reg_l */
        command->info.nes_apu.dat = buf[1];
    }
    else {
        ESP_LOGE(TAG, "Unsupported command: %02X", cmd);
        // Probably safer to fail here, in case its a multi-byte command
        command->type = VGM_CMD_UNKNOWN;
        return ESP_FAIL;
    }

    return ESP_OK;
}

void vgm_free(vgm_file_t *vgm_file)
{
    if (vgm_file) {
        if (vgm_file->file) {
            gzclose(vgm_file->file);
        }
        free(vgm_file);
    }
}
