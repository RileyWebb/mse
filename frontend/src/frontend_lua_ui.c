// Host for Lua-defined UI panels.
//
// The frontend owns the ImGui context and the frame; Lua only draws into it.
// That keeps backends from having to link the renderer, and means a backend can
// ship its own debugger without the frontend knowing anything about it.
//
// The state is a non-threaded libmse Lua worker, so it shows up in the existing
// Lua Runtime Debugger alongside the others and inherits its package.path.
// Non-threaded matters: every call below has to happen on the UI thread,
// between NewFrame and Render.

#include <stdio.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "libmse/libmse.h"
#include "libmse/libmse_lua.h"
#include "libmse/libmse_debug.h"

#include "frontend_lua_ui.h"

// The Lua module holding the panel registry.
#define LUA_UI_MODULE "mse.ui"

static libmse_lua_worker_t *g_ui_worker = NULL;
static bool g_ui_failed = false;

static lua_State *lua_ui_state(void)
{
	return g_ui_worker ? g_ui_worker->L : NULL;
}

// Pushes mse.ui onto the stack. Returns false (pushing nothing) if unavailable.
static bool lua_ui_push_module(lua_State *L)
{
	lua_getglobal(L, "require");
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return false;
	}

	lua_pushstring(L, LUA_UI_MODULE);
	if (lua_pcall(L, 1, 1, 0) != 0) {
		DEBUG_ERROR("Lua UI: could not load %s: %s", LUA_UI_MODULE, lua_tostring(L, -1));
		lua_pop(L, 1);
		return false;
	}

	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return false;
	}

	return true;
}

// Calls a zero-argument, zero-result function on mse.ui.
static void lua_ui_call(const char *function)
{
	lua_State *L = lua_ui_state();
	if (L == NULL || g_ui_failed) {
		return;
	}

	int base = lua_gettop(L);

	if (!lua_ui_push_module(L)) {
		lua_settop(L, base);
		return;
	}

	lua_getfield(L, -1, function);
	if (!lua_isfunction(L, -1)) {
		lua_settop(L, base);
		return;
	}

	if (lua_pcall(L, 0, 0, 0) != 0) {
		// A panel that throws is caught inside mse.ui and disabled there, so
		// reaching here means the registry itself is broken. Stop calling in
		// rather than logging once per frame forever.
		DEBUG_ERROR("Lua UI: %s() failed, disabling Lua panels: %s",
		            function, lua_tostring(L, -1));
		g_ui_failed = true;
	}

	lua_settop(L, base);
}

bool mse_frontend_lua_ui_init(void)
{
	if (g_ui_worker != NULL) {
		return true;
	}

	g_ui_worker = libmse_lua_worker_create("Frontend UI (draws on the UI thread)", false);
	if (g_ui_worker == NULL || g_ui_worker->L == NULL) {
		DEBUG_ERROR("Lua UI: could not create the UI Lua state");
		return false;
	}

	lua_State *L = g_ui_worker->L;

	// Fail loudly here rather than on the first frame: if the generated
	// bindings are missing or do not match cimgui, nothing else will work.
	int base = lua_gettop(L);
	if (!lua_ui_push_module(L)) {
		DEBUG_ERROR("Lua UI: %s did not load; Lua panels are disabled", LUA_UI_MODULE);
		lua_settop(L, base);
		g_ui_failed = true;
		return false;
	}
	lua_settop(L, base);

	DEBUG_INFO("Lua UI ready");
	return true;
}

void mse_frontend_lua_ui_shutdown(void)
{
	if (g_ui_worker != NULL) {
		libmse_lua_worker_destroy(g_ui_worker);
		g_ui_worker = NULL;
	}
	g_ui_failed = false;
}

// Calls mse.ui.add_path(dir), which ignores directories already on the path.
static void lua_ui_add_path(const char *dir)
{
	lua_State *L = lua_ui_state();
	if (L == NULL || dir == NULL || dir[0] == '\0') {
		return;
	}

	int base = lua_gettop(L);

	if (lua_ui_push_module(L)) {
		lua_getfield(L, -1, "add_path");
		if (lua_isfunction(L, -1)) {
			lua_pushstring(L, dir);
			if (lua_pcall(L, 1, 0, 0) != 0) {
				DEBUG_WARN("Lua UI: add_path('%s') failed: %s", dir, lua_tostring(L, -1));
			}
		}
	}

	lua_settop(L, base);
}

// Backend scripts are run by path rather than required, so a script cannot
// require a sibling unless its directory is searchable. Registers the script's
// own directory and its parent, which is what lets "cnes/data/lua/ui/cpu.lua"
// do require("ui.debug").
static void lua_ui_register_script_dirs(const char *path)
{
	char dir[512];
	size_t len = strlen(path);
	if (len == 0 || len >= sizeof(dir)) {
		return;
	}

	memcpy(dir, path, len + 1);

	char *slash = strrchr(dir, '/');
	char *back = strrchr(dir, '\\');
	if (back != NULL && (slash == NULL || back > slash)) {
		slash = back;
	}
	if (slash == NULL) {
		return;
	}

	*slash = '\0';
	lua_ui_add_path(dir);

	// ...and the parent, so require("ui.debug") resolves as ui/debug.lua.
	slash = strrchr(dir, '/');
	back = strrchr(dir, '\\');
	if (back != NULL && (slash == NULL || back > slash)) {
		slash = back;
	}
	if (slash != NULL) {
		*slash = '\0';
		lua_ui_add_path(dir);
	}
}

bool mse_frontend_lua_ui_load_script(const char *path)
{
	if (path == NULL || g_ui_worker == NULL) {
		return false;
	}

	lua_ui_register_script_dirs(path);

	if (!libmse_lua_worker_execute_script(g_ui_worker, path)) {
		DEBUG_ERROR("Lua UI: failed to run '%s'", path);
		return false;
	}

	// A script that registers panels again after an earlier failure should get
	// another chance.
	g_ui_failed = false;
	return true;
}

void mse_frontend_lua_ui_load_backend(libmse_backend_t *backend)
{
	if (backend == NULL || backend->lua_libraries == NULL) {
		return;
	}

	if (!mse_frontend_lua_ui_init()) {
		return;
	}

	for (size_t i = 0; i < backend->lua_library_count; ++i) {
		const char *path = backend->lua_libraries[i];
		if (path == NULL) {
			continue;
		}

		if (mse_frontend_lua_ui_load_script(path)) {
			DEBUG_INFO("Lua UI: loaded '%s' for backend '%s'", path,
			           backend->info.name ? backend->info.name : "unnamed");
		}
	}
}

void mse_frontend_lua_ui_draw(void)
{
	lua_ui_call("draw");
}

void mse_frontend_lua_ui_draw_menu(void)
{
	lua_ui_call("menu");
}

size_t mse_frontend_lua_ui_panel_count(void)
{
	lua_State *L = lua_ui_state();
	if (L == NULL || g_ui_failed) {
		return 0;
	}

	int base = lua_gettop(L);
	size_t count = 0;

	if (lua_ui_push_module(L)) {
		lua_getfield(L, -1, "count");
		if (lua_isfunction(L, -1) && lua_pcall(L, 0, 1, 0) == 0) {
			count = (size_t)lua_tointeger(L, -1);
		}
	}

	lua_settop(L, base);
	return count;
}
