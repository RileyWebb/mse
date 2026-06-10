#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "libmse/libmse_cvar.h"

#define CVAR_INITIAL_CAPACITY 64
#define CMD_INITIAL_CAPACITY 32

static libmse_cvar_t** g_cvar_registry = NULL;
static size_t g_cvar_count = 0;
static size_t g_cvar_capacity = 0;

static libmse_command_t** g_cmd_registry = NULL;
static size_t g_cmd_count = 0;
static size_t g_cmd_capacity = 0;

// Helper functions to find indices
static int find_cvar_index(const char* name) {
    if (!name || !g_cvar_registry) return -1;
    for (size_t i = 0; i < g_cvar_count; i++) {
        if (g_cvar_registry[i] && strcmp(g_cvar_registry[i]->name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int find_cmd_index(const char* name) {
    if (!name || !g_cmd_registry) return -1;
    for (size_t i = 0; i < g_cmd_count; i++) {
        if (g_cmd_registry[i] && strcmp(g_cmd_registry[i]->name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int find_cvar_index_reversed(const char* name) {
    if (!name || !g_cvar_registry) return -1;
    for (size_t i = g_cvar_count; i > 0; i--) {
        if (g_cvar_registry[i - 1] && strcmp(g_cvar_registry[i - 1]->name, name) == 0) {
            return (int)(i - 1);
        }
    }
    return -1;
}

LIBMSE_API bool libmse_cvar_register(const char* name, libmse_cvar_type_t type, void* ref, const char* description) {
    if (!name || !ref) return false; // Reference must be provided (Memory managed elsewhere)

    if (find_cvar_index(name) != -1)
        return false;

    if (g_cvar_count >= g_cvar_capacity) {
        size_t new_capacity = (g_cvar_capacity == 0) ? CVAR_INITIAL_CAPACITY : g_cvar_capacity * 2;
        libmse_cvar_t** new_registry = (libmse_cvar_t**)realloc(g_cvar_registry, new_capacity * sizeof(libmse_cvar_t*));
        if (!new_registry) return false;
        
        g_cvar_registry = new_registry;
        g_cvar_capacity = new_capacity;
    }

    libmse_cvar_t* cvar = (libmse_cvar_t*)malloc(sizeof(libmse_cvar_t));
    if (!cvar) return false;

    cvar->name = strdup(name);
    cvar->description = description ? strdup(description) : NULL;
    cvar->type = type;
    cvar->alloc_s = NULL;
    memset(&cvar->data, 0, sizeof(cvar->data));

    switch (type) {
        case LIBMSE_CVAR_INT:    cvar->data.i = (int*)ref; break;
        case LIBMSE_CVAR_FLOAT:  cvar->data.f = (float*)ref; break;
        case LIBMSE_CVAR_DOUBLE: cvar->data.d = (double*)ref; break;
        case LIBMSE_CVAR_STRING: cvar->data.s = (const char**)ref; break;
    }

    g_cvar_registry[g_cvar_count++] = cvar;
    return true;
}

LIBMSE_API bool libmse_cvar_destroy(const char* name) {
    int index = find_cvar_index(name);
    if (index == -1) return false;

    libmse_cvar_t* cvar = g_cvar_registry[index];
    free((void*)cvar->name);
    if (cvar->description) free((void*)cvar->description);
    if (cvar->alloc_s) free(cvar->alloc_s); // Only free memory allocated via console string overrides
    free(cvar);

    for (size_t i = (size_t)index; i < g_cvar_count - 1; i++) {
        g_cvar_registry[i] = g_cvar_registry[i + 1];
    }
    g_cvar_count--;
    g_cvar_registry[g_cvar_count] = NULL;
    return true;
}

LIBMSE_API void libmse_cvar_iterate(libmse_cvar_iterate_cb callback, void* user_data) {
    if (!callback) return;
    for (size_t i = 0; i < g_cvar_count; i++) {
        callback(g_cvar_registry[i], user_data);
    }
}

LIBMSE_API libmse_cvar_t* libmse_cvar_get(const char* name) {
    int index = find_cvar_index(name);
    if (index == -1) return NULL;
    return g_cvar_registry[index];
}

LIBMSE_API libmse_cvar_t* libmse_cvar_get_reversed(const char* name) {
    int index = find_cvar_index_reversed(name);
    if (index == -1) return NULL;
    return g_cvar_registry[index];
}

LIBMSE_API void* libmse_cvar_get_ptr(const char* name) {
    libmse_cvar_t* cvar = libmse_cvar_get(name);
    return cvar ? &cvar->data : NULL;
}

LIBMSE_API int* libmse_cvar_get_i(const char* name) {
    libmse_cvar_t* cvar = libmse_cvar_get(name);
    return (cvar && cvar->type == LIBMSE_CVAR_INT) ? cvar->data.i : NULL;
}

LIBMSE_API float* libmse_cvar_get_f(const char* name) {
    libmse_cvar_t* cvar = libmse_cvar_get(name);
    return (cvar && cvar->type == LIBMSE_CVAR_FLOAT) ? cvar->data.f : NULL;
}

LIBMSE_API double* libmse_cvar_get_d(const char* name) {
    libmse_cvar_t* cvar = libmse_cvar_get(name);
    return (cvar && cvar->type == LIBMSE_CVAR_DOUBLE) ? cvar->data.d : NULL;
}

LIBMSE_API const char** libmse_cvar_get_s(const char* name) {
    libmse_cvar_t* cvar = libmse_cvar_get(name);
    return (cvar && cvar->type == LIBMSE_CVAR_STRING) ? cvar->data.s : NULL;
}

LIBMSE_API bool libmse_cvar_set_i(const char* name, int value) {
    libmse_cvar_t* cvar = libmse_cvar_get(name);
    if (!cvar || cvar->type != LIBMSE_CVAR_INT) return false;
    *cvar->data.i = value;
    return true;
}

LIBMSE_API bool libmse_cvar_set_f(const char* name, float value) {
    libmse_cvar_t* cvar = libmse_cvar_get(name);
    if (!cvar || cvar->type != LIBMSE_CVAR_FLOAT) return false;
    *cvar->data.f = value;
    return true;
}

LIBMSE_API bool libmse_cvar_set_d(const char* name, double value) {
    libmse_cvar_t* cvar = libmse_cvar_get(name);
    if (!cvar || cvar->type != LIBMSE_CVAR_DOUBLE) return false;
    *cvar->data.d = value;
    return true;
}

LIBMSE_API bool libmse_cvar_set_s(const char* name, const char* value) {
    libmse_cvar_t* cvar = libmse_cvar_get(name);
    if (!cvar || cvar->type != LIBMSE_CVAR_STRING) return false;
    
    if (cvar->alloc_s) free(cvar->alloc_s);
    cvar->alloc_s = strdup(value);
    *cvar->data.s = cvar->alloc_s;
    return true;
}


// ---------------------------------------------------------
// COMMAND IMPLEMENTATION
// ---------------------------------------------------------

LIBMSE_API bool libmse_command_register(const libmse_command_t* cmd) {
    if (!cmd || !cmd->name || !cmd->handler) return false;

    if (find_cmd_index(cmd->name) != -1) return false;

    if (g_cmd_count >= g_cmd_capacity) {
        size_t new_capacity = (g_cmd_capacity == 0) ? CMD_INITIAL_CAPACITY : g_cmd_capacity * 2;
        libmse_command_t** new_registry = (libmse_command_t**)realloc(g_cmd_registry, new_capacity * sizeof(libmse_command_t*));
        if (!new_registry) return false;
        
        g_cmd_registry = new_registry;
        g_cmd_capacity = new_capacity;
    }

    libmse_command_t* new_cmd = (libmse_command_t*)malloc(sizeof(libmse_command_t));
    if (!new_cmd) return false;

    // Deep copy struct variables safely
    new_cmd->name = strdup(cmd->name);
    new_cmd->description = cmd->description ? strdup(cmd->description) : NULL;
    new_cmd->expected_args_count = cmd->expected_args_count;
    
    if (cmd->expected_args_count > 0 && cmd->expected_types) {
        libmse_cvar_type_t* types = (libmse_cvar_type_t*)malloc(cmd->expected_args_count * sizeof(libmse_cvar_type_t));
        memcpy(types, cmd->expected_types, cmd->expected_args_count * sizeof(libmse_cvar_type_t));
        new_cmd->expected_types = types;
    } else {
        new_cmd->expected_types = NULL;
    }

    new_cmd->handler = cmd->handler;

    g_cmd_registry[g_cmd_count++] = new_cmd;
    return true;
}

LIBMSE_API bool libmse_command_destroy(const char* name) {
    int index = find_cmd_index(name);
    if (index == -1) return false;

    libmse_command_t* cmd = g_cmd_registry[index];
    free((void*)cmd->name);
    if (cmd->description) free((void*)cmd->description);
    if (cmd->expected_types) free((void*)cmd->expected_types);
    free(cmd);

    for (size_t i = (size_t)index; i < g_cmd_count - 1; i++) {
        g_cmd_registry[i] = g_cmd_registry[i + 1];
    }
    g_cmd_count--;
    g_cmd_registry[g_cmd_count] = NULL;
    return true;
}

LIBMSE_API void libmse_command_iterate(libmse_command_iterate_cb callback, void* user_data) {
    if (!callback) return;
    for (size_t i = 0; i < g_cmd_count; i++) {
        callback(g_cmd_registry[i], user_data);
    }
}

LIBMSE_API libmse_command_t* libmse_command_get(const char* name) {
    int index = find_cmd_index(name);
    if (index == -1) return NULL;
    return g_cmd_registry[index];
}

LIBMSE_API bool libmse_command_execute(const char* name, int argc, const char** argv) {
    libmse_command_t* cmd = libmse_command_get(name);
    if (!cmd) return false;

    if ((size_t)argc < cmd->expected_args_count) return false;

    libmse_cmd_arg_t parsed_args[16];
    size_t limit = cmd->expected_args_count > 16 ? 16 : cmd->expected_args_count;

    for (size_t i = 0; i < limit; i++) {
        switch (cmd->expected_types[i]) {
            case LIBMSE_CVAR_INT:
                parsed_args[i].i = atoi(argv[i]);
                break;
            case LIBMSE_CVAR_FLOAT:
                parsed_args[i].f = (float)atof(argv[i]);
                break;
            case LIBMSE_CVAR_DOUBLE:
                parsed_args[i].d = atof(argv[i]);
                break;
            case LIBMSE_CVAR_STRING:
                parsed_args[i].s = argv[i];
                break;
        }
    }

    return cmd->handler((cmd->expected_args_count > 0) ? parsed_args : NULL);
}