#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "debug.h"
#include "cNES/nes.h"
#include "cNES/cpu.h"
#include "cNES/bus.h"
#include "cNES/ppu.h"
#include "cNES/scripting/lua_api.h"

#ifdef USE_LUAJIT
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#define LUA_AVAILABLE 1
#else
// Forward declaration for Lua state
typedef void lua_State;
#define LUA_AVAILABLE 0
#endif

typedef struct LuaScript {
    lua_State* L;
    NES* nes;
    int onframe_ref;
    int should_exit;
    int exit_code;
    char error_buffer[256];
} LuaScript;

#if LUA_AVAILABLE

static LuaScript* lua_get_script(lua_State* L) {
    return (LuaScript*)lua_touserdata(L, lua_upvalueindex(1));
}

// Lua API: memory read byte
static int lua_mem_read(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (!script || !script->nes) {
        lua_pushinteger(L, 0);
        return 1;
    }
    
    uint16_t address = (uint16_t)luaL_checkinteger(L, 1);
    uint8_t value = BUS_Read(script->nes, address);
    lua_pushinteger(L, value);
    return 1;
}

// Lua API: memory write byte
static int lua_mem_write(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (!script || !script->nes) {
        return 0;
    }
    
    uint16_t address = (uint16_t)luaL_checkinteger(L, 1);
    uint8_t value = (uint8_t)luaL_checkinteger(L, 2);
    BUS_Write(script->nes, address, value);
    return 0;
}

// Lua API: read 16-bit value
static int lua_mem_read16(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (!script || !script->nes) {
        lua_pushinteger(L, 0);
        return 1;
    }
    
    uint16_t address = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t value = BUS_Read16(script->nes, address);
    lua_pushinteger(L, value);
    return 1;
}

// Lua API: exit emulator
static int lua_exit(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (script) {
        script->should_exit = 1;
    }
    return 0;
}

// Lua API: set process exit code
static int lua_set_exit_code(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (script) {
        script->exit_code = (int)luaL_checkinteger(L, 1);
    }
    return 0;
}

// Lua API: exit emulator with code
static int lua_exit_with_code(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (script) {
        script->exit_code = (int)luaL_checkinteger(L, 1);
        script->should_exit = 1;
    }
    return 0;
}

// Lua API: get CPU PC
static int lua_get_pc(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (!script || !script->nes || !script->nes->cpu) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, script->nes->cpu->pc);
    return 1;
}

// Lua API: get CPU A register
static int lua_get_a(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (!script || !script->nes || !script->nes->cpu) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, script->nes->cpu->a);
    return 1;
}

// Lua API: get CPU X register
static int lua_get_x(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (!script || !script->nes || !script->nes->cpu) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, script->nes->cpu->x);
    return 1;
}

// Lua API: get CPU Y register
static int lua_get_y(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (!script || !script->nes || !script->nes->cpu) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, script->nes->cpu->y);
    return 1;
}

// Lua API: get completed emulator frame count
static int lua_get_frame_count(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (!script || !script->nes) {
        lua_pushinteger(L, 0);
        return 1;
    }

    lua_pushinteger(L, (lua_Integer)NES_GetFrameCount(script->nes));
    return 1;
}

// Lua API: set CPU PC
static int lua_set_pc(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (script && script->nes && script->nes->cpu) {
        script->nes->cpu->pc = (uint16_t)luaL_checkinteger(L, 1);
    }
    return 0;
}

// Lua API: set CPU A register
static int lua_set_a(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (script && script->nes && script->nes->cpu) {
        script->nes->cpu->a = (uint8_t)luaL_checkinteger(L, 1);
    }
    return 0;
}

// Lua API: set CPU X register
static int lua_set_x(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (script && script->nes && script->nes->cpu) {
        script->nes->cpu->x = (uint8_t)luaL_checkinteger(L, 1);
    }
    return 0;
}

// Lua API: set CPU Y register
static int lua_set_y(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (script && script->nes && script->nes->cpu) {
        script->nes->cpu->y = (uint8_t)luaL_checkinteger(L, 1);
    }
    return 0;
}

// Lua API: set CPU SP register
static int lua_set_sp(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (script && script->nes && script->nes->cpu) {
        script->nes->cpu->sp = (uint8_t)luaL_checkinteger(L, 1);
    }
    return 0;
}

// Lua API: set CPU status register
static int lua_set_status(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (script && script->nes && script->nes->cpu) {
        script->nes->cpu->status = (uint8_t)luaL_checkinteger(L, 1);
    }
    return 0;
}

// Lua API: set full NES controller state bitfield
static int lua_set_controller_state(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (!script || !script->nes) {
        return 0;
    }

    int controller = (int)luaL_checkinteger(L, 1);
    int state = (int)luaL_checkinteger(L, 2);
    if (controller < 0 || controller > 1) {
        return 0;
    }

    NES_SetController(script->nes, controller, (uint8_t)state);
    return 0;
}

