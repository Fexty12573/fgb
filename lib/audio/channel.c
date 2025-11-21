#include "audio/channel.h"

#define WAVEFORM_LENGTH 8

#define IS_256HZ_TICK(step) (!((step) & 1))
#define IS_128HZ_TICK(step) (((step) & 3) == 2)
#define IS_64HZ_TICK(step)  ((step) == 7)

#define MAKE_PERIOD(nrx3, nrx4) (((nrx4).period_high) << 8 | (nrx3).period_low)
#define PERIOD_LOW(period) ((period) & 0xFF)
#define PERIOD_HIGH(period) (((period) >> 8) & 0x07)

static const uint8_t s_waveforms[DUTY_CYCLE_COUNT][WAVEFORM_LENGTH] = {
    [DUTY_CYCLE_12_5] = {0, 0, 0, 0, 0, 0, 0, 1}, // 12.5%
    [DUTY_CYCLE_25]   = {1, 0, 0, 0, 0, 0, 0, 1}, // 25%
    [DUTY_CYCLE_50]   = {1, 0, 0, 0, 0, 1, 1, 1}, // 50%
    [DUTY_CYCLE_75]   = {0, 1, 1, 1, 1, 1, 1, 0}, // 75%
};

void fgb_audio_channel_1_tick(fgb_audio_channel_1* ch) {
    if (ch->timer-- <= 0) {
        ch->timer = MAKE_PERIOD(ch->nr13, ch->nr14) << 2;

        ch->waveform_index = (ch->waveform_index + 1) % WAVEFORM_LENGTH;

        if (ch->enabled) {
            ch->sample = s_waveforms[ch->nr11.wave_duty][ch->waveform_index] * ch->envelope.volume;
        } else {
            ch->sample = 0;
        }
    }
}

void fgb_audio_channel_2_tick(fgb_audio_channel_2* ch) {
    if (ch->timer-- <= 0) {
        ch->timer = MAKE_PERIOD(ch->nr23, ch->nr24) << 2;

        ch->waveform_index = (ch->waveform_index + 1) % WAVEFORM_LENGTH;

        if (ch->enabled) {
            ch->sample = s_waveforms[ch->nr21.wave_duty][ch->waveform_index] * ch->envelope.volume;
        } else {
            ch->sample = 0;
        }
    }
}

void fgb_audio_channel_3_tick(fgb_audio_channel_3* ch) {

}

void fgb_audio_channel_4_tick(fgb_audio_channel_4* ch) {
}

void fgb_audio_channel_1_fs_tick(fgb_audio_channel_1* ch, uint8_t step) {
    if (!ch->enabled) {
        return;
    }
    
    if (IS_256HZ_TICK(step) && ch->nr14.length_en && ch->length_timer > 0) {
        ch->length_timer--;
        if (ch->length_timer == 0) {
            ch->enabled = false;
        }
    }

    if (IS_128HZ_TICK(step) && ch->sweep_pace > 0) {
        if (ch->sweep_timer-- <= 0) {
            ch->sweep_timer = ch->sweep_pace ? ch->sweep_pace : 8;
            uint16_t period = MAKE_PERIOD(ch->nr13, ch->nr14);
            
            if (ch->nr10.dir == 0) {
                period += (period >> ch->nr10.step);
            } else {
                period -= (period >> ch->nr10.step);
            }

            if (period > 0x7FF) {
                ch->enabled = false;
            } else {
                ch->nr13.period_low = PERIOD_LOW(period);
                ch->nr14.period_high = PERIOD_HIGH(period);
            }
        }
    }

    if (IS_64HZ_TICK(step) && !ch->envelope.done) {
        if (ch->envelope.timer-- <= 0) {
            ch->envelope.timer = ch->nr12.pace ? ch->nr12.pace : 8;

            if (ch->nr12.env_dir && ch->envelope.volume < 15) {
                ch->envelope.volume++;
            } else if (!ch->nr12.env_dir && ch->envelope.volume > 0) {
                ch->envelope.volume--;
            }

            if (ch->envelope.volume == 0 || ch->envelope.volume == 15) {
                ch->envelope.done = true;
            }
        }
    }
}

void fgb_audio_channel_2_fs_tick(fgb_audio_channel_2* ch, uint8_t step) {
    if (!ch->enabled) {
        return;
    }

    if (IS_256HZ_TICK(step) && ch->nr24.length_en && ch->length_timer > 0) {
        ch->length_timer--;
        if (ch->length_timer == 0) {
            ch->enabled = false;
        }
    }

    if (IS_64HZ_TICK(step) && !ch->envelope.done) {
        if (ch->envelope.timer-- <= 0) {
            ch->envelope.timer = ch->nr22.pace ? ch->nr22.pace : 8;

            if (ch->nr22.env_dir && ch->envelope.volume < 15) {
                ch->envelope.volume++;
            } else if (!ch->nr22.env_dir && ch->envelope.volume > 0) {
                ch->envelope.volume--;
            }

            if (ch->envelope.volume == 0 || ch->envelope.volume == 15) {
                ch->envelope.done = true;
            }
        }
    }
}

