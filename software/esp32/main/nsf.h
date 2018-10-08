#ifndef NSF_H
#define NSF_H

#include <esp_err.h>
#include <stdint.h>

#include "nes.h"

typedef struct {
    uint8_t version;
    uint8_t total_songs;
    uint8_t starting_song;
    uint16_t load_address;
    uint16_t init_address;
    uint16_t play_address;
    char name[32];
    char artist[32];
    char copyright[32];
    uint16_t play_speed_ntsc;
    uint8_t bankswitch_init[8];
    uint16_t play_speed_pal;
    uint8_t pal_ntsc_bits;
    uint8_t extra_sound_chips;
    uint8_t extra[4];
} nsf_header_t;

typedef struct nsf_file_t nsf_file_t;

typedef void (*nsf_apu_write_cb_t)(nes_apu_register_t reg, uint8_t dat);

/*
 * Open the provided NSF file and just read the header.
 *
 * This is a convenience function for cases where we just want to know
 * the contents of the NSF file's header, without allocating any of the
 * larger data structures necessary for playback processing.
 * Since the header size is fixed, data is written to the provided
 * structure without performing any allocation internally.
 */
esp_err_t nsf_read_header(const char *filename, nsf_header_t *header);

esp_err_t nsf_open(nsf_file_t **nsf, const char *filename);

void nsf_log_header_fields(const nsf_file_t *nsf);
const nsf_header_t *nsf_get_header(const nsf_file_t *nsf);

esp_err_t nsf_playback_init(nsf_file_t *nsf, uint8_t song, nsf_apu_write_cb_t apu_write_cb);
esp_err_t nsf_playback_frame(nsf_file_t *nsf);

void nsf_free(nsf_file_t *nsf);

#endif /* NSF_H */
