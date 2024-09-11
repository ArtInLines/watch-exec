#include "header.h"

#ifndef SUBPROC_PIPE_SIZE
#   define SUBPROC_PIPE_SIZE 2048
#endif

#if defined(_WIN32) || defined(__WIN32__)
#   include <windows.h>
#   include <direct.h>
#   include <shellapi.h>
    typedef struct { DWORD in_mode, out_mode, err_mode; } SubProcConsoleState;
    typedef HANDLE SubProcStdHandle;
#else
#   include <sys/types.h>
#   include <sys/wait.h>
#   include <sys/stat.h>
#   include <termios.h>
#   include <unistd.h>
    typedef struct termios SubProcConsoleState;
    typedef int SubProcStdHandle;
#endif // _WIN32

typedef struct {
    i32 exitCode;
    b32 finished;
} SubProcRes;


// Forward declarations of functions, that all platforms need to implement
internal SubProcRes subproc_exec_internal(AIL_DA(str) *argv, char *arg_str, AIL_Allocator allocator);
internal SubProcConsoleState subproc_get_console_state(void);
internal void subproc_set_console_state(SubProcConsoleState state);
internal void subproc_init(void);


global SubProcConsoleState subproc_initial_console_state;
global SubProcStdHandle subproc_stdin;
global SubProcStdHandle subproc_stdout;
global SubProcStdHandle subproc_stderr;


internal SubProcRes subproc_exec(AIL_DA(str) *argv, char *arg_str, AIL_Allocator allocator)
{
    if (!argv->len) {
        log_err("Cannot run empty command");
        return (SubProcRes){0};
    }
    log_info("Running '%s'...", arg_str);
    return subproc_exec_internal(argv, arg_str, allocator);
}

internal void subproc_print_output(AIL_Str out_str)
{
    if (out_str.len == 1 && out_str.str[0] == '\n') out_str.len = 0;
    AIL_SV out = ail_sv_from_str(out_str);

    // @Note: Certain ANSI escape codes should not be forwarded to the console to prevent weird artifacts
    // Currently these codes are specifically some erase functions
    // For a full list of existing ansi escape codes, see this handy cheatsheet: https://gist.github.com/ConnerWill/d4b6c776b509add763e17f9f113fd25b
    AIL_SV forbidden_seqs[] = {
        SV_LIT("\x1b[H"),  // Moves cursor to position 0,0
        SV_LIT("\x1b[1J"), // Eares from cursor to beginning of screen
        SV_LIT("\x1b[2J"), // Eares entire screen
    };
    // @Note: Certain ANSI escape codes may change the console state (i.e. changing color mode)
    // thus we need to save the previous state and restore it after printing is done
    SubProcConsoleState state = subproc_get_console_state();
    while (out.len) {
        AIL_SV_Find_Of_Res res = ail_sv_find_of(out, forbidden_seqs, AIL_ARRLEN(forbidden_seqs));
        if (res.sv_idx < 0) {
            printf("%s", out.str);
            break;
        } else {
            char c = out.str[res.sv_idx];
            *((char *)(out.str + res.sv_idx)) = 0;
            printf("%s", out.str);
            *((char *)(out.str + res.sv_idx)) = c;
            out = ail_sv_offset(out, res.sv_idx + forbidden_seqs[res.needle_idx].len);
        }
    }
    subproc_set_console_state(state);
}

internal void subproc_deinit(void)
{
    subproc_set_console_state(subproc_initial_console_state);
}



#if defined(_WIN32) || defined(__WIN32__)
//////////////////////////
// Windows Implementation
//////////////////////////

internal SubProcConsoleState subproc_get_console_state(void)
{
    SubProcConsoleState res;
    AIL_ASSERT(GetConsoleMode(subproc_stdin,  &res.in_mode));
    AIL_ASSERT(GetConsoleMode(subproc_stdout, &res.out_mode));
    AIL_ASSERT(GetConsoleMode(subproc_stderr, &res.err_mode));
    return res;
}

internal void subproc_set_console_state(SubProcConsoleState state)
{
    AIL_ASSERT(SetConsoleMode(subproc_stdin,  state.in_mode));
    AIL_ASSERT(SetConsoleMode(subproc_stdout, state.out_mode));
    AIL_ASSERT(SetConsoleMode(subproc_stdout, state.err_mode));
}

