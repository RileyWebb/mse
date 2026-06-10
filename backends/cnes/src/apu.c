#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "cNES/apu.h"
#include "cNES/bus.h"
#include "cNES/nes.h"
#include "cNES/util.h"
#include "cNES/cpu.h" 

static const uint8_t pulse_duty_table[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0},
    {1, 0, 0, 1, 1, 1, 1, 1}
};

static const uint8_t length_table[32] = {
    10, 254, 20,  2, 40,  4, 80,  6,
    160,  8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22,
    192, 24, 72, 26, 16, 28, 32, 30
};

static const uint16_t noise_period_table[16] = {
    4, 8, 16, 32, 64, 96, 128, 160,
    202, 254, 380, 508, 762, 1016, 2034, 4068
};

static const uint16_t dmc_period_table[16] = {
    428, 380, 340, 320, 286, 254, 226, 214,
    190, 160, 142, 128, 106,  85,  72,  54
};

static inline float clampf(float val, float min_val, float max_val)
{    
    return (val < min_val) ? min_val : ((val > max_val) ? max_val : val);
}   

static inline void apu_queue_sample(APU *apu, float sample)
{
    if (!apu) {
        return;
    }

    sample *= apu->output_volume;
    sample = clampf(sample, -1.0f, 1.0f);

    if (apu->sample_buffer_count >= APU_SAMPLE_BUFFER_CAPACITY) {
        apu->sample_buffer_read_index = (apu->sample_buffer_read_index + 1) % APU_SAMPLE_BUFFER_CAPACITY;
        apu->sample_buffer_count--;
    }

    apu->sample_buffer[apu->sample_buffer_write_index] = sample;
    apu->sample_buffer_write_index = (apu->sample_buffer_write_index + 1) % APU_SAMPLE_BUFFER_CAPACITY;
    apu->sample_buffer_count++;
}

static inline uint8_t apu_pulse_volume(const APU_PulseChannel *pulse)
{
    if (pulse->constant_volume) {
        return pulse->volume & 0x0F;
    }
    return pulse->envelope_decay & 0x0F;
}

static inline uint8_t apu_pulse_output(const APU_PulseChannel *pulse)
{
    if (!pulse->enabled || pulse->length_counter == 0 || pulse->timer_period < 8 || pulse->timer_period > 0x7FF) {
        return 0;
    }

    if (pulse_duty_table[pulse->duty & 0x03][pulse->duty_step & 0x07] == 0) {
        return 0;
    }

    return apu_pulse_volume(pulse);
}

static inline uint8_t apu_triangle_output(const APU_TriangleChannel *triangle)
{
    static const uint8_t triangle_table[32] = {
        15, 14, 13, 12, 11, 10, 9, 8,
         7,  6,  5,  4,  3,  2, 1, 0,
         0,  1,  2,  3,  4,  5, 6, 7,
         8,  9, 10, 11, 12, 13, 14, 15
    };

    if (!triangle->enabled || triangle->length_counter == 0 || triangle->linear_counter == 0 || triangle->timer_period < 2) {
        return 0;
    }

    return triangle_table[triangle->sequence_step & 0x1F];
}

static inline uint8_t apu_noise_output(const APU_NoiseChannel *noise)
{
    if (!noise->enabled || noise->length_counter == 0 || (noise->shift_register & 0x01) != 0) {
        return 0;
    }

    return noise->constant_volume ? (noise->volume & 0x0F) : (noise->envelope_decay & 0x0F);
}

static inline float apu_mix_output(APU *apu)
{
    uint8_t pulse1 = apu_pulse_output(&apu->pulse[0]);
    uint8_t pulse2 = apu_pulse_output(&apu->pulse[1]);
    uint8_t triangle = apu_triangle_output(&apu->triangle);
    uint8_t noise = apu_noise_output(&apu->noise);
    uint8_t dmc = apu->dmc.enabled ? (apu->dmc.output_level & 0x7F) : 0;

    float pulse_mix = (pulse1 + pulse2) > 0
        ? 95.88f / ((8128.0f / (float)(pulse1 + pulse2)) + 100.0f)
        : 0.0f;

    float tnd_input = (float)triangle / 8227.0f + (float)noise / 12241.0f + (float)dmc / 22638.0f;
    float tnd_mix = tnd_input > 0.0f
        ? 159.79f / ((1.0f / tnd_input) + 100.0f)
        : 0.0f;

    return pulse_mix + tnd_mix;
}

