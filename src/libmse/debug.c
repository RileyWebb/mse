#include "libmse/debug.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_BUFFERS 8
#define MAX_LOG_MESSAGE 512
#define MAX_LOG_FILE 128

static struct {
    int level;
    bool quiet;
    bool always_flush;
    FILE *buffers[MAX_BUFFERS];
    debug_log_callback callbacks[MAX_BUFFERS];
    void *callback_user_data[MAX_BUFFERS];
} ctx = {
    .level = DEBUG_LOG_LEVEL_TRACE,
};

static const char *debug_log_strings[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "ASSRT"
};

static const char *debug_log_esc_colors[] = {
    "\x1b[95m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m", "\x1b[0;90m",
};

static void D_LogPrint(const debug_log *log, FILE *buffer) {
    if (log == NULL || buffer == NULL) {
        return;
    }

    char time_buffer[32];
    time_t curtime = log->tp.tv_sec;
    struct tm local_time;
#ifdef _WIN32
    localtime_s(&local_time, &curtime);
#else
    localtime_r(&curtime, &local_time);
#endif
    snprintf(time_buffer, sizeof(time_buffer), "%02d:%02d:%02d.%06ld",
        local_time.tm_hour, local_time.tm_min, local_time.tm_sec, (long)log->tp.tv_usec);

    const char *origin = log->origin != NULL ? log->origin : DEBUG_LOG_SOURCE;

#if defined(DEBUG) && defined(DEBUG_USE_COLOR)
    if (buffer == stdout || buffer == stderr) {
        fprintf(buffer, "%s [%s] %s%-5s\x1b[0m ", time_buffer, origin, debug_log_esc_colors[log->level], debug_log_strings[log->level]);
    } else {
        fprintf(buffer, "%s [%s] %-5s ", time_buffer, origin, debug_log_strings[log->level]);
    }
#elif defined(DEBUG)
    fprintf(buffer, "%s [%s] %-5s ", time_buffer, origin, debug_log_strings[log->level]);
#else
#if defined(DEBUG_USE_COLOR)
    if (buffer == stdout || buffer == stderr) {
        fprintf(buffer, "%s [%s] %s%-5s\x1b[0m ", time_buffer, origin, debug_log_esc_colors[log->level], debug_log_strings[log->level]);
    } else {
        fprintf(buffer, "%s [%s] %-5s ", time_buffer, origin, debug_log_strings[log->level]);
    }
#else
    fprintf(buffer, "%s [%s] %-5s ", time_buffer, origin, debug_log_strings[log->level]);
#endif
#endif

    fputs(log->message, buffer);
    fputc('\n', buffer);
}

int DEBUG_RegisterBuffer(FILE *buffer) {
    if (buffer == NULL) {
        return -1;
    }

    for (int i = 0; i < MAX_BUFFERS; ++i) {
        if (ctx.buffers[i] == NULL) {
            ctx.buffers[i] = buffer;
            return 0;
        }
    }

    return -1;
}

int DEBUG_RegisterCallback(debug_log_callback callback, void *user_data) {
    if (callback == NULL) {
        return -1;
    }

    for (int i = 0; i < MAX_BUFFERS; ++i) {
        if (ctx.callbacks[i] == NULL) {
            ctx.callbacks[i] = callback;
            ctx.callback_user_data[i] = user_data;
            return 0;
        }
    }

    return -1;
}

void DEBUG_WriteLog(int level, const char *file, int line, const char *fmt, ...) {
    if (level < ctx.level || fmt == NULL) {
        return;
    }

    debug_log log = {
        .level = (DEBUG_LOG_LEVEL)level,
        .origin = DEBUG_LOG_SOURCE,
        .line = line,
    };

    const char *source_file = file != NULL ? strrchr(file, '/') : NULL;
#ifdef _WIN32
    const char *windows_file = file != NULL ? strrchr(file, '\\') : NULL;
    if (windows_file != NULL && (source_file == NULL || windows_file > source_file)) {
        source_file = windows_file;
    }
#endif
    if (source_file != NULL) {
        ++source_file;
    } else {
        source_file = file != NULL ? file : "<unknown>";
    }

    snprintf(log.file, sizeof(log.file), "%s", source_file);

    va_list args;
    va_start(args, fmt);
    vsnprintf(log.message, sizeof(log.message), fmt, args);
    va_end(args);

    struct timespec timepoint;
    timespec_get(&timepoint, TIME_UTC);
    log.tp.tv_sec = timepoint.tv_sec;
    log.tp.tv_usec = (long)(timepoint.tv_nsec / 1000L);

    for (int i = 0; i < MAX_BUFFERS && ctx.callbacks[i] != NULL; ++i) {
        ctx.callbacks[i](&log, ctx.callback_user_data[i]);
    }

    FILE *primary = (log.level == DEBUG_LOG_LEVEL_ERROR || log.level == DEBUG_LOG_LEVEL_FATAL || log.level == DEBUG_LOG_LEVEL_ASSERT)
        ? stderr
        : stdout;
    D_LogPrint(&log, primary);

    for (int i = 0; i < MAX_BUFFERS && ctx.buffers[i] != NULL; ++i) {
        D_LogPrint(&log, ctx.buffers[i]);
    }

    if (ctx.always_flush || log.level >= DEBUG_LOG_LEVEL_ERROR) {
        fflush(stdout);
        fflush(stderr);
    }

    if (log.level == DEBUG_LOG_LEVEL_FATAL) {
        exit(-1);
    }
}

void DEBUG_Flush(void) {
    fflush(stdout);
    fflush(stderr);

    for (int i = 0; i < MAX_BUFFERS && ctx.buffers[i] != NULL; ++i) {
        fflush(ctx.buffers[i]);
    }
}