internal void subproc_init(void)
{
    subproc_stdin  = GetStdHandle(STD_INPUT_HANDLE);
    subproc_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    subproc_stderr = GetStdHandle(STD_ERROR_HANDLE);
    AIL_ASSERT(subproc_stdin  != INVALID_HANDLE_VALUE);
    AIL_ASSERT(subproc_stdout != INVALID_HANDLE_VALUE);
    AIL_ASSERT(subproc_stderr != INVALID_HANDLE_VALUE);

    SubProcConsoleState state = subproc_get_console_state();
    subproc_initial_console_state = state;
    state.out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    state.err_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    subproc_set_console_state(state);
}

// Code mostly adapted from the following documentation (with lots of experimentation until it worked properly):
// - https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session
// - https://learn.microsoft.com/en-us/windows/win32/ProcThread/creating-a-child-process-with-redirected-input-and-output
internal SubProcRes subproc_exec_internal(AIL_DA(str) *argv, char *arg_str, AIL_Allocator allocator)
{
    AIL_UNUSED(argv);
    SubProcRes res = {
        .exitCode = 0,
        .finished = false,
    };
    HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE pipe_in_read = 0, pipe_in_write = 0;
    HANDLE pipe_out_read = 0, pipe_out_write = 0;
    HPCON hpc = NULL;
    AIL_DA(char) buf = ail_da_new_with_alloc(char, SUBPROC_PIPE_SIZE, allocator);
    SECURITY_ATTRIBUTES saAttr = {0};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    AIL_UNUSED(saAttr);
    if (!CreatePipe(&pipe_in_read, &pipe_in_write, NULL, 0)) {
        log_err("Could not establish pipe to child process");
        goto done;
    }
    if (!CreatePipe(&pipe_out_read, &pipe_out_write, NULL, 0)) {
        log_err("Could not establish secondary pipes to child process");
        CloseHandle(pipe_in_read);
        goto done;
    }

    CONSOLE_SCREEN_BUFFER_INFO console_info;
    if (!GetConsoleScreenBufferInfo(stdout_handle, &console_info)) {
        log_err("Could not retrieve information about parent console");
        goto done;
    }
    if (FAILED(CreatePseudoConsole(console_info.dwSize, pipe_in_read, pipe_out_write, 0, &hpc))) {
        log_err("Could not establish pseudo console");
        goto done;
    }

    STARTUPINFOEXW si;
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    size_t listSize;
    InitializeProcThreadAttributeList(NULL, 1, 0, &listSize);
    si.lpAttributeList = AIL_CALL_ALLOC(allocator, listSize);
    if (!si.lpAttributeList) {
        log_err("Could not allocate enough memory to provide complex startup info to child process");
        goto done;
    }
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &listSize)) {
        log_err("Could not initialize startup infor for child process");
        goto done;
    }
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc, sizeof(hpc), NULL, NULL)) {
        printf("Error: %lu\n", GetLastError());
        log_err("Could not set the child process as a pseudo console");
        goto done;
    }

    si.StartupInfo.hStdError  = stderr_handle;
    si.StartupInfo.hStdOutput = stdout_handle;
    if (si.StartupInfo.hStdOutput == INVALID_HANDLE_VALUE || si.StartupInfo.hStdError == INVALID_HANDLE_VALUE) {
        log_err("Could not get the handles to child process stdout/stderr");
        goto done;
    }
    si.StartupInfo.hStdInput  = pipe_in_read;
    si.StartupInfo.hStdOutput = pipe_in_write;
    si.StartupInfo.hStdError  = pipe_in_write;
    si.StartupInfo.dwFlags   |= STARTF_USESTDHANDLES;
    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    size_t nConverted;
    wchar_t *w_arg_str = AIL_CALL_ALLOC(allocator, sizeof(wchar_t)*(strlen(arg_str) + 1));
    mbstowcs_s(&nConverted, w_arg_str, strlen(arg_str) + 1, arg_str, strlen(arg_str) + 1);
    if (!CreateProcessW(NULL, w_arg_str, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &si.StartupInfo, &piProcInfo)) {
        log_err("Could not create child process");
        goto done;
    }
    AIL_CALL_FREE(allocator, w_arg_str);

    if (WaitForSingleObject(piProcInfo.hProcess, INFINITE) == WAIT_FAILED) {
        log_err("Failed to wait for child process to exit");
        goto done;
    }
    DWORD nExitCode;
    if (!GetExitCodeProcess(piProcInfo.hProcess, &nExitCode)) {
        log_err("Failed to retrieve exit code of child process");
        goto done;
    }
    res.exitCode = nExitCode;

    DWORD nBytesRead, nBytesWritten;
    // @Note: We need to write at least one byte into the pipe, or else the ReadFile will block forever, waiting for something to read.
    WriteFile(pipe_out_write, "\n", 1, &nBytesWritten, NULL);
    for (;;) {
        u32 n = buf.cap - buf.len;
        AIL_ASSERT(n >= SUBPROC_PIPE_SIZE);
        if (!ReadFile(pipe_out_read, &buf.data[buf.len], n, &nBytesRead, 0)) {
            log_err("Failed to read stdout from child process");
            goto done;
        }
        buf.len += nBytesRead;
        if (nBytesRead < n) break;
        ail_da_resize(&buf, buf.len + SUBPROC_PIPE_SIZE);
    }
    ail_da_push(&buf, 0);
    buf.len--;
    res.finished = true;
    subproc_print_output(ail_str_from_da_nil_term(buf));

