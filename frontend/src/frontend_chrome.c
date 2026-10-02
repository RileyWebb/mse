#define DEBUG_LOG_SOURCE "frontend"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#elif !defined(__APPLE__)
#include <dlfcn.h>
#endif

#include "frontend_chrome.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frontend_app.h"
#include "frontend_cimgui.h"
#include "frontend_logo.h"
#include "frontend_theme.h"
#include "frontend_widgets.h"
#include "libmse/libmse_cvar.h"
#include "libmse/libmse_debug.h"

LIBMSE_CVAR_DEFINE_INT(g_cv_decoration, "mse_window_decoration", 1,
					   "Window decoration (0 = system, 1 = custom; a tiling window manager always gets the system's)");

// --- tiling window managers --------------------------------------------------
//
// Detected rather than configured, because the setting that says "custom" is
// usually right and only wrong on the machines running one of these -- and
// nobody wants to remember to flip it per machine.

// One list per platform, and the two must not be merged. Every Windows machine
// runs dwm.exe -- the Desktop Window Manager, Windows' compositor -- and a
// shared list that knows suckless dwm matched it, switching custom decoration
// off on every Windows install.
#if defined(_WIN32)
static const char *const k_tiling_wms[] = {
	"komorebi", "glazewm", "whim", "fancywm", "workspacer", NULL,
};
#else
static const char *const k_tiling_wms[] = {
	"dwm", "i3", "sway", "hyprland", "bspwm", "awesome", "xmonad", "herbstluftwm", "qtile", "leftwm",
	"spectrwm", "river", "niri", "dwl", "ratpoison", "stumpwm", NULL,
};
#endif

static bool g_tiling_checked = false;
static char g_tiling_wm[64]  = "";

// Matches one name -- a desktop session, a process, a window manager's own
// name -- against the list. A prefix counts, so i3-gaps is i3 and swayfx is
// sway.
static const char *chrome_match_tiler(const char *name)
{
	char lower[64];
	size_t n = 0;
	for (; name[n] != '\0' && n < sizeof(lower) - 1; ++n) {
		lower[n] = (char)tolower((unsigned char)name[n]);
	}
	lower[n] = '\0';

	for (size_t i = 0; k_tiling_wms[i] != NULL; ++i) {
		if (strncmp(lower, k_tiling_wms[i], strlen(k_tiling_wms[i])) == 0) {
			return k_tiling_wms[i];
		}
	}
#if !defined(_WIN32)
	// dwm is mostly run as a patched fork under a name of its own -- chadwm,
	// dwm-flexipatch -- so anything with it in the name counts too.
	if (strstr(lower, "dwm") != NULL) {
		return "dwm";
	}
#endif
	return NULL;
}

#if defined(_WIN32)

// Windows tilers are ordinary processes that move other programs' windows
// around, so the only way to know one is running is to look for it.
static const char *chrome_detect_tiler(void)
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	const char *found = NULL;
	PROCESSENTRY32W entry;
	entry.dwSize = sizeof(entry);

	for (BOOL more = Process32FirstW(snapshot, &entry); more && found == NULL;
		 more = Process32NextW(snapshot, &entry)) {
		// Executable names are ASCII in practice; anything else cannot be on
		// the list anyway.
		char name[64];
		size_t n = 0;
		for (; entry.szExeFile[n] != L'\0' && n < sizeof(name) - 1; ++n) {
			name[n] = entry.szExeFile[n] < 128 ? (char)entry.szExeFile[n] : '?';
		}
		name[n] = '\0';

		char *dot = strrchr(name, '.');
		if (dot != NULL) {
			*dot = '\0';
		}
		found = chrome_match_tiler(name);
	}

	CloseHandle(snapshot);
	return found;
}

#elif !defined(__APPLE__)

// Splits a list like XDG_CURRENT_DESKTOP's "sway:wlroots" and tries each part.
static const char *chrome_match_list(const char *list)
{
	if (list == NULL) {
		return NULL;
	}

	char copy[256];
	snprintf(copy, sizeof(copy), "%s", list);
	for (char *token = strtok(copy, ":;, "); token != NULL; token = strtok(NULL, ":;, ")) {
		const char *match = chrome_match_tiler(token);
		if (match != NULL) {
			return match;
		}
	}
	return NULL;
}

