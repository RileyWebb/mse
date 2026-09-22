// Headless cNES runner used by the CTest suite.
//
// A test is a Lua script that drives the console and reports a verdict by
// calling exit_with_code(). Two script shapes are supported:
//
//   onframe()  - the runner advances one frame at a time and calls back
//   onrun()    - the script drives emulation itself via step()/run_frames()
//
// Either way the process exit code is the verdict, so CTest never has to
// pattern-match on stdout.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libmse/libmse_debug.h"

#include "cNES/nes.h"
#include "cNES/rom.h"
#include "cNES/scripting/lua_api.h"

#define HEADLESS_MAX_ARGS 16

// A test that never reports a verdict is a failure, not a hang. The default
// budget is generous enough for the slowest AccuracyCoin page.
#define HEADLESS_DEFAULT_FRAME_BUDGET 36000

// Distinct from any verdict a script sets, so runner faults stay diagnosable.
#define HEADLESS_EXIT_USAGE   64
#define HEADLESS_EXIT_LOAD    65
#define HEADLESS_EXIT_TIMEOUT 66

typedef struct HeadlessArg {
    const char *key;
    const char *value;
} HeadlessArg;

typedef struct HeadlessArgs {
    const char *rom_path;
    const char *script_path;
    long        frame_budget;
    int         interpreter;
    int         show_help;
    HeadlessArg args[HEADLESS_MAX_ARGS];
    int         arg_count;
} HeadlessArgs;

static void print_help(void)
{
    printf("Usage: cNES_headless [options] <rom_path>\n");
    printf("Options:\n");
    printf("  -h, --help           Show this help message\n");
    printf("  --script <path>      Lua test script to run\n");
    printf("  --arg <key=value>    Pass a value to the script as ARGS.key (repeatable)\n");
    printf("  --frames <n>         Frame budget before the run is failed as a timeout\n");
    printf("                       (default %d, 0 disables the budget)\n", HEADLESS_DEFAULT_FRAME_BUDGET);
    printf("  --interpreter        Force the interpreter CPU core instead of the JIT\n");
    printf("  --headless           Accepted for compatibility (no-op)\n");
}

// Splits "key=value" in place. Returns 0 on success.
static int parse_script_arg(HeadlessArgs *args, char *pair)
{
    if (args->arg_count >= HEADLESS_MAX_ARGS) {
        fprintf(stderr, "Error: too many --arg values (max %d)\n", HEADLESS_MAX_ARGS);
        return 1;
    }

    char *separator = strchr(pair, '=');
    if (!separator || separator == pair) {
        fprintf(stderr, "Error: --arg expects key=value, got '%s'\n", pair);
        return 1;
    }

    *separator = '\0';
    args->args[args->arg_count].key   = pair;
    args->args[args->arg_count].value = separator + 1;
    args->arg_count++;
    return 0;
}

static int parse_args(int argc, char **argv, HeadlessArgs *args)
{
    memset(args, 0, sizeof(*args));
    args->frame_budget = HEADLESS_DEFAULT_FRAME_BUDGET;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            args->show_help = 1;
            return 0;
        }

        if (strcmp(argv[i], "--headless") == 0) {
            // Accepted for compatibility with existing test commands.
            continue;
        }

        if (strcmp(argv[i], "--interpreter") == 0) {
            args->interpreter = 1;
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

        if (strcmp(argv[i], "--arg") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --arg requires a key=value argument\n");
                return 1;
            }
            if (parse_script_arg(args, argv[++i]) != 0) {
                return 1;
            }
            continue;
        }

        if (strcmp(argv[i], "--frames") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --frames requires a count argument\n");
                return 1;
            }
            args->frame_budget = strtol(argv[++i], NULL, 10);
            if (args->frame_budget < 0) {
                fprintf(stderr, "Error: --frames must not be negative\n");
                return 1;
            }
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

int main(int argc, char **argv)
{
    HeadlessArgs args;

    // CTest kills a timed-out test, taking any buffered output with it.
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (parse_args(argc, argv, &args) != 0) {
        return HEADLESS_EXIT_USAGE;
    }

    if (args.show_help) {
        print_help();
        return 0;
    }

    if (!args.rom_path) {
        fprintf(stderr, "Error: ROM path is required.\n");
        print_help();
        return HEADLESS_EXIT_USAGE;
    }

    NES *nes = NES_Create();
    if (!nes) {
        fprintf(stderr, "Error: failed to create NES instance\n");
        return HEADLESS_EXIT_LOAD;
    }

    if (args.interpreter) {
    }

    ROM *rom = ROM_LoadFile(args.rom_path);
    if (!rom) {
        fprintf(stderr, "Error: failed to load ROM: %s\n", args.rom_path);
        NES_Destroy(nes);
        return HEADLESS_EXIT_LOAD;
    }

    if (NES_Load(nes, rom) != 0) {
        fprintf(stderr, "Error: failed to load ROM into NES: %s\n", args.rom_path);
        NES_Destroy(nes);
        return HEADLESS_EXIT_LOAD;
    }

    if (!args.script_path) {
        // No script: one frame as a bare core smoke test.
        NES_StepFrame(nes);
        NES_Destroy(nes);
        return 0;
    }

    LuaScript *lua = LuaScript_Create(nes);
    if (!lua) {
        fprintf(stderr, "Error: failed to create Lua context (built without LuaJIT?)\n");
        NES_Destroy(nes);
        return HEADLESS_EXIT_LOAD;
    }

    for (int i = 0; i < args.arg_count; ++i) {
        LuaScript_SetArg(lua, args.args[i].key, args.args[i].value);
    }

    if (LuaScript_LoadFile(lua, args.script_path) != 0) {
        fprintf(stderr, "Error: failed to load Lua script '%s': %s\n",
                args.script_path, LuaScript_GetError(lua));
        LuaScript_Destroy(lua);
        NES_Destroy(nes);
        return HEADLESS_EXIT_LOAD;
    }

    LuaScript_OnStart(lua);

    int timed_out = 0;

    if (LuaScript_HasRun(lua)) {
        // The script owns the emulation loop; its own guards bound the run.
        LuaScript_OnRun(lua);
    } else {
        long frames = 0;
        while (!LuaScript_ShouldExit(lua)) {
            if (args.frame_budget > 0 && frames >= args.frame_budget) {
                timed_out = 1;
                break;
            }
            NES_StepFrame(nes);
            LuaScript_OnFrame(lua);
            frames++;
        }

        if (timed_out) {
            fprintf(stderr,
                    "Error: script '%s' ran %ld frames without reporting a verdict\n",
                    args.script_path, frames);
        }
    }

    int exit_code = timed_out ? HEADLESS_EXIT_TIMEOUT : LuaScript_GetExitCode(lua);

    LuaScript_Destroy(lua);
    NES_Destroy(nes);
    return exit_code;
}
