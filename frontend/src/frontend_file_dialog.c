#include "frontend_file_dialog.h"
#include <SDL3/SDL_dialog.h>
#include <stdio.h> 

// Context tracker passed internally to the SDL3 asynchronous engine
typedef struct {
    frontend_dialog_path_callback_t app_callback;
    void *userdata;
} mse_dialog_ctx_t;

// Internal SDL3 callback bridge
static void sdl3_dialog_callback_bridge(void *userdata, const char * const *filelist, int filter) {
    mse_dialog_ctx_t *ctx = (mse_dialog_ctx_t *)userdata;
    if (!ctx) return;

    if (filelist == NULL) {
        // An explicit internal subsystem error occurred
        SDL_Log("SDL3 Dialog System Error: %s", SDL_GetError());
        if (ctx->app_callback) ctx->app_callback(NULL, ctx->userdata);
    } 
    else if (*filelist == NULL) {
        // User cancelled the operation cleanly
        if (ctx->app_callback) ctx->app_callback(NULL, ctx->userdata);
    } 
    else {
        // Successfully retrieved path (grabbing the primary selection index)
        if (ctx->app_callback) ctx->app_callback(*filelist, ctx->userdata);
    }

    // Clean up contextual tracking state allocation
    SDL_free(ctx);
}

void frontend_file_dialog_open(SDL_Window *parent_window, frontend_dialog_path_callback_t app_callback, void *userdata) {
    mse_dialog_ctx_t *ctx = SDL_malloc(sizeof(mse_dialog_ctx_t));
    if (!ctx) return;
    
    ctx->app_callback = app_callback;
    ctx->userdata = userdata;

    // Define file type filtering rules
    static const SDL_DialogFileFilter filters[] = {
        { "Lua Script Files", "lua" },
        { "Text Configuration Files", "txt;json" },
        { "All Files", "*" }
    };

    // Parameters: callback, userdata, parent window, filters, filter_count, default_location, allow_many
    SDL_ShowOpenFileDialog(
        sdl3_dialog_callback_bridge, 
        ctx, 
        parent_window, 
        filters, 
        SDL_arraysize(filters), 
        NULL, 
        false
    );
}

void frontend_file_dialog_save(SDL_Window *parent_window, frontend_dialog_path_callback_t app_callback, void *userdata) {
    mse_dialog_ctx_t *ctx = SDL_malloc(sizeof(mse_dialog_ctx_t));
    if (!ctx) return;
    
    ctx->app_callback = app_callback;
    ctx->userdata = userdata;

    static const SDL_DialogFileFilter filters[] = {
        { "Lua Script Files", "lua" }
    };

    // Save dialog variants drop the 'allow_many' multiple flag natively
    SDL_ShowSaveFileDialog(
        sdl3_dialog_callback_bridge, 
        ctx, 
        parent_window, 
        filters, 
        SDL_arraysize(filters), 
        NULL
    );
}