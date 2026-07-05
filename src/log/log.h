#ifndef TUNIX_BOOT_LOG_LOG_H
#define TUNIX_BOOT_LOG_LOG_H

#include <stdint.h>

#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

typedef void (*log_sink)(const char *text);

void log_set_sink(log_sink sink);

/* A deliberately small format language: %s, %u, %x, %p, %%. A bootloader that
   needs more than this in its log is doing something in the log that belongs in
   a data structure. */
void log_emit(int level, const char *format, ...);

/*
 * Levels above the compiled-in maximum vanish, arguments and all, because the
 * call sits behind a constant condition the optimiser folds away.
 */
#define LOG_ERROR(...) \
    do { if (LOG_LEVEL >= LOG_LEVEL_ERROR) log_emit(LOG_LEVEL_ERROR, __VA_ARGS__); } while (0)
#define LOG_WARN(...) \
    do { if (LOG_LEVEL >= LOG_LEVEL_WARN) log_emit(LOG_LEVEL_WARN, __VA_ARGS__); } while (0)
#define LOG_INFO(...) \
    do { if (LOG_LEVEL >= LOG_LEVEL_INFO) log_emit(LOG_LEVEL_INFO, __VA_ARGS__); } while (0)
#define LOG_DEBUG(...) \
    do { if (LOG_LEVEL >= LOG_LEVEL_DEBUG) log_emit(LOG_LEVEL_DEBUG, __VA_ARGS__); } while (0)

#endif
