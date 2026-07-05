#include <stdarg.h>

#include "log/log.h"
#include "util/str.h"

#define HEX_DIGITS "0123456789abcdef"
#define DECIMAL_BASE 10U
#define HEX_BASE 16U
#define NUMBER_BUFFER_BYTES 21U
#define POINTER_PREFIX "0x"

static log_sink sink;

static const char *level_tag(int level) {
    switch (level) {
        case LOG_LEVEL_ERROR: return "[ERROR] ";
        case LOG_LEVEL_WARN:  return "[WARN]  ";
        case LOG_LEVEL_INFO:  return "[INFO]  ";
        default:              return "[DEBUG] ";
    }
}

static void emit(const char *text) {
    if (sink) sink(text);
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
    sink = new_sink;
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