// The window manager's own name, as it publishes it on the root window under
// _NET_SUPPORTING_WM_CHECK. dwm sets nothing in the environment, so this is
// the only place it can be found. libX11 is loaded here rather than linked:
// SDL already loads it the same way, and a Wayland session need not have it.
static int chrome_x11_ignore_error(void *display, void *event)
{
	(void)display;
	(void)event;
	return 0;
}

static char *chrome_x11_wm_name(char *out, size_t out_size)
{
	typedef void *(*open_fn)(const char *);
	typedef int (*close_fn)(void *);
	typedef unsigned long (*root_fn)(void *);
	typedef unsigned long (*atom_fn)(void *, const char *, int);
	typedef int (*prop_fn)(void *, unsigned long, unsigned long, long, long, int, unsigned long, unsigned long *,
						   int *, unsigned long *, unsigned long *, unsigned char **);
	typedef int (*free_fn)(void *);
	typedef int (*handler_fn)(void *, void *);
	typedef handler_fn (*set_handler_fn)(handler_fn);
	typedef int (*sync_fn)(void *, int);

	if (getenv("DISPLAY") == NULL) {
		return NULL;
	}

	void *lib = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
	if (lib == NULL) {
		return NULL;
	}

	open_fn        x_open  = (open_fn)dlsym(lib, "XOpenDisplay");
	close_fn       x_close = (close_fn)dlsym(lib, "XCloseDisplay");
	root_fn        x_root  = (root_fn)dlsym(lib, "XDefaultRootWindow");
	atom_fn        x_atom  = (atom_fn)dlsym(lib, "XInternAtom");
	prop_fn        x_prop  = (prop_fn)dlsym(lib, "XGetWindowProperty");
	free_fn        x_free  = (free_fn)dlsym(lib, "XFree");
	set_handler_fn x_err   = (set_handler_fn)dlsym(lib, "XSetErrorHandler");
	sync_fn        x_sync  = (sync_fn)dlsym(lib, "XSync");

	char *result = NULL;
	void *display = NULL;
	if (x_open && x_close && x_root && x_atom && x_prop && x_free && x_err && x_sync) {
		display = x_open(NULL);
	}

	if (display != NULL) {
		// The check window can be stale -- a manager that exited without
		// clearing it -- and Xlib's default error handler exits the process
		// on the BadWindow that reading it would raise.
		handler_fn previous = x_err(chrome_x11_ignore_error);

		const unsigned long check = x_atom(display, "_NET_SUPPORTING_WM_CHECK", 0);
		const unsigned long wm_name = x_atom(display, "_NET_WM_NAME", 0);

		unsigned long  type = 0, items = 0, after = 0;
		int            format = 0;
		unsigned char *data = NULL;

		if (x_prop(display, x_root(display), check, 0, 1, 0, 0, &type, &format, &items, &after, &data) == 0 &&
			data != NULL && items == 1 && format == 32) {
			const unsigned long wm_window = *(unsigned long *)data;
			x_free(data);
			data = NULL;

			if (x_prop(display, wm_window, wm_name, 0, 64, 0, 0, &type, &format, &items, &after, &data) == 0 &&
				data != NULL && format == 8) {
				snprintf(out, out_size, "%.*s", (int)items, (const char *)data);
				result = out;
			}
		}
		if (data != NULL) {
			x_free(data);
		}

		x_sync(display, 0);
		x_err(previous);
		x_close(display);
	}

	dlclose(lib);
	return result;
}

static const char *chrome_detect_tiler(void)
{
	// Wayland compositors announce themselves through their IPC sockets.
	if (getenv("SWAYSOCK") != NULL) return "sway";
	if (getenv("HYPRLAND_INSTANCE_SIGNATURE") != NULL) return "hyprland";
	if (getenv("NIRI_SOCKET") != NULL) return "niri";
	if (getenv("I3SOCK") != NULL) return "i3";

	static const char *const session_vars[] = {"XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP", "DESKTOP_SESSION"};
	for (size_t i = 0; i < sizeof(session_vars) / sizeof(session_vars[0]); ++i) {
		const char *match = chrome_match_list(getenv(session_vars[i]));
		if (match != NULL) {
			return match;
		}
	}

	char name[64];
	if (chrome_x11_wm_name(name, sizeof(name)) != NULL) {
		return chrome_match_tiler(name);
	}
	return NULL;
}

#else

static const char *chrome_detect_tiler(void)
{
	return NULL;
}

#endif