done:
    if (pipe_in_read)        CloseHandle(pipe_in_read);
    if (pipe_in_write)       CloseHandle(pipe_in_write);
    if (pipe_out_read)       CloseHandle(pipe_out_read);
    if (pipe_out_write)      CloseHandle(pipe_out_write);
    if (piProcInfo.hProcess) CloseHandle(piProcInfo.hProcess);
    if (hpc)                 ClosePseudoConsole(hpc);
    if (buf.data)            ail_da_free(&buf);
    if (si.lpAttributeList)  AIL_CALL_FREE(allocator, si.lpAttributeList);
    return res;
}


#else
////////////////////////
// POSIX Implementation
////////////////////////

global struct termios subproc_old_out_mode;


SubProcConsoleState subproc_get_console_state(void)
{
    SubProcConsoleState attr;
    tcgetattr(subproc_stdin, &attr);
    return attr;
}

void subproc_set_console_state(SubProcConsoleState state)
{
    tcsetattr(subproc_stdin,  TCSANOW, &state);
    tcsetattr(subproc_stdout, TCSANOW, &state);
    tcsetattr(subproc_stderr, TCSANOW, &state);
}

void subproc_init(void)
{
    subproc_stdin  = STDIN_FILENO;
    subproc_stdout = STDOUT_FILENO;
    subproc_stderr = STDERR_FILENO;
    subproc_initial_console_state = subproc_get_console_state();
    SubProcConsoleState attr = subproc_initial_console_state;
    attr.c_lflag &= ~(ICANON|ECHO);
    attr.c_lflag |= ECHO;
    subproc_set_console_state(attr);
}


SubProcRes subproc_exec_internal(AIL_DA(str) *argv, char *arg_str, AIL_Allocator allocator)
{
    AIL_UNUSED(arg_str);
    SubProcRes res = { 0 };
    int pipefd[2];
    if (pipe(pipefd) != -1) {
        log_err("Could not establish pipe to child process: %d", errno);
        goto done;
    }

    AIL_DA(char) da = ail_da_new_with_alloc(char, SUBPROC_PIPE_SIZE, allocator);
    char buf[SUBPROC_PIPE_SIZE] = {0};
    pid_t cpid = fork();
    if (cpid < 0) {
        log_err("Could not create child process");
        goto done;
    } else if (cpid == 0) { // Run by child
        ail_da_push(argv, NULL);
        close(pipefd[0]);
        execvp(argv->data[0], (char* const*) argv->data);
        while (read(STDOUT_FILENO, buf, SUBPROC_PIPE_SIZE) != EOF) {
            u64 len = strlen(buf);
            ail_da_pushn(&da, buf, len);
            memset(buf, 0, len);
        }
        write(pipefd[1], da.data, da.len);
        close(pipefd[1]); // Required for reader to see EOF
    } else { // Run by parent
        close(pipefd[1]);
        while (read(pipefd[0], buf, SUBPROC_PIPE_SIZE) != EOF) {
            u64 len = strlen(buf);
            ail_da_pushn(&da, buf, len);
            memset(buf, 0, len);
        }
        int wstatus = 0;
        if (waitpid(cpid, &wstatus, 0) < 0) {
            log_err("Failed to wait for child process to exit");
            goto done;
        }
        if (WIFEXITED(wstatus)) res.exitCode = WEXITSTATUS(wstatus);

        ail_da_push(&da, 0);
        da.len--;
        res.finished = true;
        subproc_print_output(ail_str_from_da_nil_term(da));

done:
        if (pipefd[0]) close(pipefd[0]);
        if (da.data)   ail_da_free(&da);
    }
    return res;
}
#endif


