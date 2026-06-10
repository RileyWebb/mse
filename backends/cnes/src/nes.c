#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libmse/debug.h"

#include "cNES/bus.h"
#include "cNES/apu.h"
#include "cNES/cpu.h"
#include "cNES/mapper.h"
#include "cNES/ppu.h"
#include "cNES/nes.h"
#include "cNES/rom.h"
#include "cNES/palette.h"
#include "cNES/loaders/ines.h"

static void NES_ResetMapperState(NES *nes)
{
	if (!nes || !nes->bus) {
		return;
	}

	if (nes->bus->mapper_data) {
		free(nes->bus->mapper_data);
		nes->bus->mapper_data = NULL;
	}

	if (nes->bus->mapper == 1) {
		nes->bus->mapper_data = malloc(sizeof(Mapper1_State));
		if (!nes->bus->mapper_data) {
			DEBUG_ERROR("Failed to allocate MMC1 mapper state");
			return;
		}

		memset(nes->bus->mapper_data, 0, sizeof(Mapper1_State));
		Mapper1_State *mmc1_state  = (Mapper1_State *)nes->bus->mapper_data;
		mmc1_state->shift_register = 0x10;
		mmc1_state->control		   = 0x0C;
	}
}

NES *NES_Create()
{
	NES *nes = malloc(sizeof(NES));
	if (!nes) {
		goto error;
	}
	memset(nes, 0, sizeof(NES));

	nes->bus = malloc(sizeof(BUS));
	if (!nes->bus) {
		goto error;
	}
	memset(nes->bus, 0, sizeof(BUS));

	nes->cpu = CPU_Create(nes);
	if (!nes->cpu) {
		goto error;
	}

	nes->ppu = PPU_Create(nes);
	if (!nes->ppu) {
		goto error;
	}
	nes->bus->ppu = nes->ppu;

	nes->apu = APU_Create(nes);
	if (!nes->apu) {
		goto error;
	}

	memcpy(nes->settings.video.palette, PALETTE_default, sizeof(uint32_t) * 64);
	NES_SetRegionPreset(nes, NES_REGION_NTSC);
	nes->settings.audio.sample_rate = 44100;
	nes->settings.audio.volume		= 1.0f;
	nes->settings.frame_time		= 16.6392673398f;

	NES_Reset(nes);

	return nes;

error:
	NES_Destroy(nes);
	DEBUG_ERROR("Failed to create NES instance");

	return NULL;
}

void NES_Destroy(NES *nes)
{
	if (!nes) return;

	if (nes->cpu) free(nes->cpu);
	if (nes->apu) APU_Destroy(nes->apu);
	if (nes->ppu) PPU_Destroy(nes->ppu);
	if (nes->bus) {
		if (nes->bus->mapper_data) free(nes->bus->mapper_data);

		free(nes->bus);
	}
	if (nes->rom) ROM_Destroy(nes->rom);

	free(nes);
}