// Lua API: read current NES controller state bitfield
static int lua_get_controller_state(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (!script || !script->nes) {
        lua_pushinteger(L, 0);
        return 1;
    }

    int controller = (int)luaL_checkinteger(L, 1);
    if (controller < 0 || controller > 1) {
        lua_pushinteger(L, 0);
        return 1;
    }

    lua_pushinteger(L, NES_PollController(script->nes, controller));
    return 1;
}

// Lua API: set/clear a controller button bit
static int lua_set_controller_button(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (!script || !script->nes) {
        return 0;
    }

    int controller = (int)luaL_checkinteger(L, 1);
    int button_bit = (int)luaL_checkinteger(L, 2);
    int pressed = lua_toboolean(L, 3);
    if (controller < 0 || controller > 1) {
        return 0;
    }

    uint8_t state = NES_PollController(script->nes, controller);
    if (pressed) {
        state = (uint8_t)(state | (uint8_t)button_bit);
    } else {
        state = (uint8_t)(state & (uint8_t)(~button_bit));
    }

    NES_SetController(script->nes, controller, state);
    return 0;
}

// Lua API: read one tile byte from a nametable (0-3), offset 0-1023
static int lua_ppu_read_nametable(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (!script || !script->nes || !script->nes->ppu) {
        lua_pushinteger(L, 0);
        return 1;
    }

    int table_index = (int)luaL_checkinteger(L, 1);
    int offset = (int)luaL_checkinteger(L, 2);
    if (table_index < 0 || table_index > 3 || offset < 0 || offset > 1023) {
        lua_pushinteger(L, 0);
        return 1;
    }

    const uint8_t* table = PPU_GetNametable(script->nes->ppu, table_index);
    if (!table) {
        lua_pushinteger(L, 0);
        return 1;
    }

    lua_pushinteger(L, table[offset]);
    return 1;
}

// Lua API: read one pixel from the current PPU framebuffer (returns 32-bit ABGR)
static int lua_ppu_read_pixel(lua_State* L) {
    LuaScript* script = lua_get_script(L);
    if (!script || !script->nes || !script->nes->ppu || !script->nes->ppu->framebuffer) {
        lua_pushinteger(L, 0);
        return 1;
    }

    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    if (x < 0 || x >= PPU_FRAMEBUFFER_WIDTH || y < 0 || y >= PPU_FRAMEBUFFER_HEIGHT) {
        lua_pushinteger(L, 0);
        return 1;
    }

    uint32_t color = script->nes->ppu->framebuffer[y * PPU_FRAMEBUFFER_WIDTH + x];
    lua_pushinteger(L, (lua_Integer)color);
    return 1;
}

// Register Lua API functions
static void lua_register_api(lua_State* L, LuaScript* script) {
    #define REGISTER_LUA_API(name, fn) \
        lua_pushlightuserdata(L, script); \
        lua_pushcclosure(L, fn, 1); \
        lua_setglobal(L, name)

    // Memory functions
    REGISTER_LUA_API("mem_read", lua_mem_read);
    REGISTER_LUA_API("mem_write", lua_mem_write);
    REGISTER_LUA_API("mem_read16", lua_mem_read16);
    
    // Control functions
    REGISTER_LUA_API("exit", lua_exit);
    REGISTER_LUA_API("set_exit_code", lua_set_exit_code);
    REGISTER_LUA_API("exit_with_code", lua_exit_with_code);
    
    // CPU register functions
    REGISTER_LUA_API("get_pc", lua_get_pc);
    REGISTER_LUA_API("get_a", lua_get_a);
    REGISTER_LUA_API("get_x", lua_get_x);
    REGISTER_LUA_API("get_y", lua_get_y);
    REGISTER_LUA_API("get_frame_count", lua_get_frame_count);

    // CPU register setters
    REGISTER_LUA_API("set_pc", lua_set_pc);
    REGISTER_LUA_API("set_a", lua_set_a);
    REGISTER_LUA_API("set_x", lua_set_x);
    REGISTER_LUA_API("set_y", lua_set_y);
    REGISTER_LUA_API("set_sp", lua_set_sp);
    REGISTER_LUA_API("set_status", lua_set_status);

    // Controller helpers
    REGISTER_LUA_API("set_controller_state", lua_set_controller_state);
    REGISTER_LUA_API("get_controller_state", lua_get_controller_state);
    REGISTER_LUA_API("set_controller_button", lua_set_controller_button);

    // PPU helpers
    REGISTER_LUA_API("ppu_read_nametable", lua_ppu_read_nametable);
    REGISTER_LUA_API("ppu_read_pixel", lua_ppu_read_pixel);

    #undef REGISTER_LUA_API
}

#endif // LUA_AVAILABLE

