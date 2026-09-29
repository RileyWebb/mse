#include <stdio.h>

#include "libmse/libmse.h"
#include "libmse/libmse_debug.h"

#include "frontend_app.h"
#include "frontend_args.h"

int main(int argc, char **argv)
{
	// Parsed before anything is initialised: --help has to answer without
	// standing up SDL, and the config file it may name is read at the very
	// top of mse_frontend_run.
	mse_frontend_args_t args;
	if (!mse_frontend_args_parse(argc, argv, &args)) {
		return 2;
	}
	if (args.should_exit) {
		return args.exit_code;
	}

	if (!libmse_init()) {
		//DEBUG_ERROR("Failed to initialize libmse");
		return 1;
	}

	libmse_log_register_file(stdout, true,
        #ifdef DEBUG
            true 
        #else
            true //false
        #endif
    );

	const mse_frontend_app_config_t config = {
		.title				= "Multi-System Emulator",
		.width				= args.width > 0 ? args.width : 1280,
		.height				= args.height > 0 ? args.height : 720,
		.resizable			= true,
		.high_pixel_density = true,
		.args				= &args,
	};

	return mse_frontend_run(&config);
}