int NES_Load(NES *nes, ROM *rom)
{
	nes->rom = rom;
	if (!nes->rom) {
		goto error_rom_load;
	}

	NES_Reset(nes);

	uint8_t		   mapper_number = nes->rom->mapper_id;
	NES_MapperInfo mapper_info	 = NES_Mapper_Get(mapper_number);

	DEBUG_INFO("ROM '%s': mapper %u (%s)", rom->path, mapper_number, mapper_info.name);
	if (!mapper_info.supported) {
		DEBUG_WARN("Mapper %u is not implemented yet; ROM may fail or behave incorrectly.", mapper_number);
	}

	size_t prg_rom_size_bytes = nes->rom->prg_rom_size;
	size_t chr_rom_size_bytes = nes->rom->chr_rom_size;
	if (prg_rom_size_bytes == 0) {
		DEBUG_ERROR("ROM '%s': PRG ROM size is zero.", rom->path);
		goto error_after_rom_load;
	}

	nes->bus->prgRomData	 = nes->rom->data + nes->rom->prg_rom_offset;
	nes->bus->prgRomDataSize = prg_rom_size_bytes;
	nes->bus->prgBankSelect	 = 0;

	if (chr_rom_size_bytes > 0) {
		nes->bus->chrRomData	 = nes->rom->data + nes->rom->chr_rom_offset;
		nes->bus->chrRomDataSize = chr_rom_size_bytes;
		nes->bus->chrBankSelect	 = 0;
	} else {
		nes->bus->chrRomData	 = NULL;
		nes->bus->chrRomDataSize = 0;
		memset(nes->bus->chrRam, 0, sizeof(nes->bus->chrRam));
	}

	memset(nes->bus->vram, 0, sizeof(nes->bus->vram));
	memset(nes->bus->palette, 0, sizeof(nes->bus->palette));

	nes->bus->mapper = mapper_number;
	if (nes->rom->format == ROM_FORMAT_TNES) {
		nes->bus->mirroring = nes->rom->header[8] == 2 ? 1u : 0u;
	} else {
		nes->bus->mirroring = nes->rom->header[6] & 0x01;
	}
	nes->bus->prgRomSize = (uint8_t)((prg_rom_size_bytes + 0x3FFFu) / 0x4000u);
	nes->bus->chrRomSize = (uint8_t)((chr_rom_size_bytes + 0x1FFFu) / 0x2000u);

	NES_Reset(nes);
	return 0; // Success

error_after_rom_load:
error_rom_load:
	DEBUG_ERROR("Failed to load ROM: %s", rom && rom->path ? rom->path : "(unknown)");
	return -1; // Failure
}

void NES_Step(NES *nes)
{
	uint64_t total_cycles_before = nes->cpu->total_cycles;

	if (nes->apu && (nes->apu->frame_irq_flag || nes->apu->dmc.irq_flag)) {
		CPU_IRQ(nes->cpu);

		if (nes->cpu->total_cycles != total_cycles_before) {
			APU_Clock(nes->apu, 7);
			
            for (int i = 0; i < 7 * 3; i++)
				PPU_Step(nes->ppu);
			
            return;
		}
	}

	// Step the CPU

	int cpu_cycles = CPU_Step(nes->cpu);

	if (cpu_cycles == -1) {
		DEBUG_ERROR("CPU execution halted due to error");
	} else if (nes->apu) {
		APU_Clock(nes->apu, (uint32_t)cpu_cycles);
	}

	// Step the PPU three times for every CPU cycle (PPU runs 3x faster)
	for (int i = 0; i < cpu_cycles * 3; i++) 
		PPU_Step(nes->ppu);
	
	// Handle NMI if triggered by PPU
	if (nes->ppu->nmi_interrupt_line) {
		CPU_NMI(nes->cpu);

		nes->ppu->nmi_interrupt_line = false;

		// NMI takes 7 CPU cycles
		if (nes->apu) 
			APU_Clock(nes->apu, 7);

		for (int i = 0; i < 7 * 3; i++)
			PPU_Step(nes->ppu);
	}
	/*
    uint64_t total_cycles_before = nes->cpu->total_cycles;
    bool interrupt_serviced = false;

    // 1. NMI has the highest hardware priority
    if (nes->ppu && nes->ppu->nmi_interrupt_line) {
        CPU_NMI(nes->cpu);
        nes->ppu->nmi_interrupt_line = false;
        interrupt_serviced = true;
    } 
    // 2. IRQ has lower priority
    else if (nes->apu && (nes->apu->frame_irq_flag || nes->apu->dmc.irq_flag)) {
        CPU_IRQ(nes->cpu);
        
        // IRQs are maskable. We only consider it "serviced" if the CPU 
        // actually took the jump (i.e., the 'I' flag was clear).
        if (nes->cpu->total_cycles != total_cycles_before) {
            interrupt_serviced = true;
        }
    }

    // 3. Only execute a standard instruction if an interrupt didn't hijack the CPU
    if (!interrupt_serviced) {
        int cpu_cycles = CPU_Step(nes->cpu);
        if (cpu_cycles == -1) {
            DEBUG_ERROR("CPU execution halted due to error");
        }
    }
    
    // 4. Ensure unconditional lazy catch-up syncs components to the exact CPU cycle
    if (nes->ppu) {
        PPU_CatchUp(nes->ppu);
    }
    
    if (nes->apu) {
        APU_CatchUp(nes->apu);
    }*/
}

