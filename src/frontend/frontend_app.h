#ifndef MSE_FRONTEND_APP_H
#define MSE_FRONTEND_APP_H

#include <stdbool.h>

typedef struct mse_frontend_app_config_s {
    const char *title;
    int width;
    int height;
    bool resizable;
    bool high_pixel_density;
} mse_frontend_app_config_t;

int mse_frontend_run(const mse_frontend_app_config_t *config);

extern char g_video_resolutions_buf[64][64];
extern const char* g_video_resolutions[64];
extern int g_video_resolution_count;

extern char g_gpu_drivers_buf[16][64];
extern const char* g_gpu_drivers[16];
extern int g_gpu_driver_count;

extern unsigned int g_last_display_id;
extern bool g_gpu_drivers_cached;

#endif // MSE_FRONTEND_APP_H