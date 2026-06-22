#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

#include "cNES/nes.h"
#include "cNES/rom.h"
#include "cNES/scripting/lua_api.h"

typedef struct HeadlessArgs {
    const char *rom_path;
    const char *script_path;
    int show_help;
} HeadlessArgs;

static int parse_args(int argc, char **argv, HeadlessArgs *args)
{
    memset(args, 0, sizeof(*args));

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            args->show_help = 1;
            return 0;
        }

        if (strcmp(argv[i], "--headless") == 0) {
            // Accepted for compatibility with existing test commands.
            continue;
        }

        if (strcmp(argv[i], "--script") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --script requires a path argument\n");
                return 1;
            }
            args->script_path = argv[++i];
            continue;
        }

        if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }

        if (!args->rom_path) {
            args->rom_path = argv[i];
        }
    }

    return 0;
}

static void print_help(void)
{
    printf("Usage: cNES_headless [options] <rom_path>\n");
    printf("Options:\n");
    printf("  -h, --help           Show this help message\n");
    printf("  --script <path>      Load and run Lua script at startup\n");
    printf("  --headless           Accepted for compatibility (no-op)\n");
}

int main(int argc, char **argv)
{
    HeadlessArgs args;

    if (parse_args(argc, argv, &args) != 0) {
        return 1;
    }

    if (args.show_help) {
        print_help();
        return 0;
    }

    if (!args.rom_path) {
        fprintf(stderr, "Error: ROM path is required.\n");
        return 1;
    }

    DEBUG_RegisterBuffer(stderr);

    NES *nes = NES_Create();
    if (!nes) {
        DEBUG_ERROR("Failed to create NES instance");
        return 1;
    }

    ROM *rom = ROM_LoadFile(args.rom_path);
    if (!rom) {
        DEBUG_ERROR("Failed to load ROM: %s", args.rom_path);
        NES_Destroy(nes);
        return 1;
    }

    if (NES_Load(nes, rom) != 0) {
        DEBUG_ERROR("Failed to load ROM into NES");
        NES_Destroy(nes);
        return 1;
    }

    LuaScript *lua = LuaScript_Create(nes);
    if (lua && args.script_path) {
        if (LuaScript_LoadFile(lua, args.script_path) != 0) {
            DEBUG_ERROR("Failed to load Lua script: %s", LuaScript_GetError(lua));
            LuaScript_Destroy(lua);
            NES_Destroy(nes);
            return 1;
        }
        LuaScript_OnStart(lua);
    }

    if (lua && args.script_path) {
        while (!LuaScript_ShouldExit(lua)) {
            NES_StepFrame(nes);
            LuaScript_OnFrame(lua);
        }
    } else {
        // No script: run one frame as a basic core smoke test.
        NES_StepFrame(nes);
    }

    int exit_code = 0;
    if (lua) {
        exit_code = LuaScript_GetExitCode(lua);
        LuaScript_Destroy(lua);
    }

    NES_Destroy(nes);
    return exit_code;
}
