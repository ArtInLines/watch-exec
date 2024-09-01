#include "header.h"

#if defined(_WIN32) || defined(__WIN32__)
#   include <windows.h>
#   include <direct.h>
#   include <shellapi.h>
#else
#   include <sys/types.h>
#   include <sys/wait.h>
#   include <sys/stat.h>
#   include <unistd.h>
#endif // _WIN32

typedef struct {
    AIL_Str out;
    i32 exitCode;
    b32 finished;
} SubProcRes;

#ifndef SUBPROC_PIPE_SIZE
#   define SUBPROC_PIPE_SIZE 2048
#endif

// Forward declarations of functions, that all platforms need to implement
internal SubProcRes subproc_exec_internal(AIL_DA(str) *argv, char *arg_str, AIL_Allocator allocator);


internal SubProcRes subproc_exec(AIL_DA(str) *argv, char *arg_str, AIL_Allocator allocator)
{
    if (!argv->len) {
        log_err("Cannot run empty command");
        return (SubProcRes){0};
    }
    log_info("Running '%s'...", arg_str);
    return subproc_exec_internal(argv, arg_str, allocator);
}



#if defined(_WIN32) || defined(__WIN32__)
//////////////////////////
// Windows Implementation
//////////////////////////
// See also these resources for further information on this implementation:
// - https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session
// - https://learn.microsoft.com/en-us/windows/win32/ProcThread/creating-a-child-process-with-redirected-input-and-output


internal SubProcRes subproc_exec_internal(AIL_DA(str) *argv, char *arg_str, AIL_Allocator allocator)
{
    AIL_UNUSED(argv);
    SubProcRes res = {
        .out      = AIL_STR_FROM_LITERAL(""),
        .exitCode = 0,
        .finished = false,
    };
    HANDLE pipe_read, pipe_write;
    SECURITY_ATTRIBUTES saAttr = {0};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    if (!CreatePipe(&pipe_read, &pipe_write, &saAttr, 0)) {
        log_err("Could not establish pipe to child process");
        goto done;
    }

    STARTUPINFO siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(siStartInfo));
    siStartInfo.cb = sizeof(STARTUPINFO);

    siStartInfo.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    siStartInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    if (siStartInfo.hStdOutput == INVALID_HANDLE_VALUE || siStartInfo.hStdError == INVALID_HANDLE_VALUE) {
        log_err("Could not get the handles to child process stdout/stderr");
        goto done;
    }
    siStartInfo.hStdOutput = pipe_write;
    siStartInfo.hStdError  = pipe_write;
    siStartInfo.dwFlags   |= STARTF_USESTDHANDLES;
    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    if (!CreateProcessA(NULL, arg_str, NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo)) {
        log_err("Could not create child process");
        goto done;
    }

    if (WaitForSingleObject(piProcInfo.hProcess, INFINITE) == WAIT_FAILED) {
        log_err("Failed to wait for child process to exit");
        goto done;
    }

    char *buf = AIL_CALL_ALLOC(allocator, SUBPROC_PIPE_SIZE);
    if (!buf) {
        log_err("Could not allocate enough memory to read child process' output");
        goto done;
    }
    DWORD nBytesRead, nBytesWritten;
    // @Note: We need to write at least one byte into the pipe, or else the ReadFile will block forever, waiting for something to read.
    WriteFile(pipe_write, "\n", 1, &nBytesWritten, NULL);
    if (!ReadFile(pipe_read, buf, SUBPROC_PIPE_SIZE, &nBytesRead, 0)) {
        log_err("Failed to read stdout from child process");
        goto done;
    }
    buf[nBytesRead] = 0;
    res.out      = ail_str_from_parts(buf, nBytesRead);
    res.finished = true;

done:
    if (pipe_read)  CloseHandle(pipe_read);
    if (pipe_write) CloseHandle(pipe_write);
    if (piProcInfo.hProcess) CloseHandle(piProcInfo.hProcess);
    return res;
}


#else
////////////////////////
// POSIX Implementation
////////////////////////

internal SubProcRes subproc_exec_internal(AIL_DA(str) *argv, char *arg_str, AIL_Allocator allocator)
{
    SubProcRes res = {
        .out      = AIL_STR_FROM_LITERAL(""),
        .exitCode = 0,
        .finished = false,
    };
    int pipefd[2];
    if (pipe(pipefd) != -1) {
        log_err("Could not establish pipe to child process");
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
        while (read(stdout, buf, SUBPROC_PIPE_SIZE) != EOF) {
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
        res.out      = ail_sv_from_da(da);
        res.finished = true;

done:
        if (pipefd[0]) close(pipefd[0]);
    }
    return res;
}
#endif


