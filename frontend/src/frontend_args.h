#ifndef MSE_FRONTEND_ARGS_H
#define MSE_FRONTEND_ARGS_H

#include <stdbool.h>
#include <stddef.h>

// Command line parsing.
//
// Kept apart from main() and from the app because it has to run before any of
// them: the config file it names is read at the very top of startup, and
// --help has to be answerable without touching SDL at all.

#define MSE_ARGS_MAX_SETS 32
#define MSE_ARGS_MAX_EXEC 8

typedef struct mse_frontend_args_s {
	// A ROM given positionally or with --rom, loaded once a backend is up.
	const char *rom_path;

	// The console script read at startup. Defaults to "config.cfg"; NULL when
	// --no-config was given.
	const char *config_path;

	// "name value" pairs from --set, applied before the config file so the
	// file cannot quietly override something asked for on the command line.
	const char *sets[MSE_ARGS_MAX_SETS];
	size_t      set_count;

	// Extra scripts from --exec, run after the config file.
	const char *execs[MSE_ARGS_MAX_EXEC];
	size_t      exec_count;

	int   width;      // 0 to leave the default
	int   height;     // 0 to leave the default
	float scale;      // 0 to take the display scale
	bool  fullscreen;

	// Set when parsing has already said everything it is going to say, and
	// the process should exit with `exit_code` rather than start up.
	bool should_exit;
	int  exit_code;
} mse_frontend_args_t;

// Fills `out` with defaults and applies argv. Returns false only when the
// arguments were malformed; `out->should_exit` covers --help and --version,
// which are not errors.
bool mse_frontend_args_parse(int argc, char **argv, mse_frontend_args_t *out);

// Writes the usage text. Safe to call before anything else is initialised.
void mse_frontend_args_print_usage(const char *program);

#endif // MSE_FRONTEND_ARGS_H