LuaScript* LuaScript_Create(NES* nes) {
    LuaScript* script = (LuaScript*)malloc(sizeof(LuaScript));
    if (!script) return NULL;
    
    memset(script, 0, sizeof(LuaScript));
    script->nes = nes;
    script->onframe_ref = -1;
    script->should_exit = 0;
    script->exit_code = 0;
    
#if LUA_AVAILABLE
    script->L = luaL_newstate();
    if (!script->L) {
        free(script);
        return NULL;
    }
    luaL_openlibs(script->L);
    lua_register_api(script->L, script);
    DEBUG_INFO("Lua scripting initialized");
#else
    script->L = NULL;
    DEBUG_WARN("Lua scripting not compiled in (USE_LUAJIT not defined)");
#endif
    
    return script;
}

void LuaScript_Destroy(LuaScript* script) {
    if (!script) return;
    
#if LUA_AVAILABLE
    if (script->L) {
        lua_close(script->L);
    }
#endif
    
    free(script);
}

int LuaScript_LoadFile(LuaScript* script, const char* path) {
    if (!script || !path) return -1;
    
#if LUA_AVAILABLE
    if (!script->L) {
        snprintf(script->error_buffer, sizeof(script->error_buffer), "Lua state not initialized");
        return -1;
    }
    
    if (luaL_loadfile(script->L, path) != 0) {
        snprintf(script->error_buffer, sizeof(script->error_buffer), "Lua load error: %s",
                 lua_tostring(script->L, -1));
        lua_pop(script->L, 1);
        return -1;
    }
    
    if (lua_pcall(script->L, 0, 0, 0) != 0) {
        snprintf(script->error_buffer, sizeof(script->error_buffer), "Lua execution error: %s",
                 lua_tostring(script->L, -1));
        lua_pop(script->L, 1);
        return -1;
    }
    
    // Try to get the onframe function
    lua_getglobal(script->L, "onframe");
    if (lua_isfunction(script->L, -1)) {
        script->onframe_ref = luaL_ref(script->L, LUA_REGISTRYINDEX);
        DEBUG_INFO("Lua onframe function registered");
    } else {
        lua_pop(script->L, 1);
        script->onframe_ref = -1;
    }
    
    DEBUG_INFO("Lua script loaded: %s", path);
    return 0;
#else
    snprintf(script->error_buffer, sizeof(script->error_buffer), "Lua not available");
    return -1;
#endif
}

void LuaScript_OnFrame(LuaScript* script) {
    if (!script) return;
    
#if LUA_AVAILABLE
    if (!script->L || script->onframe_ref < 0) return;
    
    lua_rawgeti(script->L, LUA_REGISTRYINDEX, script->onframe_ref);
    if (lua_pcall(script->L, 0, 0, 0) != 0) {
        DEBUG_ERROR("Lua onframe error: %s", lua_tostring(script->L, -1));
        lua_pop(script->L, 1);
    }
#endif
}

void LuaScript_OnStart(LuaScript* script) {
    if (!script) return;

#if LUA_AVAILABLE
    if (!script->L) return;

    lua_getglobal(script->L, "onstart");
    if (!lua_isfunction(script->L, -1)) {
        lua_pop(script->L, 1);
        return;
    }

    if (lua_pcall(script->L, 0, 0, 0) != 0) {
        DEBUG_ERROR("Lua onstart error: %s", lua_tostring(script->L, -1));
        lua_pop(script->L, 1);
    }
#endif
}

int LuaScript_ShouldExit(LuaScript* script) {
    if (!script) return 0;
    return script->should_exit;
}

int LuaScript_GetExitCode(LuaScript* script) {
    if (!script) return 0;
    return script->exit_code;
}

const char* LuaScript_GetError(LuaScript* script) {
    if (!script) return "Invalid script pointer";
    return script->error_buffer[0] ? script->error_buffer : "No error";
}

int LuaScript_ExecuteString(LuaScript* script, const char* code) {
    if (!script || !code) return -1;
    
#if LUA_AVAILABLE
    if (!script->L) {
        snprintf(script->error_buffer, sizeof(script->error_buffer), "Lua state not initialized");
        return -1;
    }
    
    if (luaL_dostring(script->L, code) != 0) {
        snprintf(script->error_buffer, sizeof(script->error_buffer), "Lua execution error: %s",
                 lua_tostring(script->L, -1));
        lua_pop(script->L, 1);
        return -1;
    }
    
    // Check if they redefined onframe
    lua_getglobal(script->L, "onframe");
    if (lua_isfunction(script->L, -1)) {
        if (script->onframe_ref != -1) {
            luaL_unref(script->L, LUA_REGISTRYINDEX, script->onframe_ref);
        }
        script->onframe_ref = luaL_ref(script->L, LUA_REGISTRYINDEX);
        DEBUG_INFO("Lua onframe function registered from string");
    } else {
        lua_pop(script->L, 1);
    }

    DEBUG_INFO("Lua string executed.");
    return 0;
#else
    snprintf(script->error_buffer, sizeof(script->error_buffer), "Lua not available");
    return -1;
#endif
}

