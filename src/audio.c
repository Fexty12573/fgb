#include "audio.h"

#include <stdlib.h>
#include <string.h>

#define MINIAUDIO_IMPLEMENTATION
#include <ulog.h>
#include <miniaudio/miniaudio.h>


typedef struct fgb_audio_driver {
    ma_device device;
    ma_mutex lock;
    ma_pcm_rb buffer;
    ma_data_converter converter;
    bool convert;
} fgb_audio_driver;

static fgb_audio_driver* s_driver = NULL;

static void fgb_audio_callback(ma_device* device, void* output, const void* input, ma_uint32 frames);

bool fgb_audio_init(uint32_t device_rate, uint32_t emu_rate) {
    s_driver = calloc(1, sizeof(fgb_audio_driver));
    if (!s_driver) {
        log_error("Failed to allocate audio driver");
        return false;
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = device_rate;
    cfg.dataCallback = fgb_audio_callback;
    cfg.pUserData = s_driver;

    ma_result r = ma_device_init(NULL, &cfg, &s_driver->device);
    if (r != MA_SUCCESS) {
        log_error("Failed to initialize audio device: %d", r);
        free(s_driver);
        s_driver = NULL;
        return false;
    }

    device_rate = s_driver->device.sampleRate; // Update in case the device changed it.

    r = ma_pcm_rb_init(ma_format_f32, 2, device_rate / 4, NULL, NULL, &s_driver->buffer);
    if (r != MA_SUCCESS) {
        log_error("Failed to initialize audio ring buffer: %d", r);
        ma_device_uninit(&s_driver->device);
        free(s_driver);
        s_driver = NULL;
        return false;
    }

    s_driver->convert = (device_rate != emu_rate);
    if (s_driver->convert) {
        const ma_data_converter_config conv_cfg = ma_data_converter_config_init(
            ma_format_f32,
            ma_format_f32,
            2,
            2,
            emu_rate,
            device_rate
        );

        r = ma_data_converter_init(&conv_cfg, NULL, &s_driver->converter);
        if (r != MA_SUCCESS) {
            log_error("Failed to initialize audio data converter: %d", r);
            ma_device_uninit(&s_driver->device);
            ma_pcm_rb_uninit(&s_driver->buffer);
            free(s_driver);
            s_driver = NULL;
            return false;
        }
    }

    r = ma_device_start(&s_driver->device);
    if (r != MA_SUCCESS) {
        log_error("Failed to start audio device: %d", r);
        ma_device_uninit(&s_driver->device);
        if (s_driver->convert) {
            ma_data_converter_uninit(&s_driver->converter, NULL);
        }
        ma_pcm_rb_uninit(&s_driver->buffer);
        free(s_driver);
        s_driver = NULL;
        return false;
    }

    return true;
}

void fgb_audio_push_samples(const float* interleaved, size_t frame_count, void* userdata) {
    fgb_audio_driver* driver = userdata;
    if (!driver || frame_count == 0) return;

    if (driver->convert) {
        // Convert in chunks and write to ring buffer robustly.
        const float* in = interleaved;
        ma_uint64 in_frames_remaining = (ma_uint64)frame_count;
        float buf[4096 * 2];

        while (in_frames_remaining > 0) {
            ma_uint64 in_frames = in_frames_remaining;
            ma_uint64 out_frames = (ma_uint64)(sizeof(buf) / (2 * sizeof(float)));

            ma_result cr = ma_data_converter_process_pcm_frames(&driver->converter, in, &in_frames, buf, &out_frames);
            if (cr != MA_SUCCESS) {
                // On converter failure, drop remaining.
                break;
            }

            in += (size_t)in_frames * 2;
            in_frames_remaining -= in_frames;

            // Write converted frames to ring buffer, handling partial writes.
            ma_uint32 written = 0;
            ma_uint32 total = (ma_uint32)out_frames;
            while (written < total) {
                ma_uint32 to_write = total - written;
                void* write_buffer = NULL;
                ma_result wr = ma_pcm_rb_acquire_write(&driver->buffer, &to_write, &write_buffer);
                if (wr != MA_SUCCESS || to_write == 0) {
                    // Buffer full; stop trying to write this chunk.
                    break;
                }
                memcpy(write_buffer, buf + ((size_t)written * 2), (size_t)to_write * 2 * sizeof(float));
                ma_pcm_rb_commit_write(&driver->buffer, to_write);
                written += to_write;
            }

            // If nothing was consumed and nothing was produced, avoid infinite loop.
            if (in_frames == 0 && out_frames == 0) {
                break;
            }
        }
    } else {
        // No conversion; write directly in chunks that fit the ring buffer.
        const float* src = interleaved;
        ma_uint32 remaining = (ma_uint32)frame_count;
        ma_uint32 written_total = 0;

        while (remaining > 0) {
            ma_uint32 to_write = remaining;
            void* write_buffer = NULL;
            ma_result wr = ma_pcm_rb_acquire_write(&driver->buffer, &to_write, &write_buffer);
            if (wr != MA_SUCCESS || to_write == 0) {
                // Buffer full; drop remaining frames to avoid blocking the producer.
                break;
            }
            memcpy(write_buffer, src + ((size_t)written_total * 2), (size_t)to_write * 2 * sizeof(float));
            ma_pcm_rb_commit_write(&driver->buffer, to_write);
            written_total += to_write;
            remaining -= to_write;
        }
    }
}

void* fgb_audio_get_driver(void) {
    return s_driver;
}

void fgb_audio_callback(ma_device* device, void* output, const void* input, ma_uint32 frames) {
    fgb_audio_driver* driver = device->pUserData;
    (void)input;

    float* dst = output;
    ma_uint32 frames_read = 0;

    void* read_buffer = NULL;

    while (frames_read < frames) {
        ma_uint32 to_read = frames - frames_read;
        const ma_result r = ma_pcm_rb_acquire_read(&driver->buffer, &to_read, &read_buffer);

        float* const dst_ptr = dst + (size_t)frames_read * 2;
        const size_t dst_size = (size_t)to_read * 2 * sizeof(float);

        if (r != MA_SUCCESS || to_read == 0) {
            // Fill the rest with silence
            memset(dst_ptr, 0, (size_t)(frames - frames_read) * 2 * sizeof(float));
            frames_read = frames;
            break;
        }

        memcpy(dst_ptr, read_buffer, dst_size);
        frames_read += to_read;

        (void)ma_pcm_rb_commit_read(&driver->buffer, to_read);
    }

    if (frames_read < frames) {
        // Shouldn't happen due to the loop logic, but just in case
        log_warn("Audio underrun: requested %u frames, got %u frames", frames, frames_read);
    }
}
