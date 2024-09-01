#include "header.h"

AIL_PRINTF_FORMAT(1, 2)
void log_err(char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("\x1b[31m[ERROR]: ", stdout);
    vprintf(format, args);
    fputs("\x1b[0m\n", stdout);
    va_end(args);
}

AIL_PRINTF_FORMAT(1, 2)
void log_warn(char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("\x1b[33m[WARN]: ", stdout);
    vprintf(format, args);
    fputs("\x1b[0m\n", stdout);
    va_end(args);
}

AIL_PRINTF_FORMAT(1, 2)
void log_info(char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("[INFO]: ", stdout);
    vprintf(format, args);
    fputs("\n", stdout);
    va_end(args);
}

AIL_PRINTF_FORMAT(1, 2)
void log_succ(char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("\x1b[32m[SUCC]: ", stdout);
    vprintf(format, args);
    fputs("\x1b[0m\n", stdout);
    va_end(args);
}
