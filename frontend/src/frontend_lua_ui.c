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

#include <SDL3/SDL.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "libmse/libmse.h"
#include "libmse/libmse_lua.h"
#include "libmse/libmse_debug.h"

#include "frontend_lua_ui.h"

// The Lua modules holding the two registries: windows beside the game, and
// scripts that draw on the picture itself.
#define LUA_UI_MODULE      "mse.ui"
#define LUA_OVERLAY_MODULE "mse.overlay"

static libmse_lua_worker_t *g_ui_worker = NULL;
static bool g_ui_failed = false;

// Remembered so a reload can put back what was loaded. The backend's scripts
// are named by the backend, and the drop-in directories by whoever called in;
// neither is discoverable from the Lua state afterwards.
#define LUA_UI_MAX_DIRS 8
static libmse_backend_t *g_ui_backend = NULL;
static char				 g_ui_dirs[LUA_UI_MAX_DIRS][256];
static size_t			 g_ui_dir_count = 0;

static lua_State *lua_ui_state(void)
{
	return g_ui_worker ? g_ui_worker->L : NULL;
}

// Pushes a registry module onto the stack. Returns false (pushing nothing) if
// it is unavailable.
static bool lua_ui_push(lua_State *L, const char *module)
{
	lua_getglobal(L, "require");
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return false;
	}

	lua_pushstring(L, module);
	if (lua_pcall(L, 1, 1, 0) != 0) {
		DEBUG_ERROR("Lua UI: could not load %s: %s", module, lua_tostring(L, -1));
		lua_pop(L, 1);
		return false;
	}

	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return false;
	}

	return true;
}

static bool lua_ui_push_module(lua_State *L)
{
	return lua_ui_push(L, LUA_UI_MODULE);
}

// Calls a zero-argument, zero-result function on a registry module.
static void lua_ui_call_on(const char *module, const char *function)
{
	lua_State *L = lua_ui_state();
	if (L == NULL || g_ui_failed) {
		return;
	}

	int base = lua_gettop(L);

	if (!lua_ui_push(L, module)) {
		lua_settop(L, base);
		return;
	}

	lua_getfield(L, -1, function);
	if (!lua_isfunction(L, -1)) {
		lua_settop(L, base);
		return;
	}

	if (lua_pcall(L, 0, 0, 0) != 0) {
		// A panel that throws is caught inside the registry and disabled there,
		// so reaching here means the registry itself is broken. Stop calling in
		// rather than logging once per frame forever.
		DEBUG_ERROR("Lua UI: %s.%s() failed, disabling Lua panels: %s",
		            module, function, lua_tostring(L, -1));
		g_ui_failed = true;
	}

	lua_settop(L, base);
}

static void lua_ui_call(const char *function)
{
	lua_ui_call_on(LUA_UI_MODULE, function);
}

// Shared by the panel and overlay registries, which expose the same get/show
// pair. Returns false when the id is not registered, so a typo in a config says
// so rather than doing nothing quietly.
static bool lua_ui_show_in(const char *module, const char *id, bool visible)
{
	lua_State *L = lua_ui_state();
	if (L == NULL || g_ui_failed || id == NULL) {
		return false;
	}

	const int base = lua_gettop(L);
	bool found = false;

	if (lua_ui_push(L, module)) {
		lua_getfield(L, -1, "get");
		lua_pushstring(L, id);
		if (lua_isfunction(L, -2) && lua_pcall(L, 1, 1, 0) == 0) {
			found = !lua_isnil(L, -1);
		}
		lua_settop(L, base + 1);

		if (found) {
			lua_getfield(L, -1, "show");
			lua_pushstring(L, id);
			lua_pushboolean(L, visible ? 1 : 0);
			if (lua_isfunction(L, -3)) {
				lua_pcall(L, 2, 0, 0);
			}
		}
	}

	lua_settop(L, base);
	return found;
}

// Lists the overlays and whether each is on. Registered separately from
// "overlay" because that one needs an id and this one must not.
// Defined below, next to the reload it wraps.
static bool cmd_ui_reload_handler(int argc, const char **argv);

static bool cmd_overlays_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	lua_State *L = lua_ui_state();
	if (L == NULL || g_ui_failed) {
		libmse_log("mse: the Lua UI is not running");
		return false;
	}

	const int base = lua_gettop(L);

	if (lua_ui_push(L, LUA_OVERLAY_MODULE)) {
		lua_getfield(L, -1, "list");
		if (lua_isfunction(L, -1) && lua_pcall(L, 0, 1, 0) == 0 && lua_istable(L, -1)) {
			const int count = (int)lua_objlen(L, -1);
			if (count == 0) {
				libmse_log("mse: no overlays are loaded");
			}
			for (int i = 1; i <= count; ++i) {
				lua_rawgeti(L, -1, i);

				lua_getfield(L, -1, "id");
				const char *id = lua_tostring(L, -1);
				lua_getfield(L, -2, "title");
				const char *title = lua_tostring(L, -1);
				lua_getfield(L, -3, "enabled");
				const bool enabled = lua_toboolean(L, -1) != 0;
				lua_getfield(L, -4, "failed");
				const char *failed = lua_tostring(L, -1);

				libmse_logf("  [%s] %-28s %s%s",
				            enabled ? "x" : " ",
				            id != NULL ? id : "?",
				            title != NULL ? title : "",
				            failed != NULL ? "  (errored)" : "");

				lua_pop(L, 5);
			}
		}
	}

	lua_settop(L, base);
	return true;
}