const char *mse_frontend_chrome_tiling_wm(void)
{
	if (!g_tiling_checked) {
		g_tiling_checked = true;
		const char *found = chrome_detect_tiler();
		if (found != NULL) {
			snprintf(g_tiling_wm, sizeof(g_tiling_wm), "%s", found);
			DEBUG_INFO("Tiling window manager detected (%s); using the system's window decoration", found);
		}
	}
	return g_tiling_wm[0] != '\0' ? g_tiling_wm : NULL;
}

// --- mode --------------------------------------------------------------------

static SDL_Window *g_window         = NULL;
static bool        g_custom_applied = false;

static bool chrome_wants_custom(void)
{
	const int mode = (g_cv_decoration != NULL) ? *g_cv_decoration : 1;
	return mode == 1 && mse_frontend_chrome_tiling_wm() == NULL;
}

bool mse_frontend_chrome_wants_borderless(void)
{
	return chrome_wants_custom();
}

bool mse_frontend_chrome_active(void)
{
	return g_window != NULL && g_custom_applied && !(SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN);
}

// --- hit testing ---------------------------------------------------------------
//
// Called by SDL from inside its event processing, on the same thread as the
// frame loop, so it reads what the last frame recorded with no locking. The
// regions are in window coordinates, which are what ImGui's are once the main
// viewport's position is taken off.

typedef struct {
	float x0, y0, x1, y1;
} chrome_rect_t;

static chrome_rect_t g_drag[2];
static int           g_drag_count      = 0;
static bool          g_drag_seen       = false;
static bool          g_popup_open      = false;
static int           g_resize_border   = 5;

static bool chrome_rect_contains(const chrome_rect_t *r, float x, float y)
{
	return x >= r->x0 && x < r->x1 && y >= r->y0 && y < r->y1;
}

static SDL_HitTestResult SDLCALL chrome_hit_test(SDL_Window *window, const SDL_Point *point, void *data)
{
	(void)data;

	const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
	if (flags & SDL_WINDOW_FULLSCREEN) {
		return SDL_HITTEST_NORMAL;
	}

	if (!(flags & SDL_WINDOW_MAXIMIZED)) {
		int w = 0, h = 0;
		SDL_GetWindowSize(window, &w, &h);

		const int  b = g_resize_border;
		const int  c = b * 3; // corners get a longer grab than the straight edges
		const bool left = point->x < b, right = point->x >= w - b;
		const bool top = point->y < b, bottom = point->y >= h - b;
		const bool near_left = point->x < c, near_right = point->x >= w - c;
		const bool near_top = point->y < c, near_bottom = point->y >= h - c;

		if ((top && near_left) || (left && near_top)) return SDL_HITTEST_RESIZE_TOPLEFT;
		if ((top && near_right) || (right && near_top)) return SDL_HITTEST_RESIZE_TOPRIGHT;
		if ((bottom && near_left) || (left && near_bottom)) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
		if ((bottom && near_right) || (right && near_bottom)) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
		if (top) return SDL_HITTEST_RESIZE_TOP;
		if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
		if (left) return SDL_HITTEST_RESIZE_LEFT;
		if (right) return SDL_HITTEST_RESIZE_RIGHT;
	}

	// An open menu has to be able to close when the title bar is clicked, and
	// a click that starts a drag never reaches ImGui.
	if (!g_popup_open) {
		for (int i = 0; i < g_drag_count; ++i) {
			if (chrome_rect_contains(&g_drag[i], (float)point->x, (float)point->y)) {
				return SDL_HITTEST_DRAGGABLE;
			}
		}
	}
	return SDL_HITTEST_NORMAL;
}

static void chrome_apply(void)
{
	if (g_window == NULL) {
		return;
	}

	const bool custom = chrome_wants_custom();
	SDL_SetWindowBordered(g_window, !custom);
	if (!SDL_SetWindowHitTest(g_window, custom ? chrome_hit_test : NULL, NULL) && custom) {
		DEBUG_WARN("Window hit testing is unavailable (%s); the custom title bar cannot move the window",
				   SDL_GetError());
	}
	g_custom_applied = custom;
}

static void chrome_cvar_changed(libmse_cvar_t *cvar, void *user_data)
{
	(void)cvar;
	(void)user_data;
	chrome_apply();
}

void mse_frontend_chrome_attach(SDL_Window *window)
{
	g_window = window;
	chrome_apply();
	libmse_cvar_register_change_cb("mse_window_decoration", chrome_cvar_changed, NULL);
}

