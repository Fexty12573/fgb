#include "apu.h"

#include <stdlib.h>
#include <string.h>

#include <ulog.h>

#include "cpu.h"

#define FRAME_SEQUENCER_FREQUENCY 512 // Hz
#define FRAME_SEQUENCER_CYCLES (FGB_CPU_CLOCK_SPEED / FRAME_SEQUENCER_FREQUENCY)
#define FRAME_SEQUENCER_STEPS 8
#define SAMPLE_LENGTH_MS 5.3f // ~5.3ms latency

#define ACCUMULATE_SAMPLES(CHANNEL) \
    do { \
        const float raw_sample = apu->channel##CHANNEL##.sample; \
        if (apu->nr51.ch##CHANNEL##_l) { \
            left_sample += (raw_sample * (float)apu->nr50.vol_l) / (15.0f * 7.0f); \
        } \
        if (apu->nr51.ch##CHANNEL##_r) { \
            right_sample += (raw_sample * (float)apu->nr50.vol_r) / (15.0f * 7.0f); \
        } \
    } while (0)

fgb_apu* fgb_apu_create(uint32_t sample_rate, fgb_apu_sample_callback sample_callback, void* userdata) {
    fgb_apu* apu = malloc(sizeof(fgb_apu));
    if (!apu) {
        log_error("Failed to allocate APU");
        return NULL;
    }

    memset(apu, 0, sizeof(fgb_apu));

    apu->sample_rate = sample_rate;
    apu->channel1.sample_rate = sample_rate;
    apu->channel2.sample_rate = sample_rate;
    apu->channel3.sample_rate = sample_rate;
    apu->channel4.sample_rate = sample_rate;

    apu->sample_chunk = (size_t)(SAMPLE_LENGTH_MS * (float)sample_rate / 1000);
    apu->sample_callback = sample_callback;
    apu->userdata = userdata;
    apu->sample_buffer = malloc(sizeof(float) * 2 * apu->sample_chunk);

    if (!apu->sample_buffer) {
        log_error("Failed to allocate APU sample buffer");
        free(apu);
        return NULL;
    }

    return apu;
}

void fgb_apu_destroy(fgb_apu* apu) {
    if (apu->sample_buffer) {
        free(apu->sample_buffer);
    }

    free(apu);
}

void fgb_apu_tick(fgb_apu* apu) {
    if (!apu->nr52.apu_en) {
        return;
    }

    if (apu->cycle_counter++ >= FRAME_SEQUENCER_CYCLES) {
        // Tick at 512 Hz
        fgb_audio_channel_1_fs_tick(&apu->channel1, apu->sequencer_step);
        fgb_audio_channel_2_fs_tick(&apu->channel2, apu->sequencer_step);
        fgb_audio_channel_3_fs_tick(&apu->channel3, apu->sequencer_step);
        fgb_audio_channel_4_fs_tick(&apu->channel4, apu->sequencer_step);

        apu->cycle_counter = 0;
        apu->sequencer_step = (apu->sequencer_step + 1) % FRAME_SEQUENCER_STEPS;
    }

    fgb_audio_channel_1_tick(&apu->channel1);
    fgb_audio_channel_2_tick(&apu->channel2);
    fgb_audio_channel_3_tick(&apu->channel3);
    fgb_audio_channel_4_tick(&apu->channel4);

    apu->accumulator += apu->sample_rate;
    if (apu->accumulator >= FGB_CPU_CLOCK_SPEED) {
        apu->accumulator -= FGB_CPU_CLOCK_SPEED;

        // Mix samples
        float left_sample = 0.0f;
        float right_sample = 0.0f;

        ACCUMULATE_SAMPLES(1);
        ACCUMULATE_SAMPLES(2);
        ACCUMULATE_SAMPLES(3);
        //ACCUMULATE_SAMPLES(4); // not implemented yet

        // Simple soft clip to [-1, 1]
        if (left_sample > 1.0f) left_sample = 1.0f;
        if (left_sample < -1.0f) left_sample = -1.0f;
        if (right_sample > 1.0f) right_sample = 1.0f;
        if (right_sample < -1.0f) right_sample = -1.0f;

        apu->sample_buffer[apu->sample_count * 2 + 0] = left_sample;
        apu->sample_buffer[apu->sample_count * 2 + 1] = right_sample;
        apu->sample_count++;

        if (apu->sample_count >= apu->sample_chunk) {
            if (apu->sample_callback) {
                apu->sample_callback(apu->sample_buffer, apu->sample_count, apu->userdata);
            }

            apu->sample_count = 0;
        }
    }
}

uint8_t fgb_apu_read(const fgb_apu* apu, uint16_t addr) {
    if (addr < 0xFF10) {
        return 0xFF;
    }

    if (addr < 0xFF15) {
        return fgb_audio_channel_1_read(&apu->channel1, addr);
    }

    if (addr >= 0xFF16 && addr < 0xFF1A) {
        return fgb_audio_channel_2_read(&apu->channel2, addr);
    }

    if (addr < 0xFF1F || (addr >= 0xFF30 && addr < 0xFF40)) {
        return fgb_audio_channel_3_read(&apu->channel3, addr);
    }

    if (addr >= 0xFF20 && addr < 0xFF24) {
        return fgb_audio_channel_4_read(&apu->channel4, addr);
    }

    switch (addr) {
    case 0xFF24:
        return apu->nr50.value;
    case 0xFF25:
        return apu->nr51.value;
    case 0xFF26:
        return (union fgb_nr52) {
            .ch1_en = apu->channel1.enabled,
            .ch2_en = apu->channel2.enabled,
            .ch3_en = apu->channel3.enabled,
            .ch4_en = apu->channel4.enabled,
            .apu_en = apu->nr52.apu_en,
        }.value;
    default:
        return 0xFF;
    }
}

void fgb_apu_write(fgb_apu* apu, uint16_t addr, uint8_t value) {
    if (addr < 0xFF10) {
        return;
    }

    if (addr < 0xFF15) {
        fgb_audio_channel_1_write(&apu->channel1, addr, value);
    }

    if (addr >= 0xFF16 && addr < 0xFF1A) {
        fgb_audio_channel_2_write(&apu->channel2, addr, value);
    }

    if (addr < 0xFF1F || (addr >= 0xFF30 && addr < 0xFF40)) {
        fgb_audio_channel_3_write(&apu->channel3, addr, value);
    }

    if (addr >= 0xFF20 && addr < 0xFF24) {
        fgb_audio_channel_4_write(&apu->channel4, addr, value);
    }

    switch (addr) {
    case 0xFF24:
        apu->nr50.value = value;
        break;
    case 0xFF25:
        apu->nr51.value = value;
        break;
    case 0xFF26:
        apu->nr52.apu_en = (value >> 7) & 1;
        if (!apu->nr52.apu_en) {
            // When APU is disabled, all channels are disabled and length timers reset.
            apu->channel1.enabled = false;
            apu->channel2.enabled = false;
            apu->channel3.enabled = false;
            apu->channel4.enabled = false;
        }
        break;
    default:
        break;
    }
}
