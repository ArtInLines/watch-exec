#include "header.h"

global TermState log_term_state;

internal void log_init(void)
{
	log_term_state = term_current_state;
	term_add_mode((TERM_MODE_VPROC | TERM_MODE_LINE_INPUT) & ~TERM_MODE_ECHO);
}

internal void log_deinit(void)
{
	term_set_state(log_term_state);
}

AIL_PRINTF_FORMAT(1, 2)
internal void log_err(char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("\x1b[31m[ERROR]: ", stdout);
    vprintf(format, args);
    fputs("\x1b[0m\n", stdout);
    va_end(args);
}

AIL_PRINTF_FORMAT(1, 2)
internal void log_warn(char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("\x1b[33m[WARN]: ", stdout);
    vprintf(format, args);
    fputs("\x1b[0m\n", stdout);
    va_end(args);
}

AIL_PRINTF_FORMAT(1, 2)
internal void log_info(char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("[INFO]: ", stdout);
    vprintf(format, args);
    fputs("\n", stdout);
    va_end(args);
}

AIL_PRINTF_FORMAT(1, 2)
internal void log_succ(char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("\x1b[32m[SUCC]: ", stdout);
    vprintf(format, args);
    fputs("\x1b[0m\n", stdout);
    va_end(args);
}