static bool cmd_overlay_handler(int argc, const char **argv)
{
	if (argc < 1) {
		libmse_log("usage: overlay <id> [0|1]");
		return false;
	}

	const bool visible = (argc < 2) || (argv[1][0] != '0');

	if (!mse_frontend_lua_ui_show_overlay(argv[0], visible)) {
		libmse_logf("mse: no overlay '%s' (try \"overlays\")", argv[0]);
		return false;
	}
	return true;
}

static bool cmd_panel_handler(int argc, const char **argv)
{
	if (argc < 1) {
		libmse_log("usage: panel <id> [0|1]");
		return false;
	}

	// Absent second argument means show: opening a panel is what anyone typing
	// this is after, and "panel cnes.tas 0" is there for the other case.
	const bool visible = (argc < 2) || (argv[1][0] != '0');

	if (!mse_frontend_lua_ui_show_panel(argv[0], visible)) {
		libmse_logf("mse: no panel '%s'", argv[0]);
		return false;
	}
	return true;
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

	libmse_cmd_register(&(libmse_cmd_t){
		"panel", "Shows or hides a Lua panel by id, as the View menu does",
		1, cmd_panel_handler, "<id> [0|1]"});
	libmse_cmd_register(&(libmse_cmd_t){
		"overlay", "Turns a game overlay on or off by id", 1, cmd_overlay_handler, "<id> [0|1]"});
	libmse_cmd_register(&(libmse_cmd_t){
		"overlays", "Lists the loaded game overlays", 0, cmd_overlays_handler});
	libmse_cmd_register(&(libmse_cmd_t){
		"ui_reload", "Re-runs every Lua panel and overlay script from disk", 0, cmd_ui_reload_handler});

	DEBUG_INFO("Lua UI ready");
	return true;
}

bool mse_frontend_lua_ui_show_panel(const char *id, bool visible)
{
	return lua_ui_show_in(LUA_UI_MODULE, id, visible);
}

// Collects the ids a registry currently has switched on. Both registries answer
// list() with the same shape, which is what lets one function serve them.
#define LUA_UI_MAX_REMEMBERED 64

static size_t lua_ui_collect_enabled(const char *module, char out[][64], size_t max)
{
	lua_State *L = lua_ui_state();
	if (L == NULL || g_ui_failed) {
		return 0;
	}

	const int base	  = lua_gettop(L);
	size_t	  written = 0;

	if (lua_ui_push(L, module)) {
		lua_getfield(L, -1, "list");
		if (lua_isfunction(L, -1) && lua_pcall(L, 0, 1, 0) == 0 && lua_istable(L, -1)) {
			const int count = (int)lua_objlen(L, -1);
			for (int i = 1; i <= count && written < max; ++i) {
				lua_rawgeti(L, -1, i);

				lua_getfield(L, -1, "enabled");
				const bool enabled = lua_toboolean(L, -1) != 0;
				lua_pop(L, 1);

				if (enabled) {
					lua_getfield(L, -1, "id");
					const char *id = lua_tostring(L, -1);
					if (id != NULL) {
						snprintf(out[written++], 64, "%s", id);
					}
					lua_pop(L, 1);
				}

				lua_pop(L, 1);
			}
		}
	}

	lua_settop(L, base);
	return written;
}

bool mse_frontend_lua_ui_reload(void)
{
	// What was on, so a reload is not also a reset. Collected before the state
	// goes away, because that is where the answer lives.
	char   panels[LUA_UI_MAX_REMEMBERED][64];
	char   overlays[LUA_UI_MAX_REMEMBERED][64];
	size_t panel_count	 = lua_ui_collect_enabled(LUA_UI_MODULE, panels, LUA_UI_MAX_REMEMBERED);
	size_t overlay_count = lua_ui_collect_enabled(LUA_OVERLAY_MODULE, overlays, LUA_UI_MAX_REMEMBERED);

	// Torn down rather than re-run on top: a script that renamed a panel or
	// stopped registering one would otherwise leave the old one behind, and
	// the stale one is exactly what you reloaded to get rid of.
	mse_frontend_lua_ui_shutdown();
	g_ui_failed = false;

	if (!mse_frontend_lua_ui_init()) {
		DEBUG_ERROR("Lua UI: reload failed; no panels are loaded");
		return false;
	}

	if (g_ui_backend != NULL) {
		mse_frontend_lua_ui_load_backend(g_ui_backend);
	}

	// Copied first: loading walks the same array and would otherwise be
	// appending to what it is iterating.
	char   dirs[LUA_UI_MAX_DIRS][256];
	size_t dir_count = g_ui_dir_count;
	for (size_t i = 0; i < dir_count; ++i) {
		snprintf(dirs[i], sizeof(dirs[0]), "%s", g_ui_dirs[i]);
	}
	for (size_t i = 0; i < dir_count; ++i) {
		mse_frontend_lua_ui_load_directory(dirs[i]);
	}

	for (size_t i = 0; i < panel_count; ++i) {
		mse_frontend_lua_ui_show_panel(panels[i], true);
	}
	for (size_t i = 0; i < overlay_count; ++i) {
		mse_frontend_lua_ui_show_overlay(overlays[i], true);
	}

	libmse_logf("mse: reloaded the Lua UI (%zu panel(s), %zu overlay(s) still open)", panel_count,
				overlay_count);
	return true;
}

