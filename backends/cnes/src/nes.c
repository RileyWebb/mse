#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libmse/libmse_debug.h"

#include "cNES/bus.h"
#include "cNES/cheats.h"
#include "cNES/apu.h"
#include "cNES/cpu.h"
#include "cNES/mapper.h"
#include "cNES/ppu.h"
#include "cNES/nes.h"
#include "cNES/rom.h"
#include "cNES/palette.h"
#include "cNES/loaders/ines.h"
#include "cNES/tas.h"

// Tear down whatever mapper was live, then bring up the one bus->mapper names.
// The core no longer knows which mapper numbers carry private state or how big
// it is -- that used to be a hardcoded `if (mapper == 1) malloc(...)` here.
static void NES_ResetMapperState(NES *nes)
{
	if (!nes || !nes->bus) {
		return;
	}

	BUS *bus = nes->bus;

	if (bus->mapper_info && bus->mapper_info->destroy) {
		bus->mapper_info->destroy(bus);
	}
	free(bus->mapper_data);
	bus->mapper_data = NULL;

	// Resolve the handler table once. The bus consults this pointer on every
	// cartridge access, so it must be non-NULL before anything touches $6000+
	// (CPU_Reset fetching the reset vector is the first thing that does).
	bus->mapper_info  = NES_Mapper_Get(bus->mapper);
	bus->irq_asserted = false;

	if (bus->mapper_info->init && !bus->mapper_info->init(bus)) {
		DEBUG_ERROR("Mapper %u failed to initialise; falling back to default behaviour.", bus->mapper);
		bus->mapper_info = NES_Mapper_Get(0xFFFF);
	}

	if (bus->mapper_info->reset) {
		bus->mapper_info->reset(bus);
	}
}

NES *NES_Create(void)
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
	// CPU_Create resets the CPU, which fetches $FFFC through the mapper.
	nes->bus->mapper_info = NES_Mapper_Get(nes->bus->mapper);

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

	// The movie player exists for the life of the console, empty until a file
	// is loaded into it. Optional: everything that touches it null-checks, so
	// a failed allocation costs playback rather than the emulator.
	nes->tas = TAS_Create();
	if (!nes->tas) {
		DEBUG_WARN("TAS player unavailable; movie playback is disabled");
	}

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

	if (nes->cpu) CPU_Destroy(nes->cpu);
	if (nes->apu) APU_Destroy(nes->apu);
	if (nes->ppu) PPU_Destroy(nes->ppu);
	if (nes->bus) {
		if (nes->bus->mapper_info && nes->bus->mapper_info->destroy) {
			nes->bus->mapper_info->destroy(nes->bus);
		}
		free(nes->bus->mapper_data);
		CHEAT_ReleaseBus(nes->bus);

		free(nes->bus);
	}
	if (nes->rom) ROM_Destroy(nes->rom);
	if (nes->tas) TAS_Destroy(nes->tas);

	free(nes);
}

