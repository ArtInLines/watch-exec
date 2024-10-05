#include "header.h"

#if defined(_WIN32) || defined(__WIN32__)
#	include <windows.h>
	typedef HANDLE TermHandle;
	typedef struct TermState { DWORD in, out, err; } TermState;
#else
#   include <termios.h>
	typedef int TermHandle;
	typedef struct termios TermState;
#endif

typedef struct TermHandles { TermHandle in, out, err; } TermHandles;
typedef enum AIL_FLAG_ENUM TermMode {
	TERM_MODE_ECHO       = 1 << 0, // Echo input stdin to stdout automatically
	TERM_MODE_LINE_INPUT = 1 << 1, // Input is only processed when enter is pressed
	TERM_MODE_INSERT     = 1 << 2, // Insert text instead of overriding text at specified position
	TERM_MODE_MOUSE      = 1 << 3, // Mouse input events enabled @Note: Only has an effect on Windows
	TERM_MODE_CTRL_PROC  = 1 << 4, // Certain control sequences (like Ctrl+c) are handled by the console
	TERM_MODE_VPROC      = 1 << 5, // Process virtual ansi codes
	TERM_MODE_FLAG_COUNT = 1 << 6, // Count of flags in this enum
} TermMode;

global TermHandles term_handles;
global TermState   term_initial_state;
global TermState   term_current_state;

// Forward declarations of functions that are implemented per platform
internal TermHandles term_get_handles(void);
internal TermMode    term_state_get_mode(TermState state);
internal TermState   term_state_set_mode(TermState state, TermMode mode);
internal void        term_set_state(TermState state);
internal TermState   _term_get_state(void);

internal void term_init(void);
internal void term_deinit(void);
internal TermMode term_get_mode(void);
internal TermState term_state_add_mode(TermState state, TermMode mode);
internal TermState term_state_sub_mode(TermState state, TermMode mode);
internal void term_set_mode(TermMode mode);
internal void term_add_mode(TermMode mode);
internal void term_sub_mode(TermMode mode);
internal int term_get_char(void);


internal void term_init(void)
{
	term_handles       = term_get_handles();
	term_initial_state = _term_get_state();
	term_current_state = term_initial_state;
}

internal void term_deinit(void)
{
	term_set_state(term_initial_state);
}

internal TermMode term_get_mode(void)
{
	return term_state_get_mode(term_current_state);
}

internal TermState term_state_add_mode(TermState state, TermMode mode)
{
	TermMode cur = term_state_get_mode(term_current_state);
	return term_state_set_mode(state, cur | mode);
}

internal TermState term_state_sub_mode(TermState state, TermMode mode)
{
	TermMode cur = term_state_get_mode(term_current_state);
	return term_state_set_mode(state, cur & ~mode);
}

internal void term_set_mode(TermMode mode)
{
	term_set_state(term_state_set_mode(term_current_state, mode));
}

internal void term_add_mode(TermMode mode)
{
	term_set_state(term_state_add_mode(term_current_state, mode));
}

internal void term_sub_mode(TermMode mode)
{
	term_set_state(term_state_sub_mode(term_current_state, mode));
}

internal int term_get_char(void)
{
	return getchar(); // @TODO: Rewrite without libc
}


#if defined(_WIN32) || defined(__WIN32__)
//////////////////////////
// Windows Implementation
//////////////////////////

internal TermHandles term_get_handles(void)
{
	TermHandles res = {
		.in  = GetStdHandle(STD_INPUT_HANDLE),
		.out = GetStdHandle(STD_OUTPUT_HANDLE),
		.err = GetStdHandle(STD_ERROR_HANDLE),
	};
	AIL_ASSERT(res.in  != INVALID_HANDLE_VALUE);
	AIL_ASSERT(res.out != INVALID_HANDLE_VALUE);
	AIL_ASSERT(res.err != INVALID_HANDLE_VALUE);
	return res;
}

internal TermState _term_get_state(void)
{
	TermState state;
	AIL_ASSERT(GetConsoleMode(term_handles.in,  &state.in));
	AIL_ASSERT(GetConsoleMode(term_handles.out, &state.out));
	AIL_ASSERT(GetConsoleMode(term_handles.err, &state.err));
	return state;
}

internal TermMode term_state_get_mode(TermState state)
{
	AIL_STATIC_ASSERT(TERM_MODE_FLAG_COUNT == (1 << 6));
	TermMode mode = 0;
	if (state.in  & ENABLE_ECHO_INPUT)      mode |= TERM_MODE_ECHO;
	if (state.in  & ENABLE_LINE_INPUT)      mode |= TERM_MODE_LINE_INPUT;
	if (state.in  & ENABLE_INSERT_MODE)     mode |= TERM_MODE_INSERT;
	if (state.in  & ENABLE_MOUSE_INPUT)     mode |= TERM_MODE_MOUSE;
	if (state.in  & ENABLE_PROCESSED_INPUT) mode |= TERM_MODE_CTRL_PROC;
	if (state.out & ENABLE_PROCESSED_OUTPUT &&
		state.out & ENABLE_VIRTUAL_TERMINAL_PROCESSING &&
		state.err & ENABLE_PROCESSED_OUTPUT &&
		state.err & ENABLE_VIRTUAL_TERMINAL_PROCESSING) mode |= TERM_MODE_VPROC;
	return mode;
}

