#ifndef FRONTEND_THEME_H
#define FRONTEND_THEME_H

typedef enum mse_frontend_theme_e {
    MSE_FRONTEND_THEME_DARK = 0,
    MSE_FRONTEND_THEME_LIGHT,
    MSE_FRONTEND_THEME_CLASSIC,
    MSE_FRONTEND_THEME_MSE,
    MSE_FRONTEND_THEME_RETRO
} mse_frontend_theme_t;

const char *mse_frontend_theme_name(mse_frontend_theme_t theme);
mse_frontend_theme_t mse_frontend_theme_next(mse_frontend_theme_t theme);
void mse_frontend_theme_apply(mse_frontend_theme_t theme);

#endif // FRONTEND_THEME_H