static inline void apu_clock_length_counter(uint8_t *length_counter, bool halt)
{
    if (*length_counter > 0 && !halt) {
        (*length_counter)--;
    }
}

static inline void apu_clock_pulse_envelope(APU_PulseChannel *pulse)
{
    if (pulse->envelope_start) {
        pulse->envelope_start = false;
        pulse->envelope_decay = 15;
        pulse->envelope_divider = pulse->envelope_period;
        return;
    }

    if (pulse->envelope_divider == 0) {
        pulse->envelope_divider = pulse->envelope_period;
        if (pulse->envelope_decay == 0) {
            if (pulse->length_halt) {
                pulse->envelope_decay = 15;
            }
        } else {
            pulse->envelope_decay--;
        }
    } else {
        pulse->envelope_divider--;
    }
}

static inline void apu_clock_pulse_sweep(APU_PulseChannel *pulse, bool channel1)
{
    if (!pulse->sweep_enabled || pulse->sweep_shift == 0) {
        if (pulse->sweep_reload) {
            pulse->sweep_reload = false;
        }
        return;
    }

    if (pulse->sweep_divider == 0) {
        uint16_t change = pulse->timer_period >> pulse->sweep_shift;
        uint16_t target = pulse->timer_period;
        if (pulse->sweep_negate) {
            target -= change + (channel1 ? 1 : 0);
        } else {
            target += change;
        }

        if (target < 0x800 && pulse->timer_period >= 8) {
            pulse->timer_period = target;
        }

        pulse->sweep_divider = pulse->sweep_period;
        pulse->sweep_reload = false;
    } else {
        pulse->sweep_divider--;
        if (pulse->sweep_reload) {
            pulse->sweep_divider = pulse->sweep_period;
            pulse->sweep_reload = false;
        }
    }
}

static inline void apu_clock_triangle_linear(APU_TriangleChannel *triangle)
{
    if (triangle->linear_reload_flag) {
        triangle->linear_counter = triangle->linear_counter_reload;
    } else if (triangle->linear_counter > 0) {
        triangle->linear_counter--;
    }

    if (!triangle->control_flag) {
        triangle->linear_reload_flag = false;
    }
}

static inline void apu_clock_noise_envelope(APU_NoiseChannel *noise)
{
    if (noise->envelope_start) {
        noise->envelope_start = false;
        noise->envelope_decay = 15;
        noise->envelope_divider = noise->envelope_period;
        return;
    }

    if (noise->envelope_divider == 0) {
        noise->envelope_divider = noise->envelope_period;
        if (noise->envelope_decay == 0) {
            if (noise->length_halt) {
                noise->envelope_decay = 15;
            }
        } else {
            noise->envelope_decay--;
        }
    } else {
        noise->envelope_divider--;
    }
}

static inline void apu_clock_quarter_frame(APU *apu)
{
    apu_clock_pulse_envelope(&apu->pulse[0]);
    apu_clock_pulse_envelope(&apu->pulse[1]);
    apu_clock_triangle_linear(&apu->triangle);
    apu_clock_noise_envelope(&apu->noise);
}

static inline void apu_clock_half_frame(APU *apu)
{
    apu_clock_length_counter(&apu->pulse[0].length_counter, apu->pulse[0].length_halt);
    apu_clock_length_counter(&apu->pulse[1].length_counter, apu->pulse[1].length_halt);
    apu_clock_length_counter(&apu->triangle.length_counter, apu->triangle.control_flag);
    apu_clock_length_counter(&apu->noise.length_counter, apu->noise.length_halt);
    apu_clock_pulse_sweep(&apu->pulse[0], true);
    apu_clock_pulse_sweep(&apu->pulse[1], false);
}