internal TermState term_state_set_mode(TermState state, TermMode mode)
{
	AIL_STATIC_ASSERT(TERM_MODE_FLAG_COUNT == (1 << 6));
	if (mode & TERM_MODE_ECHO)       state.in |=  ENABLE_ECHO_INPUT;
	else                             state.in &= ~ENABLE_ECHO_INPUT;
	if (mode & TERM_MODE_LINE_INPUT) state.in |=  ENABLE_LINE_INPUT;
	else                             state.in &= ~ENABLE_LINE_INPUT;
	if (mode & TERM_MODE_INSERT)     state.in |=  ENABLE_INSERT_MODE;
	else                             state.in &= ~ENABLE_INSERT_MODE;
	if (mode & TERM_MODE_MOUSE)      state.in |=  ENABLE_MOUSE_INPUT;
	else                             state.in &= ~ENABLE_MOUSE_INPUT;
	if (mode & TERM_MODE_CTRL_PROC)  state.in |=  ENABLE_PROCESSED_INPUT;
	else                             state.in &= ~ENABLE_PROCESSED_INPUT;
	if (mode & TERM_MODE_VPROC) {
		state.out |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		state.err |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	} else {
		state.out &= ~(ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
		state.err &= ~(ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	}
	return state;
}

internal void term_set_state(TermState state)
{
	if (!SetConsoleMode(term_handles.in,  state.in)) {
		printf("Error in setting console mode for STDIN (handle: %p, state: %lu): %lu\n", term_handles.in, state.in, GetLastError());
	}
	if (!SetConsoleMode(term_handles.out, state.out)) {
		printf("Error in setting console mode for STDOUT (handle: %p, state: %lu): %lu\n", term_handles.out, state.out, GetLastError());
	}
	if (!SetConsoleMode(term_handles.err, state.err)) {
		printf("Error in setting console mode for STDERR (handle: %p, state: %lu): %lu\n", term_handles.err, state.err, GetLastError());
	}
	term_current_state = state;
}


#else
////////////////////////
// POSIX Implementation
////////////////////////

internal TermHandles term_get_handles(void)
{
	return (TermHandles) {
		.in  = STDIN_FILENO,
		.out = STDOUT_FILENO,
		.err = STDERR_FILENO,
	};
}

internal TermState _term_get_state(void)
{
	TermState state;
	AIL_ASSERT(!tcgetattr(term_handles.in, &state));
	return state;
}

internal TermMode term_state_get_mode(TermState state)
{
	AIL_STATIC_ASSERT(TERM_MODE_FLAG_COUNT == (1 << 6));
	TermMode mode = 0;
	if (state.c_lflag & ECHO)   mode |= TERM_MODE_ECHO;
	if (state.c_lflag & ICANON) mode |= TERM_MODE_LINE_INPUT | TERM_MODE_INSERT;
	if (state.c_lflag & ISIG)   mode |= TERM_MODE_CTRL_PROC;
	// @TODO: Which flags effect processing of ansi escape codes? Can they be disabled at all?
	mode |= TERM_MODE_VPROC;
	return mode;
}

internal TermState term_state_set_mode(TermState state, TermMode mode)
{
	AIL_STATIC_ASSERT(TERM_MODE_FLAG_COUNT == (1 << 6));
	if (mode & TERM_MODE_ECHO)       state.c_lflag |=   ECHO | ECHONL;
	else                             state.c_lflag &= ~(ECHO | ECHONL);
	// @TODO: One of the following modes might turn ICANON on and the other turn it off again
	if (mode & TERM_MODE_LINE_INPUT) state.c_lflag |=  ICANON;
	else                             state.c_lflag &= ~ICANON;
	if (mode & TERM_MODE_INSERT)     state.c_lflag |=  ICANON;
	else                             state.c_lflag &= ~ICANON;
	if (mode & TERM_MODE_CTRL_PROC)  state.c_lflag |=  ISIG;
	else                             state.c_lflag &= ~ISIG;
	return state;
}

internal void term_set_state(TermState state)
{
	// @TODO: Maybe use TCSADRAIN instead of TCSANOW?
	AIL_ASSERT(!tcsetattr(term_handles.in, TCSANOW, &state));
	term_current_state = state;
}

#endif