static bool cmd_ui_reload_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	return mse_frontend_lua_ui_reload();
}

bool mse_frontend_lua_ui_show_overlay(const char *id, bool visible)
{
	return lua_ui_show_in(LUA_OVERLAY_MODULE, id, visible);
}

void mse_frontend_lua_ui_draw_overlay_menu(void)
{
	lua_ui_call_on(LUA_OVERLAY_MODULE, "menu");
}

void mse_frontend_lua_ui_draw_overlays(const mse_frontend_overlay_view_t *view)
{
	lua_State *L = lua_ui_state();
	if (L == NULL || g_ui_failed || view == NULL) {
		return;
	}

	const int base = lua_gettop(L);

	if (lua_ui_push(L, LUA_OVERLAY_MODULE)) {
		lua_getfield(L, -1, "draw");
		if (lua_isfunction(L, -1)) {
			lua_pushnumber(L, view->x);
			lua_pushnumber(L, view->y);
			lua_pushnumber(L, view->width);
			lua_pushnumber(L, view->height);
			lua_pushinteger(L, view->pixels_x);
			lua_pushinteger(L, view->pixels_y);

			if (lua_pcall(L, 6, 0, 0) != 0) {
				// As above: an overlay that throws is caught and disabled by
				// the registry, so a failure here is the registry itself.
				DEBUG_ERROR("Lua UI: overlay.draw() failed, disabling Lua UI: %s",
				            lua_tostring(L, -1));
				g_ui_failed = true;
			}
		}
	}

	lua_settop(L, base);
}

size_t mse_frontend_lua_ui_overlay_count(void)
{
	lua_State *L = lua_ui_state();
	if (L == NULL || g_ui_failed) {
		return 0;
	}

	const int base = lua_gettop(L);
	size_t count = 0;

	if (lua_ui_push(L, LUA_OVERLAY_MODULE)) {
		lua_getfield(L, -1, "count");
		if (lua_isfunction(L, -1) && lua_pcall(L, 0, 1, 0) == 0) {
			count = (size_t)lua_tointeger(L, -1);
		}
	}

	lua_settop(L, base);
	return count;
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

size_t mse_frontend_lua_ui_load_directory(const char *dir)
{
	if (dir == NULL || !mse_frontend_lua_ui_init()) {
		return 0;
	}

	// Recorded before the scan, not after: a directory with nothing in it yet
	// is still one to look in again after a reload, which is the whole point of
	// being able to drop a script into it.
	bool known = false;
	for (size_t i = 0; i < g_ui_dir_count; ++i) {
		if (strcmp(g_ui_dirs[i], dir) == 0) {
			known = true;
			break;
		}
	}
	if (!known && g_ui_dir_count < LUA_UI_MAX_DIRS) {
		snprintf(g_ui_dirs[g_ui_dir_count++], sizeof(g_ui_dirs[0]), "%s", dir);
	}

	int count = 0;
	// Sorted, so a directory of overlays loads in a predictable order and one
	// script can rely on another having registered.
	char **names = SDL_GlobDirectory(dir, "*.lua", SDL_GLOB_CASEINSENSITIVE, &count);
	if (names == NULL) {
		// Not an error: the directory is optional, and an empty one is the
		// normal state until someone drops a script in.
		return 0;
	}

	size_t loaded = 0;
	for (int i = 0; i < count; ++i) {
		char path[512];
		snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
		if (mse_frontend_lua_ui_load_script(path)) {
			loaded++;
		}
	}

	SDL_free(names);

	if (loaded > 0) {
		DEBUG_INFO("Lua UI: loaded %zu script(s) from %s", loaded, dir);
	}
	return loaded;
}

void mse_frontend_lua_ui_load_backend(libmse_backend_t *backend)
{
	if (backend == NULL || backend->lua_libraries == NULL) {
		return;
	}

	if (!mse_frontend_lua_ui_init()) {
		return;
	}

	g_ui_backend = backend;

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
