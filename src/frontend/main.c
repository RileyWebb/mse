#include "frontend_app.h"

int main(void)
{
	const mse_frontend_app_config_t config = {
		.title				= "Multi-System Emulator",
		.width				= 1280,
		.height				= 720,
		.resizable			= true,
		.high_pixel_density = true,
	};

	return mse_frontend_run(&config);
}
