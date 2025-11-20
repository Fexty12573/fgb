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

typedef struct fgb_apu {
	union fgb_nr50 nr50;
	union fgb_nr51 nr51;
	union fgb_nr52 nr52;

	fgb_audio_channel_1 channel1;
	fgb_audio_channel_2 channel2;
	fgb_audio_channel_3 channel3;
	fgb_audio_channel_4 channel4;
} fgb_apu;

fgb_apu* fgb_apu_init(void);

#endif // FGB_APU_H
