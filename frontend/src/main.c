#include <stdio.h>

#include "libmse/libmse.h"
#include "libmse/libmse_debug.h"

#include "frontend_app.h"

int main(void)
{
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
		.width				= 1280,
		.height				= 720,
		.resizable			= true,
		.high_pixel_density = true,
	};

	return mse_frontend_run(&config);
}
