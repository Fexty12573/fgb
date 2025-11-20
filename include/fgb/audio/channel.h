#ifndef FGB_AUDIO_CHANNEL_H
#define FGB_AUDIO_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

union fgb_nrx1 {
	struct {
		uint8_t init_length_timer : 6;
		uint8_t wave_duty : 2;
	};
	uint8_t value;
};

union fgb_nrx2 {
	struct {
		uint8_t sweep_pace : 3;
		uint8_t env_dir : 1;
		uint8_t init_vol : 4;
	};
	uint8_t value;
};

union fgb_nrx3 {
	uint8_t period_low;
	uint8_t value;
};

union fgb_nrx4 {
	struct {
		uint8_t period_high : 3;
		uint8_t : 3;
		uint8_t length_en : 1;
		uint8_t trigger : 1;
	};
	uint8_t value;
};

typedef struct fgb_audio_channel_1 {
	bool enabled;

	union {
		struct {
			uint8_t step : 3;
			uint8_t dir : 1;
			uint8_t pace : 3;
			uint8_t : 1;
		};
		uint8_t value;
	} nr10;

	union fgb_nrx1 nr11;
	union fgb_nrx2 nr12;
	union fgb_nrx3 nr13;
	union fgb_nrx4 nr14;
} fgb_audio_channel_1;

typedef struct fgb_audio_channel_2 {
	bool enabled;

	union fgb_nrx1 nr21;
	union fgb_nrx2 nr22;
	union fgb_nrx3 nr23;
	union fgb_nrx4 nr24;
} fgb_audio_channel_2;

typedef struct fgb_audio_channel_3 {
	bool enabled;

	union {
		struct {
			uint8_t : 7;
			uint8_t dac_en : 1;
		};
		uint8_t value;
	} nr30;

	union {
		uint8_t init_length_timer;
		uint8_t value;
	} nr31;

	union {
		struct {
			uint8_t : 5;
			uint8_t output_level : 2;
			uint8_t : 1;
		};
		uint8_t value;
	} nr32;

	union fgb_nrx3 nr33;
	union fgb_nrx4 nr34;

	uint8_t wave_pattern_ram[16];
} fgb_audio_channel_3;

typedef struct fgb_audio_channel_4 {
	bool enabled;

	union fgb_nrx1 nr41; // No wave_duty member
	union fgb_nrx2 nr42;

	union {
		struct {
			uint8_t clk_div : 3;
			uint8_t lfsr_width : 1;
			uint8_t clk_shift : 4;
		};
		uint8_t value;
	} nr43;

	union fgb_nrx4 nr44; // No period_high member
} fgb_audio_channel_4;

#endif // FGB_AUDIO_CHANNEL_H