int mse_frontend_chrome_mode(void)
{
	return (g_cv_decoration != NULL) ? *g_cv_decoration : 1;
}

// --- drawing -----------------------------------------------------------------

static ImVec2 g_logo_end;

void mse_frontend_chrome_menu_bar_begin(void)
{
	if (!mse_frontend_chrome_active()) {
		return;
	}

	// The window's icon, where the system's title bar would have shown it.
	const ImVec2 bar_pos = igGetWindowPos();
	const float  bar_h   = igGetWindowHeight();
	const float  size    = floorf(bar_h * 0.62f);
	const ImVec2 cursor  = igGetCursorScreenPos();

	mse_frontend_logo_draw(igGetWindowDrawList(), (ImVec2){cursor.x, bar_pos.y + ((bar_h - size) * 0.5f)}, size);
	igDummy((ImVec2){size + mse_frontend_ui_px(4.0f), 1.0f});

	g_logo_end = (ImVec2){cursor.x + size, bar_pos.y + bar_h};
}

typedef enum { CHROME_MINIMISE, CHROME_MAXIMISE, CHROME_CLOSE } chrome_button_t;

static bool chrome_button(chrome_button_t kind, ImVec2 min, ImVec2 size, bool maximised)
{
	const mse_frontend_theme_tokens_t *t = mse_frontend_theme();

	static const char *const ids[] = {"##chrome_min", "##chrome_max", "##chrome_close"};
	igSetCursorScreenPos(min);
	const bool clicked = igInvisibleButton(ids[kind], size, 0);
	const bool hovered = igIsItemHovered(0);
	const bool held    = igIsItemActive();

	ImDrawList  *dl  = igGetWindowDrawList();
	const ImVec2 max = {min.x + size.x, min.y + size.y};

	ImVec4 glyph = t->text_muted;
	if (hovered || held) {
		if (kind == CHROME_CLOSE) {
			ImDrawList_AddRectFilled(dl, min, max, igGetColorU32_Vec4(mse_frontend_theme_alpha(t->danger, held ? 0.75f : 1.0f)),
									 0.0f, 0);
			glyph = (ImVec4){1.0f, 1.0f, 1.0f, 1.0f};
		} else {
			ImDrawList_AddRectFilled(dl, min, max, igGetColorU32_Vec4(held ? t->bg_active : t->bg_hover), 0.0f, 0);
			glyph = t->text;
		}
	}

	// Drawn rather than taken from the icon font, so all three share one
	// weight and sit on the same centre however the font is swapped.
	const ImU32 col  = igGetColorU32_Vec4(glyph);
	const float s    = floorf(mse_frontend_ui_px(10.0f));
	const float cx   = floorf(min.x + (size.x * 0.5f)) + 0.5f;
	const float cy   = floorf(min.y + (size.y * 0.5f)) + 0.5f;
	const float line = fmaxf(1.0f, floorf(mse_frontend_ui_px(1.0f)));
	const float h    = s * 0.5f;

	switch (kind) {
	case CHROME_MINIMISE:
		ImDrawList_AddLine(dl, (ImVec2){cx - h, cy}, (ImVec2){cx + h, cy}, col, line);
		break;
	case CHROME_MAXIMISE:
		if (maximised) {
			// Restore: a square with a second one showing behind it.
			const float o = floorf(s * 0.22f);
			ImDrawList_AddRect(dl, (ImVec2){cx - h, cy - h + o}, (ImVec2){cx + h - o, cy + h}, col, 0.0f, line, 0);
			ImDrawList_PathLineTo(dl, (ImVec2){cx - h + o, cy - h + o});
			ImDrawList_PathLineTo(dl, (ImVec2){cx - h + o, cy - h});
			ImDrawList_PathLineTo(dl, (ImVec2){cx + h, cy - h});
			ImDrawList_PathLineTo(dl, (ImVec2){cx + h, cy + h - o});
			ImDrawList_PathLineTo(dl, (ImVec2){cx + h - o, cy + h - o});
			ImDrawList_PathStroke(dl, col, line, 0);
		} else {
			ImDrawList_AddRect(dl, (ImVec2){cx - h, cy - h}, (ImVec2){cx + h, cy + h}, col, 0.0f, line, 0);
		}
		break;
	case CHROME_CLOSE:
		ImDrawList_AddLine(dl, (ImVec2){cx - h, cy - h}, (ImVec2){cx + h, cy + h}, col, line);
		ImDrawList_AddLine(dl, (ImVec2){cx - h, cy + h}, (ImVec2){cx + h, cy - h}, col, line);
		break;
	}

	return clicked;
}

