#ifndef FGB_APU_H
#define FGB_APU_H

#include "audio/channel.h"

#include <stdint.h>

union fgb_nr50 {
    struct {
        uint8_t vol_r : 3;
        uint8_t vin_r : 1;
        uint8_t vol_l : 3;
        uint8_t vin_l : 1;
    };
    uint8_t value;
};

union fgb_nr51 {
    struct {
        uint8_t ch1_r : 1;
        uint8_t ch2_r : 1;
        uint8_t ch3_r : 1;
        uint8_t ch4_r : 1;
        uint8_t ch1_l : 1;
        uint8_t ch2_l : 1;
        uint8_t ch3_l : 1;
        uint8_t ch4_l : 1;
    };
    uint8_t value;
};

union fgb_nr52 {
    struct {
        uint8_t ch1_en : 1;
        uint8_t ch2_en : 1;
        uint8_t ch3_en : 1;
        uint8_t ch4_en : 1;
        uint8_t : 3;
        uint8_t apu_en : 1;
    };
    struct {
        uint8_t read_only : 4;
        uint8_t : 4;
    };
    uint8_t value;
};

typedef void(*fgb_apu_sample_callback)(const float* samples, size_t frame_count, void* userdata);

typedef struct fgb_apu {
    union fgb_nr50 nr50;
    union fgb_nr51 nr51;
    union fgb_nr52 nr52;

    fgb_audio_channel_1 channel1;
    fgb_audio_channel_2 channel2;
    fgb_audio_channel_3 channel3;
    fgb_audio_channel_4 channel4;

    uint32_t sample_rate;
    size_t sample_chunk;
    uint16_t cycle_counter;
    uint8_t sequencer_step; // 0-7
    uint64_t accumulator;
    fgb_apu_sample_callback sample_callback;
    void* userdata;
    float* sample_buffer;
    uint32_t sample_count;
} fgb_apu;

fgb_apu* fgb_apu_create(uint32_t sample_rate, fgb_apu_sample_callback sample_callback, void* userdata);
void fgb_apu_destroy(fgb_apu* apu);
void fgb_apu_reset(fgb_apu* apu);
void fgb_apu_tick(fgb_apu* apu);

uint8_t fgb_apu_read(const fgb_apu* apu, uint16_t addr);
void fgb_apu_write(fgb_apu* apu, uint16_t addr, uint8_t value);

#endif // FGB_APU_H
