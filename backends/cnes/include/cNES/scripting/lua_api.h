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

// Expose a script argument as a string field of the global ARGS table.
// Must be called before LuaScript_LoadFile so the script body can read it.
int LuaScript_SetArg(LuaScript* script, const char* key, const char* value);

// True if the script defines onrun(), meaning it drives emulation itself
int LuaScript_HasRun(LuaScript* script);

// Call the onrun() callback if it exists. The script is expected to run the
// console to completion itself using step()/run_frames() and then exit.
void LuaScript_OnRun(LuaScript* script);

// Check if emulator should exit (set by lua function)
int LuaScript_ShouldExit(LuaScript* script);

// Get process exit code set by script (default 0)
int LuaScript_GetExitCode(LuaScript* script);

// Get string error from last Lua operation
const char* LuaScript_GetError(LuaScript* script);

#endif // LUA_API_H
