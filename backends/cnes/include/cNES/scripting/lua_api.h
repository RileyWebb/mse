#ifndef LUA_API_H
#define LUA_API_H

#include <stdint.h>

typedef struct NES NES;
typedef struct LuaScript LuaScript;

// Create and initialize a Lua script context
LuaScript* LuaScript_Create(NES* nes);

// Destroy the Lua script context
void LuaScript_Destroy(LuaScript* script);

// Load and execute a Lua script file
// Returns 0 on success, non-zero on error
int LuaScript_LoadFile(LuaScript* script, const char* path);

// Execute a Lua string directly
// Returns 0 on success, non-zero on error
int LuaScript_ExecuteString(LuaScript* script, const char* code);

// Call the onframe() callback if it exists
// Called once per frame during emulation
void LuaScript_OnFrame(LuaScript* script);

// Call the onstart() callback if it exists
// Called once after loading script, before emulation starts
void LuaScript_OnStart(LuaScript* script);

// Check if emulator should exit (set by lua function)
int LuaScript_ShouldExit(LuaScript* script);

// Get process exit code set by script (default 0)
int LuaScript_GetExitCode(LuaScript* script);

// Get string error from last Lua operation
const char* LuaScript_GetError(LuaScript* script);

#endif // LUA_API_H