static inline void apu_dmc_restart(APU_DMCChannel *dmc)
{
    dmc->current_address = dmc->sample_address;
    dmc->bytes_remaining = dmc->sample_length;
    dmc->bits_remaining = 8;
    dmc->sample_buffer_empty = true;
    dmc->timer = dmc->timer_period > 0 ? (uint16_t)(dmc->timer_period - 1U) : 0;
}

static inline void apu_clock_dmc(APU *apu)
{
    APU_DMCChannel *dmc = &apu->dmc;
    if (!dmc->enabled || dmc->timer_period == 0) {
        return;
    }

    if (dmc->timer > 0) {
        dmc->timer--;
        return;
    }

    dmc->timer = dmc->timer_period;

    if (dmc->bits_remaining == 0) {
        if (dmc->sample_buffer_empty && dmc->bytes_remaining > 0) {
            dmc->sample_buffer = BUS_Read(apu->nes, dmc->current_address);
            dmc->sample_buffer_empty = false;
            dmc->current_address++;
            if (dmc->current_address == 0x0000) {
                dmc->current_address = 0x8000;
            }
            dmc->bytes_remaining--;
            if (dmc->bytes_remaining == 0) {
                if (dmc->loop) {
                    apu_dmc_restart(dmc);
                } else if (dmc->irq_enable) {
                    dmc->irq_flag = true;
                }
            }
        }

        if (!dmc->sample_buffer_empty) {
            dmc->shift_register = dmc->sample_buffer;
            dmc->sample_buffer_empty = true;
            dmc->bits_remaining = 8;
        }
    }

    if (dmc->bits_remaining > 0) {
        if ((dmc->shift_register & 0x01) != 0) {
            if (dmc->output_level <= 125) {
                dmc->output_level += 2;
            }
        } else {
            if (dmc->output_level >= 2) {
                dmc->output_level -= 2;
            }
        }
        dmc->shift_register >>= 1;
        dmc->bits_remaining--;
    }
}

static inline void apu_clock_timers(APU *apu)
{
    for (int i = 0; i < 2; ++i) {
        APU_PulseChannel *pulse = &apu->pulse[i];
        if (pulse->timer_period < 8) {
            continue;
        }

        if (pulse->timer == 0) {
            pulse->timer = pulse->timer_period;
            pulse->duty_step = (uint8_t)((pulse->duty_step + 1) & 0x07);
        } else {
            pulse->timer--;
        }
    }

    if (apu->triangle.timer_period >= 2) {
        if (apu->triangle.timer == 0) {
            apu->triangle.timer = apu->triangle.timer_period;
            if (apu->triangle.length_counter > 0 && apu->triangle.linear_counter > 0) {
                apu->triangle.sequence_step = (uint8_t)((apu->triangle.sequence_step + 1) & 0x1F);
            }
        } else {
            apu->triangle.timer--;
        }
    }

    if (apu->noise.timer_period > 0) {
        if (apu->noise.timer == 0) {
            apu->noise.timer = apu->noise.timer_period;
            uint16_t tap = apu->noise.mode ? 6U : 1U;
            uint16_t feedback = (uint16_t)((apu->noise.shift_register & 0x0001U) ^ ((apu->noise.shift_register >> tap) & 0x0001U));
            apu->noise.shift_register >>= 1;
            apu->noise.shift_register |= (uint16_t)(feedback << 14);
        } else {
            apu->noise.timer--;
        }
    }

    apu_clock_dmc(apu);
}