void mse_frontend_chrome_menu_bar_end(void)
{
	if (!mse_frontend_chrome_active()) {
		return;
	}

	const mse_frontend_theme_tokens_t *t = mse_frontend_theme();

	const ImGuiViewport *vp        = igGetMainViewport();
	const ImVec2         bar_pos   = igGetWindowPos();
	const ImVec2         bar_size  = igGetWindowSize();
	const float          menus_end = igGetCursorScreenPos().x;
	const ImVec2         button    = {floorf(mse_frontend_ui_px(46.0f)), bar_size.y};
	const float          buttons_x = bar_pos.x + bar_size.x - (button.x * 3.0f);
	const bool           maximised = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MAXIMIZED) != 0;

	// The window's title, centred on the window, and only when it fits in the
	// space the menus leave -- sliding it sideways to fit would make it look
	// like one more menu.
	{
		// The cybercore look puts a status line here instead, with a block
		// cursor that blinks once a second.
		char        cyber_title[96];
		const char *title = SDL_GetWindowTitle(g_window);
		if (mse_frontend_ui_cyber()) {
			const bool on = ((int)(igGetTime() * 2.0)) % 2 == 0;
			snprintf(cyber_title, sizeof(cyber_title), "MSE // MULTI-SYSTEM EMULATOR %s", on ? "_" : " ");
			title = cyber_title;
		}
		const ImVec2 extent = igCalcTextSize(title, NULL, false, 0.0f);
		const float  x      = floorf(bar_pos.x + ((bar_size.x - extent.x) * 0.5f));
		const float  gap    = mse_frontend_ui_px(16.0f);
		if (x > menus_end + gap && x + extent.x < buttons_x - gap) {
			ImDrawList_AddText_Vec2(igGetWindowDrawList(), (ImVec2){x, floorf(bar_pos.y + ((bar_size.y - extent.y) * 0.5f))},
									igGetColorU32_Vec4(mse_frontend_ui_cyber() ? t->accent : t->text_faint), title, NULL);
		}
	}

	if (chrome_button(CHROME_MINIMISE, (ImVec2){buttons_x, bar_pos.y}, button, maximised)) {
		SDL_MinimizeWindow(g_window);
	}
	if (chrome_button(CHROME_MAXIMISE, (ImVec2){buttons_x + button.x, bar_pos.y}, button, maximised)) {
		if (maximised) {
			SDL_RestoreWindow(g_window);
		} else {
			SDL_MaximizeWindow(g_window);
		}
	}
	if (chrome_button(CHROME_CLOSE, (ImVec2){buttons_x + (button.x * 2.0f), bar_pos.y}, button, maximised)) {
		// The same way the system's close button would ask, so shutdown takes
		// the one path it always takes.
		SDL_Event quit;
		SDL_zero(quit);
		quit.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&quit);
	}

	// What drags the window: the empty stretch between the last menu and the
	// buttons, and the logo at the far left.
	const float ox = vp->Pos.x, oy = vp->Pos.y;
	g_drag[0] = (chrome_rect_t){menus_end - ox, bar_pos.y - oy, buttons_x - ox, bar_pos.y + bar_size.y - oy};
	g_drag[1] = (chrome_rect_t){bar_pos.x - ox, bar_pos.y - oy, g_logo_end.x - ox, g_logo_end.y - oy};
	g_drag_count = 2;
	g_drag_seen  = true;
}

void mse_frontend_chrome_draw_frame(void)
{
	// Whatever this frame drew is what the hit test answers from until the
	// next one; a frame without the bar -- a fullscreen game -- leaves no
	// drag area behind it.
	if (!g_drag_seen) {
		g_drag_count = 0;
	}
	g_drag_seen     = false;
	g_popup_open    = igIsPopupOpen_Str("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
	g_resize_border = (int)fmaxf(4.0f, floorf(mse_frontend_ui_px(5.0f)));

	if (!mse_frontend_chrome_active() || (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MAXIMIZED)) {
		return;
	}

	const ImGuiViewport *vp = igGetMainViewport();
	ImDrawList_AddRect(igGetForegroundDrawList_ViewportPtr((ImGuiViewport *)vp), vp->Pos,
					   (ImVec2){vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y},
					   igGetColorU32_Vec4(mse_frontend_theme()->border), 0.0f, 1.0f, 0);
}
