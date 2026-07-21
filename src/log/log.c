#include <stdarg.h>

#include "log/log.h"
#include "util/str.h"

#define HEX_DIGITS "0123456789abcdef"
#define DECIMAL_BASE 10U
#define HEX_BASE 16U
#define NUMBER_BUFFER_BYTES 21U
#define POINTER_PREFIX "0x"

/* Two: the serial line, and the screen once there is one. */
#define LOG_MAX_SINKS 2U

static log_sink sinks[LOG_MAX_SINKS];
static unsigned sink_count;

static const char *level_tag(int level) {
    switch (level) {
        case LOG_LEVEL_ERROR: return "[ERROR] ";
        case LOG_LEVEL_WARN:  return "[WARN]  ";
        case LOG_LEVEL_INFO:  return "[INFO]  ";
        default:              return "[DEBUG] ";
    }
}

static void emit(const char *text) {
    for (unsigned index = 0; index < sink_count; index++) sinks[index](text);
}

static void emit_unsigned(uint64_t value, uint64_t base) {
    char digits[NUMBER_BUFFER_BYTES];
    size_t length = 0;

    if (value == 0) {
        emit("0");
        return;
    }
    while (value != 0 && length < sizeof digits - 1) {
        digits[length++] = HEX_DIGITS[value % base];
        value /= base;
    }
    char text[NUMBER_BUFFER_BYTES];
    for (size_t index = 0; index < length; index++)
        text[index] = digits[length - index - 1];
    text[length] = '\0';
    emit(text);
}

void log_set_sink(log_sink new_sink) {
    sink_count = 0;
    if (new_sink) sinks[sink_count++] = new_sink;
}

bool log_add_sink(log_sink extra) {
    if (!extra || sink_count == LOG_MAX_SINKS) return false;
    sinks[sink_count++] = extra;
    return true;
}

void log_emit(int level, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    emit(level_tag(level));

    char literal[2] = {0, 0};
    for (size_t index = 0; format[index] != '\0'; index++) {
        if (format[index] != '%') {
            literal[0] = format[index];
            emit(literal);
            continue;
        }
        index++;
        switch (format[index]) {
            case 's': emit(va_arg(arguments, const char *)); break;
            case 'u': emit_unsigned(va_arg(arguments, uint64_t), DECIMAL_BASE); break;
            case 'x': emit_unsigned(va_arg(arguments, uint64_t), HEX_BASE); break;
            case 'p':
                emit(POINTER_PREFIX);
                emit_unsigned((uint64_t)va_arg(arguments, void *), HEX_BASE);
                break;
            case '%': emit("%"); break;
            default:
                /* An unknown specifier prints itself rather than consuming an
                   argument that may not be there. */
                literal[0] = format[index];
                emit(literal);
                break;
        }
    }
    emit("\n");
    va_end(arguments);
}