static inline void apu_clock_frame_sequencer(APU *apu)
{
    if (apu->frame_mode_five_step) {
        switch (apu->frame_cycle) {
            case 3729:
            case 11186:
                apu_clock_quarter_frame(apu);
                break;
            case 7457:
            case 14916:
                apu_clock_quarter_frame(apu);
                apu_clock_half_frame(apu);
                break;
            case 18640:
                apu->frame_cycle = 0;
                break;
            default:
                break;
        }
    } else {
        switch (apu->frame_cycle) {
            case 3729:
            case 11186:
                apu_clock_quarter_frame(apu);
                break;
            case 7457:
                apu_clock_quarter_frame(apu);
                apu_clock_half_frame(apu);
                break;
            case 14916:
                apu_clock_quarter_frame(apu);
                apu_clock_half_frame(apu);
                if (!apu->frame_irq_inhibit) {
                    apu->frame_irq_flag = true;
                }
                apu->frame_cycle = 0;
                break;
            default:
                break;
        }
    }
}

APU *APU_Create(NES *nes)
{
    APU *apu = (APU *)calloc(1, sizeof(APU));
    if (!apu) {
        return NULL;
    }

    apu->nes = nes;
    apu->output_volume = 1.0f;
    APU_Reset(apu);
    return apu;
}

void APU_Destroy(APU *apu)
{
    if (!apu) {
        return;
    }

    free(apu);
}

void APU_Reset(APU *apu)
{
    if (!apu) {
        return;
    }

    NES *nes = apu->nes;
    float volume = apu->output_volume;

    memset(apu, 0, sizeof(*apu));

    apu->nes = nes;
    apu->output_volume = volume > 0.0f ? volume : 1.0f;
    apu->cycles_per_sample = 0.0;
    apu->sample_cycle_accumulator = 0.0;
    apu->frame_mode_five_step = false;
    apu->frame_irq_inhibit = false;
    apu->frame_irq_flag = false;

    if (apu->nes) {
        int sample_rate = apu->nes->settings.audio.sample_rate > 0 ? apu->nes->settings.audio.sample_rate : 44100;
        float cpu_rate = apu->nes->settings.timing.cpu_clock_rate > 0.0f ? apu->nes->settings.timing.cpu_clock_rate : 1789773.0f;
        apu->cycles_per_sample = (double)cpu_rate / (double)sample_rate;
        if (apu->nes->settings.audio.volume > 0.0f) {
            apu->output_volume *= apu->nes->settings.audio.volume;
        }
    }

    apu->pulse[0].duty = 0;
    apu->pulse[1].duty = 0;
    apu->noise.shift_register = 1;
    apu->dmc.output_level = 0;
}

void APU_CatchUp(APU *apu)
{
    if (!apu || !apu->nes || !apu->nes->cpu) return;
    if (apu->nes->cpu->total_cycles > apu->cpu_cycle_counter) {
        uint32_t diff = (uint32_t)(apu->nes->cpu->total_cycles - apu->cpu_cycle_counter);
        APU_Clock(apu, diff);
    }
}

void APU_SetVolume(APU *apu, float volume)
{
    if (!apu) {
        return;
    }

    apu->output_volume = clampf(volume, 0.0f, 1.0f);
}

size_t APU_GetBufferedSampleCount(APU *apu)
{
    if (!apu) {
        return 0;
    }

    return apu->sample_buffer_count;
}

size_t APU_ReadSamples(APU *apu, float *dst, size_t max_samples)
{
    if (!apu || !dst || max_samples == 0 || apu->sample_buffer_count == 0) {
        return 0;
    }

    size_t samples_read = 0;
    while (samples_read < max_samples && apu->sample_buffer_count > 0) {
        dst[samples_read++] = apu->sample_buffer[apu->sample_buffer_read_index];
        apu->sample_buffer_read_index = (apu->sample_buffer_read_index + 1) % APU_SAMPLE_BUFFER_CAPACITY;
        apu->sample_buffer_count--;
    }

    return samples_read;
}

void APU_ClearSampleBuffer(APU *apu)
{
    if (!apu) {
        return;
    }

    apu->sample_buffer_read_index = 0;
    apu->sample_buffer_write_index = 0;
    apu->sample_buffer_count = 0;
}

