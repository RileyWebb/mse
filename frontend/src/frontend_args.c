#define DEBUG_LOG_SOURCE "frontend"

#include "frontend_args.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libmse/libmse_version.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

// The app is built as a GUI binary, so it starts with no console attached and
// anything printed goes nowhere. This finds somewhere for --help to land.
//
// The redirect is checked before the console is: when output has been piped or
// sent to a file, writing to CONOUT$ instead would put the text on screen and
// leave the pipe empty, which is the opposite of what was asked for.
static void args_attach_console(void)
{
#if defined(_WIN32)
	static bool attached = false;
	if (attached) {
		return;
	}
	attached = true;

	const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
	if (handle != NULL && handle != INVALID_HANDLE_VALUE) {
		const DWORD type = GetFileType(handle) & ~FILE_TYPE_REMOTE;
		if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE) {
			return; // already going somewhere the caller chose
		}
	}

	if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
		return;
	}

	FILE *unused = NULL;
	freopen_s(&unused, "CONOUT$", "w", stdout);
	freopen_s(&unused, "CONOUT$", "w", stderr);
#endif
}

static void args_print(const char *fmt, ...)
{
	args_attach_console();

	va_list list;
	va_start(list, fmt);
	vfprintf(stdout, fmt, list);
	va_end(list);
	fflush(stdout);
}

void mse_frontend_args_print_usage(const char *program)
{
	const char *name = (program != NULL && program[0] != '\0') ? program : "mse";

	// Trimmed to the basename; the full path is noise in a usage line.
	const char *slash = strrchr(name, '/');
	const char *back  = strrchr(name, '\\');
	if (back > slash) slash = back;
	if (slash != NULL) name = slash + 1;

	args_print(
		"MSE - Multi-System Emulator (libmse %s)\n"
		"\n"
		"Usage: %s [options] [rom]\n"
		"\n"
		"Options:\n"
		"  -h, --help             Show this and exit\n"
		"  -v, --version          Print the version and exit\n"
		"  -r, --rom <file>       Load a ROM once the backend is up\n"
		"  -f, --fullscreen       Start fullscreen\n"
		"  -w, --width <px>       Window width (default 1280)\n"
		"  -H, --height <px>      Window height (default 720)\n"
		"  -s, --scale <factor>   Interface scale; overrides the display scale\n"
		"  -c, --config <file>    Console script to read at startup (default config.cfg)\n"
		"      --no-config        Skip the startup script entirely\n"
		"      --set <name=value> Set a cvar before startup; may be repeated\n"
		"  -e, --exec <file>      Run a console script after the config; may be repeated\n"
		"\n"
		"Anything settable in the console can be set here, so --set mse_theme=2 and\n"
		"typing \"set mse_theme 2\" do the same thing.\n",
		LIBMSE_VERSION_STRING, name);
}

static bool args_needs_value(const char *option, int index, int argc)
{
	if (index + 1 < argc) {
		return true;
	}
	args_print("mse: %s needs a value\n", option);
	return false;
}

static bool args_parse_int(const char *text, int *out)
{
	char *end = NULL;
	const long value = strtol(text, &end, 10);
	if (end == text || *end != '\0' || value <= 0 || value > 100000) {
		return false;
	}
	*out = (int)value;
	return true;
}

static bool args_parse_float(const char *text, float *out)
{
	char *end = NULL;
	const double value = strtod(text, &end);
	if (end == text || *end != '\0' || value <= 0.0 || value > 8.0) {
		return false;
	}
	*out = (float)value;
	return true;
}

static bool args_match(const char *arg, const char *short_form, const char *long_form)
{
	return (short_form != NULL && strcmp(arg, short_form) == 0) ||
		   (long_form != NULL && strcmp(arg, long_form) == 0);
}

bool mse_frontend_args_parse(int argc, char **argv, mse_frontend_args_t *out)
{
	if (out == NULL) {
		return false;
	}

	memset(out, 0, sizeof(*out));
	out->config_path = "config.cfg";

	const char *program = (argc > 0 && argv != NULL) ? argv[0] : "mse";

	for (int i = 1; i < argc; ++i) {
		const char *arg = argv[i];

		if (args_match(arg, "-h", "--help")) {
			mse_frontend_args_print_usage(program);
			out->should_exit = true;
			out->exit_code   = 0;
			return true;
		}

		if (args_match(arg, "-v", "--version")) {
			args_print("MSE, libmse %s (%s)\n", LIBMSE_VERSION_STRING, LIBMSE_VERSION_BUILD_STRING);
			out->should_exit = true;
			out->exit_code   = 0;
			return true;
		}

		if (args_match(arg, "-f", "--fullscreen")) {
			out->fullscreen = true;
			continue;
		}

		if (args_match(arg, NULL, "--no-config")) {
			out->config_path = NULL;
			continue;
		}

		if (args_match(arg, "-r", "--rom")) {
			if (!args_needs_value(arg, i, argc)) return false;
			out->rom_path = argv[++i];
			continue;
		}

		if (args_match(arg, "-c", "--config")) {
			if (!args_needs_value(arg, i, argc)) return false;
			out->config_path = argv[++i];
			continue;
		}

		if (args_match(arg, "-w", "--width")) {
			if (!args_needs_value(arg, i, argc)) return false;
			if (!args_parse_int(argv[++i], &out->width)) {
				args_print("mse: '%s' is not a usable width\n", argv[i]);
				return false;
			}
			continue;
		}

		if (args_match(arg, "-H", "--height")) {
			if (!args_needs_value(arg, i, argc)) return false;
			if (!args_parse_int(argv[++i], &out->height)) {
				args_print("mse: '%s' is not a usable height\n", argv[i]);
				return false;
			}
			continue;
		}

		if (args_match(arg, "-s", "--scale")) {
			if (!args_needs_value(arg, i, argc)) return false;
			if (!args_parse_float(argv[++i], &out->scale)) {
				args_print("mse: '%s' is not a usable scale\n", argv[i]);
				return false;
			}
			continue;
		}

		if (args_match(arg, NULL, "--set")) {
			if (!args_needs_value(arg, i, argc)) return false;
			if (out->set_count >= MSE_ARGS_MAX_SETS) {
				args_print("mse: too many --set options (limit %d)\n", MSE_ARGS_MAX_SETS);
				return false;
			}
			out->sets[out->set_count++] = argv[++i];
			continue;
		}

		if (args_match(arg, "-e", "--exec")) {
			if (!args_needs_value(arg, i, argc)) return false;
			if (out->exec_count >= MSE_ARGS_MAX_EXEC) {
				args_print("mse: too many --exec options (limit %d)\n", MSE_ARGS_MAX_EXEC);
				return false;
			}
			out->execs[out->exec_count++] = argv[++i];
			continue;
		}

		if (arg[0] == '-' && arg[1] != '\0') {
			args_print("mse: unknown option '%s'\n", arg);
			mse_frontend_args_print_usage(program);
			return false;
		}

		// The one positional: a ROM to open.
		if (out->rom_path != NULL) {
			args_print("mse: only one ROM can be given ('%s' was already)\n", out->rom_path);
			return false;
		}
		out->rom_path = arg;
	}

	return true;
}
