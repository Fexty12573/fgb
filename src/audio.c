#include "audio.h"

#include <stdlib.h>

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
	s_driver = malloc(sizeof(fgb_audio_driver));
	if (!s_driver) {
		log_error("Failed to allocate audio driver");
		return false;
	}

	s_driver->convert = (device_rate != emu_rate);

	ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
	cfg.playback.format = ma_format_f32;
	cfg.playback.channels = 2;
	cfg.sampleRate = device_rate;
	cfg.dataCallback = fgb_audio_callback;
	cfg.pUserData = s_driver;

	ma_result r = ma_pcm_rb_init(ma_format_f32, 2, device_rate * 2, NULL, NULL, &s_driver->buffer);
	if (r != MA_SUCCESS) {
		log_error("Failed to initialize audio ring buffer: %d", r);
		free(s_driver);
		s_driver = NULL;
		return false;
	}

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
			ma_pcm_rb_uninit(&s_driver->buffer);
			free(s_driver);
			s_driver = NULL;
			return false;
		}
	}

	r = ma_device_init(NULL, &cfg, &s_driver->device);
	if (r != MA_SUCCESS) {
		log_error("Failed to initialize audio device: %d", r);
		ma_data_converter_uninit(&s_driver->converter, NULL);
		ma_pcm_rb_uninit(&s_driver->buffer);
		free(s_driver);
		s_driver = NULL;
		return false;
	}

	r = ma_device_start(&s_driver->device);
	if (r != MA_SUCCESS) {
		log_error("Failed to start audio device: %d", r);
		ma_device_uninit(&s_driver->device);
		ma_data_converter_uninit(&s_driver->converter, NULL);
		ma_pcm_rb_uninit(&s_driver->buffer);
		free(s_driver);
		s_driver = NULL;
		return false;
	}

	return true;
}

void fgb_audio_push_samples(const float* interleaved, size_t frame_count) {
	if (!s_driver) {
		return;
	}

	if (s_driver->convert) {
		float buf[4096 * 2];
		ma_uint64 in_frames = frame_count;
		ma_uint64 out_frames = sizeof(buf) / (2 * sizeof(float));
		ma_data_converter_process_pcm_frames(&s_driver->converter, interleaved, &in_frames, buf, &out_frames);

		ma_uint32 frames_written = (ma_uint32)out_frames;
		void* write_buffer = NULL;
		ma_pcm_rb_acquire_write(&s_driver->buffer, &frames_written, &write_buffer);
		memcpy(write_buffer, buf, (size_t)frames_written * 2 * sizeof(float));
		ma_pcm_rb_commit_write(&s_driver->buffer, frames_written);
	} else {
		ma_uint32 frames_written = (ma_uint32)frame_count;
		void* write_buffer = NULL;
		ma_pcm_rb_acquire_write(&s_driver->buffer, &frames_written, &write_buffer);
		memcpy(write_buffer, interleaved, (size_t)frames_written * 2 * sizeof(float));
		ma_pcm_rb_commit_write(&s_driver->buffer, frames_written);
	}
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