void NES_StepFrame(NES *nes)
{
	const int starting_frame = nes->ppu->frame_odd;
	while (nes->ppu->frame_odd == starting_frame) {
		NES_Step(nes);
	}
}

void NES_Reset(NES *nes)
{
	// 1. Initialize Bus Memory and Base State FIRST
	memset(nes->bus->memory, 0, sizeof(nes->bus->memory));
	nes->cpu_open_bus		= 0;
	nes->bus->prgBankSelect = 0;
	nes->bus->chrBankSelect = 0;

	nes->controllers[0] = 0;
	nes->controllers[1] = 0;

	// 2. Initialize the Mapper so memory reads (like the reset vector) route correctly
	NES_ResetMapperState(nes);

	// 3. Reset Subsystems (PPU/APU)
	PPU_Reset(nes->ppu);
	if (nes->apu) {
		APU_Reset(nes->apu);
	}

	if (nes->bus->mirroring) {
		PPU_SetMirroring(nes->ppu, MIRROR_VERTICAL);
	} else {
		PPU_SetMirroring(nes->ppu, MIRROR_HORIZONTAL);
	}

	// 4. Reset CPU LAST. It relies on the Bus and Mapper being fully alive to fetch $FFFC!
	CPU_Reset(nes->cpu);
}

uint64_t NES_GetFrameCount(NES *nes)
{
	if (!nes || !nes->ppu) {
		return 0;
	}

	return nes->ppu->frame_count;
}

uint8_t NES_PollController(NES *nes, int controller)
{
	return nes->controllers[controller];
}

void NES_SetController(NES *nes, int controller, uint8_t state)
{
	if (!nes) return;
	if (controller < 0 || controller > 1) return;
	nes->controllers[controller] = state;
}

void NES_SetRegionPreset(NES *nes, NES_Region region)
{
	if (!nes) return;

	nes->settings.region = region;

	switch (region) {
	case NES_REGION_NTSC:
		nes->settings.timing.scanlines_visible	 = 240;
		nes->settings.timing.scanline_vblank	 = 241;
		nes->settings.timing.scanline_prerender	 = 261;
		nes->settings.timing.cycles_per_scanline = 341;
		nes->settings.timing.cpu_clock_rate		 = 1789773.0f; // Hz
		break;

	case NES_REGION_PAL:
		nes->settings.timing.scanlines_visible	 = 240;
		nes->settings.timing.scanline_vblank	 = 241;
		nes->settings.timing.scanline_prerender	 = 311;
		nes->settings.timing.cycles_per_scanline = 341;
		nes->settings.timing.cpu_clock_rate		 = 1662607.0f; // Hz
		break;

	case NES_REGION_DENDY:
		nes->settings.timing.scanlines_visible	 = 240;
		nes->settings.timing.scanline_vblank	 = 291;
		nes->settings.timing.scanline_prerender	 = 311;
		nes->settings.timing.cycles_per_scanline = 341;
		nes->settings.timing.cpu_clock_rate		 = 1773448.0f; // Hz
		break;

	case NES_REGION_CUSTOM:
		break;
	}

	if (nes->apu) {
		double cpu_rate =
			nes->settings.timing.cpu_clock_rate > 0.0f ? (double)nes->settings.timing.cpu_clock_rate : 1789773.0;
		int sample_rate				= nes->settings.audio.sample_rate > 0 ? nes->settings.audio.sample_rate : 44100;
		nes->apu->cycles_per_sample = cpu_rate / (double)sample_rate;
	}
}

void NES_LoadPaletteRGBA(NES *nes, const uint32_t *rgba_palette)
{
	if (!nes || !rgba_palette) return;
	memcpy(nes->settings.video.palette, rgba_palette, sizeof(uint32_t) * 64);
}