void APU_Clock(APU *apu, uint32_t cpu_cycles)
{
    if (!apu || cpu_cycles == 0) {
        return;
    }

    for (uint32_t i = 0; i < cpu_cycles; ++i) {
        apu->cpu_cycle_counter++;
        apu->frame_cycle++;
        apu_clock_timers(apu);
        apu_clock_frame_sequencer(apu);

        if (apu->cycles_per_sample > 0.0) {
            apu->sample_cycle_accumulator += 1.0;
            while (apu->sample_cycle_accumulator >= apu->cycles_per_sample) {
                apu_queue_sample(apu, apu_mix_output(apu));
                apu->sample_cycle_accumulator -= apu->cycles_per_sample;
            }
        }
    }
}

uint8_t APU_ReadRegister(APU *apu, uint16_t addr)
{
    APU_CatchUp(apu); // Ensure lazy runahead syncs APU immediately before read
    if (!apu) {
        return 0;
    }

    switch (addr & 0x001F) {
        case 0x0015: {
            uint8_t value = 0;
            if (apu->pulse[0].length_counter > 0) value |= 0x01;
            if (apu->pulse[1].length_counter > 0) value |= 0x02;
            if (apu->triangle.length_counter > 0) value |= 0x04;
            if (apu->noise.length_counter > 0) value |= 0x08;
            if (apu->dmc.bytes_remaining > 0) value |= 0x10;
            if (apu->frame_irq_flag) value |= 0x40;
            if (apu->dmc.irq_flag) value |= 0x80;
            apu->frame_irq_flag = false;
            apu->dmc.irq_flag = false;
            return value;
        }
        default:
            return 0;
    }
}

