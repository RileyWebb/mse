#ifndef FRONTEND_FILE_DIALOG_H
#define FRONTEND_FILE_DIALOG_H

#include <SDL3/SDL.h>

// Callback signature matching your application's handling needs
typedef void (*frontend_dialog_path_callback_t)(const char *chosen_path, void *userdata);

void frontend_file_dialog_open(SDL_Window *parent_window, frontend_dialog_path_callback_t app_callback, void *userdata);
void frontend_file_dialog_save(SDL_Window *parent_window, frontend_dialog_path_callback_t app_callback, void *userdata);

#endif // FRONTEND_FILE_DIALOG_H