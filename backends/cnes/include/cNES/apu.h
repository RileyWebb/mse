#ifndef APU_H
#define APU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct NES NES;

#define APU_SAMPLE_BUFFER_CAPACITY 8192

typedef struct APU_PulseChannel {
    bool enabled;
    bool length_halt;
    bool constant_volume;
    bool sweep_enabled;
    bool sweep_negate;
    uint8_t duty;
    uint8_t volume;
    uint8_t envelope_period;
    uint8_t envelope_divider;
    uint8_t envelope_decay;
    bool envelope_start;
    uint8_t sweep_period;
    uint8_t sweep_divider;
    uint8_t sweep_shift;
    bool sweep_reload;
    uint16_t timer_period;
    uint16_t timer;
    uint8_t length_counter;
    uint8_t duty_step;
} APU_PulseChannel;

typedef struct APU_TriangleChannel {
    bool enabled;
    bool control_flag;
    uint8_t length_counter;
    uint8_t linear_counter_reload;
    uint8_t linear_counter;
    bool linear_reload_flag;
    uint16_t timer_period;
    uint16_t timer;
    uint8_t sequence_step;
} APU_TriangleChannel;

typedef struct APU_NoiseChannel {
    bool enabled;
    bool length_halt;
    bool constant_volume;
    uint8_t volume;
    uint8_t envelope_period;
    uint8_t envelope_divider;
    uint8_t envelope_decay;
    bool envelope_start;
    bool mode;
    uint8_t period_index;
    uint16_t timer_period;
    uint16_t timer;
    uint16_t shift_register;
    uint8_t length_counter;
} APU_NoiseChannel;

typedef struct APU_DMCChannel {
    bool enabled;
    bool irq_enable;
    bool loop;
    bool irq_flag;
    uint8_t rate_index;
    uint16_t timer_period;
    uint16_t timer;
    uint8_t output_level;
    uint16_t sample_address;
    uint16_t current_address;
    uint16_t sample_length;
    uint16_t bytes_remaining;
    uint8_t bits_remaining;
    uint8_t shift_register;
    bool sample_buffer_empty;
    uint8_t sample_buffer;
} APU_DMCChannel;

typedef struct APU {
    NES *nes;

    float output_volume;
    uint64_t cpu_cycle_counter;
    uint32_t frame_cycle;
    bool frame_mode_five_step;
    bool frame_irq_inhibit;
    bool frame_irq_flag;

    double cycles_per_sample;
    double sample_cycle_accumulator;
    float sample_buffer[APU_SAMPLE_BUFFER_CAPACITY];
    size_t sample_buffer_read_index;
    size_t sample_buffer_write_index;
    size_t sample_buffer_count;

    APU_PulseChannel pulse[2];
    APU_TriangleChannel triangle;
    APU_NoiseChannel noise;
    APU_DMCChannel dmc;
} APU;

APU *APU_Create(NES *nes);
void APU_Destroy(APU *apu);
void APU_Reset(APU *apu);
void APU_Clock(APU *apu, uint32_t cpu_cycles);
void APU_CatchUp(APU *apu);
uint8_t APU_ReadRegister(APU *apu, uint16_t addr);
void APU_WriteRegister(APU *apu, uint16_t addr, uint8_t value);
void APU_SetVolume(APU *apu, float volume);
size_t APU_ReadSamples(APU *apu, float *dst, size_t max_samples);
size_t APU_GetBufferedSampleCount(APU *apu);
void APU_ClearSampleBuffer(APU *apu);

#endif // APU_H
