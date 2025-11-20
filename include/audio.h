#ifndef FGB_AUDIO_H
#define FGB_AUDIO_H

#include <stdbool.h>
#include <stdint.h>


bool fgb_audio_init(uint32_t device_rate, uint32_t emu_rate);
void fgb_audio_push_samples(const float* interleaved, size_t frame_count);

#endif // FGB_AUDIO_H