int NES_Load(NES *nes, ROM *rom)
{
	if (!rom) {
		goto error_rom_load;
	}

	// Release the cartridge we were running before adopting the new one; the bus
	// holds raw pointers into rom->data, so nothing may outlive it silently.
	if (nes->rom && nes->rom != rom) {
		ROM_Destroy(nes->rom);
	}
	nes->rom = rom;

	uint16_t mapper_number = nes->rom->mapper_id;

	DEBUG_INFO("ROM '%s': mapper %u (%s)", rom->path, mapper_number, NES_Mapper_GetName(mapper_number));
	if (!NES_Mapper_IsSupported(mapper_number)) {
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

	// Cartridge RAM at $6000-$7FFF. iNES cannot say for certain whether a board
	// has it, so assume it does: an unused window costs 8KB, whereas guessing
	// the other way silently breaks every game that saves.
	nes->bus->prgRamPresent = true;
	nes->bus->prgRamBattery = nes->rom->has_battery;
	nes->bus->prgRamDirty   = false;
	memset(nes->bus->prgRam, 0, sizeof(nes->bus->prgRam));

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
	// Hand ownership back to the caller so its cleanup is the only one that runs.
	nes->rom			  = NULL;
	nes->bus->prgRomData  = NULL;
	nes->bus->prgRomDataSize = 0;
	nes->bus->chrRomData  = NULL;
	nes->bus->chrRomDataSize = 0;

error_rom_load:
	DEBUG_ERROR("Failed to load ROM: %s", rom && rom->path ? rom->path : "(unknown)");
	return -1; // Failure
}

void NES_Step(NES *nes)
{
	// Interrupt dispatch lives entirely in CPU_Step now. It used to be split
	// between here and there: this function serviced the PPU's NMI line and
	// cleared it, while CPU_Step separately latched the same line mid-instruction
	// and serviced it on its next call, so one vblank could vector twice. It also
	// had to infer whether an IRQ was actually taken by watching the cycle
	// counter move, which is no longer a usable signal now that every bus access
	// moves it.
	int cpu_cycles = CPU_Step(nes->cpu);
	if (cpu_cycles < 0) {
		DEBUG_ERROR("CPU execution halted due to error");
		return;
	}

	(void)cpu_cycles;

	// Components have already been synced at each register access; this brings
	// them up to the instruction boundary. There used to be a second, eager
	// execution path here selected by a setting, but it was unmaintained -- it
	// did not account for DMA-stolen cycles, so the two disagreed about how long
	// an instruction took.
	if (nes->ppu) {
		PPU_CatchUp(nes->ppu);
	}
	if (nes->apu) {
		APU_CatchUp(nes->apu);
	}
}

void NES_StepFrame(NES *nes)
{
	if (!TAS_IsFinished(nes->tas, nes))
		TAS_ApplyFrame(nes->tas, nes);

	// A reset parks the PPU at the start of the pre-render line, which this
	// numbering puts at the *end* of a frame: stepping straight to the next
	// frame boundary from there runs a single scanline and calls it a frame.
	// Long enough for a movie to consume an input row while the console does
	// nothing, which is a frame of desync in every .fm2 played back. Walk off
	// the pre-render line first so what follows is a whole frame. In steady
	// state the PPU sits at scanline 0 and this does nothing.
	while (nes->ppu->scanline == nes->ppu->scanline_prerender) {
		NES_Step(nes);
	}

	const uint64_t starting_frame = nes->ppu->frame_count;
	while (nes->ppu->frame_count == starting_frame) {
		NES_Step(nes);
	}
}

void NES_Reset(NES *nes)
{
	// 1. Initialize Bus Memory and Base State FIRST
	// Zero the master clock up front. PPU_Reset and APU_Reset rewind their own
	// counters to zero, and anything that lazily catches up between here and
	// CPU_Reset would otherwise be asked to run forward by the whole previous
	// session's worth of cycles.
	if (nes->cpu) {
		nes->cpu->total_cycles = 0;
		nes->cpu->stall_cycles = 0;
	}

	memset(nes->bus->ram, 0, sizeof(nes->bus->ram));
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

uint8_t *NES_GetCartridgeRam(NES *nes, size_t *size_out)
{
	if (!nes || !nes->bus || !nes->bus->prgRamPresent) {
		if (size_out) *size_out = 0;
		return NULL;
	}

	if (size_out) *size_out = sizeof(nes->bus->prgRam);
	return nes->bus->prgRam;
}

bool NES_CartridgeRamIsBatteryBacked(const NES *nes)
{
	return nes && nes->bus && nes->bus->prgRamPresent && nes->bus->prgRamBattery;
}

bool NES_CartridgeRamIsDirty(const NES *nes)
{
	return nes && nes->bus && nes->bus->prgRamDirty;
}

void NES_ClearCartridgeRamDirty(NES *nes)
{
	if (nes && nes->bus) {
		nes->bus->prgRamDirty = false;
	}
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

	// Delegate rather than copying into settings directly: the PPU keeps a
	// derived copy with the emphasis bits folded in, and writing the settings
	// behind its back left that stale, so a palette set this way had no visible
	// effect until something else happened to trigger a rebuild.
	if (nes->ppu) {
		PPU_SetPalette(nes->ppu, rgba_palette);
	} else {
		memcpy(nes->settings.video.palette, rgba_palette, sizeof(uint32_t) * 64);
	}
}