void fgb_audio_channel_3_fs_tick(fgb_audio_channel_3* ch, uint8_t step) {
    if (!ch->enabled) {
        return;
    }
}

void fgb_audio_channel_4_fs_tick(fgb_audio_channel_4* ch, uint8_t step) {
    if (!ch->enabled) {
        return;
    }
}

uint8_t fgb_audio_channel_1_read(const fgb_audio_channel_1* ch, uint16_t addr) {
    switch (addr) {
    case 0xFF10:
        return ch->nr10.value;
    case 0xFF11:
        return ch->nr11.value & 0xC0; // Upper 2 bits only, init_length_timer is write-only
    case 0xFF12:
        return ch->nr12.value;
    case 0xFF13:
        return 0x00; // period_low is write-only
    case 0xFF14:
        return ch->nr14.value & 0x78; // Bits 3-6 only, others are write-only
    default:
        return 0xFF;
    }
}

uint8_t fgb_audio_channel_2_read(const fgb_audio_channel_2* ch, uint16_t addr) {
    switch (addr) {
    case 0xFF16:
        return ch->nr21.value & 0xC0; // Upper 2 bits only, init_length_timer is write-only
    case 0xFF17:
        return ch->nr22.value;
    case 0xFF18:
        return 0x00;
    case 0xFF19:
        return ch->nr24.value & 0x78; // Bits 3-6 only, others are write-only
    default:
        return 0xFF;
    }
}

uint8_t fgb_audio_channel_3_read(const fgb_audio_channel_3* ch, uint16_t addr) {
    return 0xFF;
}

uint8_t fgb_audio_channel_4_read(const fgb_audio_channel_4* ch, uint16_t addr) {
    return 0xFF;
}

void fgb_audio_channel_1_write(fgb_audio_channel_1* ch, uint16_t addr, uint8_t value) {
    switch (addr) {
    case 0xFF10:
        if (ch->nr10.pace == 0 && ((value & 0x07) != 0)) {
            // If pace is changed from 0 to non-zero, sweep pace is read immediately and timer reset
            ch->sweep_pace = value & 0x07;
            ch->sweep_timer = ch->sweep_pace ? ch->sweep_pace : 8;
        }

        ch->nr10.value = value;

        if (ch->nr10.pace == 0) {
            // When pace is 0, sweep is disabled
            ch->sweep_pace = 0;
        }
        break;
    case 0xFF11:
        ch->nr11.value = value;
        ch->length_timer = 64 - (ch->nr11.init_length_timer & 0x3F);
        break;
    case 0xFF12:
        ch->nr12.value = value;
        break;
    case 0xFF13:
        ch->nr13.value = value;
        break;
    case 0xFF14:
        ch->nr14.value = value;
        if (ch->nr14.trigger) {
            ch->enabled = true;
            if (ch->length_timer == 0) {
                ch->length_timer = 64;
            }

            ch->envelope.volume = ch->nr12.init_vol;
            ch->envelope.timer = ch->nr12.pace ? ch->nr12.pace : 8;
            ch->envelope.done = false;

            ch->sweep_pace = ch->nr10.pace;
            ch->sweep_timer = ch->sweep_pace ? ch->sweep_pace : 8;

            ch->timer = MAKE_PERIOD(ch->nr13, ch->nr14) << 2;
            ch->waveform_index = 0;

            // TODO:
            // - Reset volume
        }
        break;
    default:
        break;
    }
}

void fgb_audio_channel_2_write(fgb_audio_channel_2* ch, uint16_t addr, uint8_t value) {
    switch (addr) {
    case 0xFF16:
        ch->nr21.value = value;
        ch->length_timer = 64 - (ch->nr21.init_length_timer & 0x3F);
        break;
    case 0xFF17:
        ch->nr22.value = value;
        break;
    case 0xFF18:
        ch->nr23.value = value;
        break;
    case 0xFF19:
        ch->nr24.value = value;
        if (ch->nr24.trigger) {
            ch->enabled = true;
            if (ch->length_timer == 0) {
                ch->length_timer = 64;
            }

            ch->envelope.volume = ch->nr22.init_vol;
            ch->envelope.timer = ch->nr22.pace ? ch->nr22.pace : 8;
            ch->envelope.done = false;

            ch->timer = MAKE_PERIOD(ch->nr23, ch->nr24) << 2;
            ch->waveform_index = 0;

            // TODO:
            // - Reset volume
        }
        break;
    default:
        break;
    }
}

void fgb_audio_channel_3_write(fgb_audio_channel_3* ch, uint16_t addr, uint8_t value) {

}

void fgb_audio_channel_4_write(fgb_audio_channel_4* ch, uint16_t addr, uint8_t value) {

}