void APU_WriteRegister(APU *apu, uint16_t addr, uint8_t value)
{
    APU_CatchUp(apu); // Ensure lazy runahead syncs APU immediately before write
    if (!apu) {
        return;
    }

    switch (addr & 0x001F) {
        case 0x0000:
            apu->pulse[0].duty = (value >> 6) & 0x03;
            apu->pulse[0].length_halt = (value & 0x20) != 0;
            apu->pulse[0].constant_volume = (value & 0x10) != 0;
            apu->pulse[0].volume = value & 0x0F;
            apu->pulse[0].envelope_period = value & 0x0F;
            apu->pulse[0].envelope_start = true;
            break;
        case 0x0001:
            apu->pulse[0].sweep_enabled = (value & 0x80) != 0;
            apu->pulse[0].sweep_period = (uint8_t)((value >> 4) & 0x07);
            apu->pulse[0].sweep_negate = (value & 0x08) != 0;
            apu->pulse[0].sweep_shift = value & 0x07;
            apu->pulse[0].sweep_reload = true;
            break;
        case 0x0002:
            apu->pulse[0].timer_period = (apu->pulse[0].timer_period & 0x700) | value;
            break;
        case 0x0003:
            apu->pulse[0].timer_period = (apu->pulse[0].timer_period & 0x0FF) | ((uint16_t)(value & 0x07) << 8);
            apu->pulse[0].length_counter = length_table[value >> 3];
            apu->pulse[0].duty_step = 0;
            apu->pulse[0].envelope_start = true;
            apu->pulse[0].timer = apu->pulse[0].timer_period;
            break;
        case 0x0004:
            apu->pulse[1].duty = (value >> 6) & 0x03;
            apu->pulse[1].length_halt = (value & 0x20) != 0;
            apu->pulse[1].constant_volume = (value & 0x10) != 0;
            apu->pulse[1].volume = value & 0x0F;
            apu->pulse[1].envelope_period = value & 0x0F;
            apu->pulse[1].envelope_start = true;
            break;
        case 0x0005:
            apu->pulse[1].sweep_enabled = (value & 0x80) != 0;
            apu->pulse[1].sweep_period = (uint8_t)((value >> 4) & 0x07);
            apu->pulse[1].sweep_negate = (value & 0x08) != 0;
            apu->pulse[1].sweep_shift = value & 0x07;
            apu->pulse[1].sweep_reload = true;
            break;
        case 0x0006:
            apu->pulse[1].timer_period = (apu->pulse[1].timer_period & 0x700) | value;
            break;
        case 0x0007:
            apu->pulse[1].timer_period = (apu->pulse[1].timer_period & 0x0FF) | ((uint16_t)(value & 0x07) << 8);
            apu->pulse[1].length_counter = length_table[value >> 3];
            apu->pulse[1].duty_step = 0;
            apu->pulse[1].envelope_start = true;
            apu->pulse[1].timer = apu->pulse[1].timer_period;
            break;
        case 0x0008:
            apu->triangle.control_flag = (value & 0x80) != 0;
            apu->triangle.linear_counter_reload = value & 0x7F;
            break;
        case 0x000A:
            apu->triangle.timer_period = (apu->triangle.timer_period & 0x700) | value;
            break;
        case 0x000B:
            apu->triangle.timer_period = (apu->triangle.timer_period & 0x0FF) | ((uint16_t)(value & 0x07) << 8);
            apu->triangle.length_counter = length_table[value >> 3];
            apu->triangle.linear_reload_flag = true;
            apu->triangle.timer = apu->triangle.timer_period;
            apu->triangle.sequence_step = 0;
            break;
        case 0x000C:
            apu->noise.length_halt = (value & 0x20) != 0;
            apu->noise.constant_volume = (value & 0x10) != 0;
            apu->noise.volume = value & 0x0F;
            apu->noise.envelope_period = value & 0x0F;
            apu->noise.envelope_start = true;
            break;
        case 0x000E:
            apu->noise.mode = (value & 0x80) != 0;
            apu->noise.period_index = value & 0x0F;
            apu->noise.timer_period = noise_period_table[apu->noise.period_index];
            break;
        case 0x000F:
            apu->noise.length_counter = length_table[value >> 3];
            apu->noise.envelope_start = true;
            apu->noise.timer = apu->noise.timer_period;
            apu->noise.shift_register = 1;
            break;
        case 0x0010:
            apu->dmc.irq_enable = (value & 0x80) != 0;
            apu->dmc.loop = (value & 0x40) != 0;
            apu->dmc.rate_index = value & 0x0F;
            apu->dmc.timer_period = dmc_period_table[apu->dmc.rate_index];
            break;
        case 0x0011:
            apu->dmc.output_level = value & 0x7F;
            break;
        case 0x0012:
            apu->dmc.sample_address = (uint16_t)(0xC000 + ((uint16_t)value << 6));
            break;
        case 0x0013:
            apu->dmc.sample_length = (uint16_t)(((uint16_t)value << 4) + 1U);
            break;
        case 0x0015:
            apu->pulse[0].enabled = (value & 0x01) != 0;
            if (!apu->pulse[0].enabled) apu->pulse[0].length_counter = 0;
            apu->pulse[1].enabled = (value & 0x02) != 0;
            if (!apu->pulse[1].enabled) apu->pulse[1].length_counter = 0;
            apu->triangle.enabled = (value & 0x04) != 0;
            if (!apu->triangle.enabled) apu->triangle.length_counter = 0;
            apu->noise.enabled = (value & 0x08) != 0;
            if (!apu->noise.enabled) apu->noise.length_counter = 0;
            apu->dmc.enabled = (value & 0x10) != 0;
            if (apu->dmc.enabled) {
                if (apu->dmc.bytes_remaining == 0) {
                    apu_dmc_restart(&apu->dmc);
                }
            } else {
                apu->dmc.bytes_remaining = 0;
            }
            apu->dmc.irq_flag = false;
            break;
        case 0x0017:
            apu->frame_mode_five_step = (value & 0x80) != 0;
            apu->frame_irq_inhibit = (value & 0x40) != 0;
            apu->frame_cycle = 0;
            if (apu->frame_mode_five_step) {
                apu_clock_quarter_frame(apu);
                apu_clock_half_frame(apu);
            }
            apu->frame_irq_flag = false;
            break;
        default:
            break;
    }
}