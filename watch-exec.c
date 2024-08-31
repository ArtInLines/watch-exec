#include <stdio.h>

#define DMON_IMPL
#include "deps/dmon/dmon.h"
#define RE_DOT_MATCHES_NEWLINE 0
#include "deps/tiny-regex-c/re.h"
#include "deps/tiny-regex-c/re.c"
#define AIL_ALL_IMPL
#include "deps/ail/ail.h"
#define AIL_SV_IMPL
#include "deps/ail/ail_sv.h"

#define VERSION "1.1"

// @TODO: Features to add:
// - ignore folders
// - allow specifying seperate commands for seperate dirs/matches
// - provide non-recursive option

#define SV_LIT AIL_SV_FROM_LITERAL
#define SV_LIT_T AIL_SV_FROM_LITERAL_T

#define PROC_PIPE_SIZE 2048

#define BUFFER_LEN 32
typedef struct StrList {
    u32 len;
    char* data[BUFFER_LEN];
} StrList;
typedef struct RegexList {
    u32 len;
    re_t data[BUFFER_LEN];
} RegexList;
typedef struct CmdList {
    u32 len;
    str data[BUFFER_LEN];
    AIL_DA(str) cmds[BUFFER_LEN];
} CmdList;
#define list_push(list, el) ((list).data[(list).len++] = (el))

#ifdef _WIN32
#   include <windows.h>
#   include <direct.h>
#   include <shellapi.h>
    typedef HANDLE ProcOSPipe;
#else
#   include <sys/types.h>
#   include <sys/wait.h>
#   include <sys/stat.h>
#   include <unistd.h>
    typedef int ProcOSPipe;
#endif // _WIN32
typedef struct {
    AIL_Str out;
    b32 succ;
} ProcRes;


global StrList   dirs;
global RegexList regexs;
global CmdList   cmds;

global b32 is_proc_running;
global b32 is_proc_waiting;


AIL_PRINTF_FORMAT(1, 2)
void log_err(char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("\x1b[31m[ERROR:] ", stdout);
    vprintf(format, args);
    fputs("\x1b[0m\n", stdout);
    va_end(args);
}

AIL_PRINTF_FORMAT(1, 2)
void log_warn(char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("\x1b[33m[WARN:] ", stdout);
    vprintf(format, args);
    fputs("\x1b[0m\n", stdout);
    va_end(args);
}

AIL_PRINTF_FORMAT(1, 2)
void log_info(char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("[INFO:] ", stdout);
    vprintf(format, args);
    fputs("\n", stdout);
    va_end(args);
}

internal void print_help(char *program)
{
    printf("%s:\n", program);
    printf("Execute commands whenever specific files are changed...\n");
    printf("\n");
    printf("There are different ways to use this program\n");
    printf("Each of the following variants is more powerful than the previous options\n");
    printf("Usage variants:\n");
    printf("  1. %s <dir> <cmd>\n", program);
    printf("  2. %s <dir> <match> <cmd> [<cmd>]*\n", program);
    printf("  3. %s [<flag>]+\n", program);
    printf("\n");
    printf("Usage options 3 allows providing several directories/match-strings/commands\n");
    printf("\n");
    printf("To match specific files, the following regex syntax is used:\n");
    printf("  - '.':        matches any character\n");
    printf("  - '^':        matches beginning of string\n");
    printf("  - '$':        matches end of string\n");
    printf("  - '*':        match zero or more (greedy)\n");
    printf("  - '+':        match one or more (greedy)\n");
    printf("  - '?':        match zero or one (non-greedy)\n");
    printf("  - '[abc]':    match if one of {'a', 'b', 'c'}\n");
    printf("  - '[^abc]':   match if NOT one of {'a', 'b', 'c'}\n");
    printf("  - '[a-zA-Z]': match the character set of the ranges { a-z | A-Z }\n");
    printf("  - '\\s':       Whitespace, \\t \\f \\r \\n \\v and spaces\n");
    printf("  - '\\S':       Non-whitespace\n");
    printf("  - '\\w':       Alphanumeric, [a-zA-Z0-9_]\n");
    printf("  - '\\W':       Non-alphanumeric\n");
    printf("  - '\\d':       Digits, [0-9]\n");
    printf("  - '\\D':       Non-digits\n");
    printf("\n");
    printf("When using option 3, the following syntax variants are available for specifying options:\n");
    printf("  1. <flag>=<value>\n");
    printf("  2. <flag> <value> [<value>]*\n");
    printf("Each flag is allowed to be provided several times\n");
    printf("The ordering of options doesn't matter except for the order of commands\n");
    printf("All commands are executed in the order that they are provided in when the specified files are changed\n");
    printf("\n");
    printf("Option flags:\n");
    printf("  -d|--dir:     Directory to match files inside of\n");
    printf("  -m|--match:   Regular Expression to match file-names against\n");
    printf("  -c|--cmd:     Command to execute when a matching file was changed\n");
    printf("  -h|--help:    Show this help message\n");
    printf("  -v|--version: Show the program's version\n");
}

internal void print_version(char *program)
{
    printf("Watch-Exec (%s): v%s\n", program, VERSION);
    printf("Copyright (C) 2024 Lily Val Richter\n");
}

ProcRes proc_exec(AIL_DA(str) *argv, char *arg_str, AIL_Allocator allocator)
{
    ProcRes res = {
        .out  = AIL_STR_FROM_LITERAL(""),
        .succ = false,
    };
    if (!argv->len) {
        log_err("Cannot run empty command");
        return res;
    }
    log_info("Running '%s'", arg_str);
#ifdef _WIN32
    ProcOSPipe pipe_read, pipe_write;
    SECURITY_ATTRIBUTES saAttr = {0};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    if (!CreatePipe(&pipe_read, &pipe_write, &saAttr, 0)) {
        log_err("Could not establish pipe to child process");
        return res;
    }

    STARTUPINFO siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(siStartInfo));
    siStartInfo.cb = sizeof(STARTUPINFO);

    siStartInfo.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    siStartInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    if (siStartInfo.hStdOutput == INVALID_HANDLE_VALUE || siStartInfo.hStdError == INVALID_HANDLE_VALUE) {
        log_err("Could not get the handles to child process stdout/stderr");
        return res;
    }
    siStartInfo.hStdOutput = pipe_write;
    siStartInfo.hStdError  = pipe_write;
    siStartInfo.dwFlags   |= STARTF_USESTDHANDLES;
    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    if (!CreateProcessA(NULL, arg_str, NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo)) {
        log_err("Could not create child process");
        CloseHandle(pipe_write);
        CloseHandle(pipe_read);
        return res;
    }

    if (WaitForSingleObject(piProcInfo.hProcess, INFINITE) == WAIT_FAILED) {
        log_err("Failed to wait for child process to exit");
        CloseHandle(pipe_write);
        CloseHandle(pipe_read);
        CloseHandle(piProcInfo.hProcess);
        return res;
    }

    char *buf = AIL_CALL_ALLOC(allocator, PROC_PIPE_SIZE);
    if (!buf) {
        log_err("Could not allocate enough memory to read child process' output");
        return res;
    }
    DWORD nBytesRead;
    ReadFile(pipe_read, buf, PROC_PIPE_SIZE, &nBytesRead, 0);
    buf[nBytesRead] = 0;
    res.out = ail_str_from_parts(buf, nBytesRead);
#else
    ProcOSPipe pipefd[2];
    if (pipe(pipefd) != -1) {
        log_err("Could not establish pipe to child process");
        return res;
    }

    AIL_DA(char) da = ail_da_new_with_alloc(char, PROC_PIPE_SIZE, allocator);
    char buf[PROC_PIPE_SIZE] = {0};
    pid_t cpid = fork();
    if (cpid < 0) {
        log_err("Could not create child process");
        return res;
    } else if (cpid == 0) { // Run by child
        ail_da_push(argv, NULL);
        close(pipefd[0]);
        execvp(argv->data[0], (char* const*) argv->data);
        while (read(stdout, buf, PROC_PIPE_SIZE) != EOF) {
            u64 len = strlen(buf);
            ail_da_pushn(&da, buf, len);
            memset(buf, 0, len);
        }
        write(pipefd[1], da.data, da.len);
        close(pipefd[1]); // Required for reader to see EOF
    } else { // Run by parent
        close(pipefd[1]);
        while (read(pipefd[0], buf, PROC_PIPE_SIZE) != EOF) {
            u64 len = strlen(buf);
            ail_da_pushn(&da, buf, len);
            memset(buf, 0, len);
        }
        int wstatus = 0;
        if (waitpid(cpid, &wstatus, 0) < 0) {
            log_err("Failed to wait for child process to exit");
            return res;
        }
        ail_da_push(&da, 0);
        res.out = ail_sv_from_da(da);
    }
#endif
    AIL_CALL_FREE(argv->allocator, arg_str);
    res.succ = true;
    return res;
}

b32 re_matches(re_t regex, const char *str)
{
    int len;
    int idx = re_matchp(regex, str, &len);
    return idx;
}

internal void watch_callback(dmon_watch_id watch_id, dmon_action action, const char* root_dir, const char* filepath, const char* oldfilepath, void* user_data)
{
    AIL_UNUSED(user_data);
    AIL_UNUSED(watch_id);

    b32 matched = 0;
    for (u32 i = 0; i < regexs.len && !matched; i++) {
        matched = re_matches(regexs.data[i], filepath);
        if (oldfilepath) matched |= re_matches(regexs.data[i], filepath);
    }
    if (!matched) return;

    switch (action) {
        case DMON_ACTION_CREATE:
            log_info("Created [%s]%s...", root_dir, filepath);
            break;
        case DMON_ACTION_DELETE:
            log_info("Deleted [%s]%s...", root_dir, filepath);
            break;
        case DMON_ACTION_MODIFY:
            log_info("Modified [%s]%s...", root_dir, filepath);
            break;
        case DMON_ACTION_MOVE:
            log_info("Renamed [%s]%s to [%s]%s...", root_dir, oldfilepath, root_dir, filepath);
            break;
    }

    is_proc_waiting = true;
    while (is_proc_running) {}; // busy looping isn't very cool but eh ¯\_(ツ)_/¯
    is_proc_waiting = false;
    for (u32 i = 0; i < cmds.len; i++) {
        if (is_proc_waiting) break;
        is_proc_running = true;
        log_info("Running '%s'...", cmds.data[i]);
        ProcRes proc = proc_exec(&cmds.cmds[i], cmds.data[i], ail_default_allocator);
        if (ail_sv_ends_with_char(ail_sv_from_str(proc.out), '\n')) printf("%s", proc.out.str);
        else puts(proc.out.str);
        if (!proc.succ) {
            log_warn("'%s' failed", cmds.data[i]);
            break;
        }
        is_proc_running = false;
    }
}

#define collect_flag_vals(arg, argv, argc, idx, program, list) do {               \
    i64 _eq_idx = ail_sv_index_of_char(arg, '=');                                 \
    if (_eq_idx >= 0) {                                                           \
        ail_sv_split_next_char(&arg, '=', true);                                  \
        if (!arg.len) {                                                           \
            log_err("Expected a value after the equals sign in '%s'", argv[idx]); \
            printf("See detailed usage info by running `%s --help`\n", program);  \
        } else {                                                                  \
            list_push(dirs, (char*)arg.str);                                      \
        }                                                                         \
    } else {                                                                      \
        for (++idx; idx < argc && argv[idx][0] != '-'; idx++) {                   \
            list_push(dirs, argv[idx]);                                           \
        }                                                                         \
        idx--;                                                                    \
    }                                                                             \
} while(0)

int main(int argc, char **argv)
{
    AIL_ASSERT(argc > 0);
    char *program = argv[0];
    if (argc == 1) {
        log_err("Invalid Usage: Too few arguments");
        print_help(program);
        return 1;
    }

    if (argv[1][0] == '-') { // Flags are used in command line options (Usage variant 3)
        for (i32 i = 1; i < argc; i++) {
            AIL_SV arg = ail_sv_from_cstr(argv[i]);
            if (ail_sv_starts_with(arg, SV_LIT_T("-d")) || ail_sv_starts_with(arg, SV_LIT_T("--dir"))) {
                collect_flag_vals(arg, argv, argc, i, program, dirs);
            } else if (ail_sv_starts_with(arg, SV_LIT_T("-m")) || ail_sv_starts_with(arg, SV_LIT_T("--match"))) {
                collect_flag_vals(arg, argv, argc, i, program, regexs);
            } else if (ail_sv_starts_with(arg, SV_LIT_T("-c")) || ail_sv_starts_with(arg, SV_LIT_T("--cmd"))) {
                collect_flag_vals(arg, argv, argc, i, program, cmds);
            } else if (ail_sv_starts_with(arg, SV_LIT_T("-v")) || ail_sv_starts_with(arg, SV_LIT_T("--version"))) {
                print_version(program);
                return 0;
            } else if (ail_sv_starts_with(arg, SV_LIT_T("-h")) || ail_sv_starts_with(arg, SV_LIT_T("--help"))) {
                print_help(program);
                return 0;
            } else {
                if (ail_sv_starts_with_char(arg, '-')) log_err("Unknown flag '%s'", argv[i]);
                else log_err("Expected a flag, but received '%s' instead", argv[i]);
                printf("See detailed usage info by running `%s --help`\n", program);
                return 1;
            }
        }
        if (dirs.len == 0) {
            log_err("Invalid Usage: No directory specified");
            printf("See detailed usage info by running `%s --help`\n", program);
            return 1;
        }
        if (cmds.len == 0) {
            log_err("Invalid Usage: No command specified");
            printf("See detailed usage info by running `%s --help`\n", program);
            return 1;
        }
    } else { // Flags are not used
        if (argc == 2) {
            log_err("Invalid usage: Too few arguments");
            print_help(program);
            return 1;
        } if (argc == 3) { // Usage variant 1
            list_push(dirs, argv[1]);
            list_push(cmds, argv[2]);
        } else { // Usage variant 2
            list_push(dirs,   argv[1]);
            list_push(regexs, re_compile(argv[2]));
            for (i32 i = 3; i < argc; i++) {
                list_push(cmds, argv[i]);
            }
        }
    }

    for (u32 i = 0; i < cmds.len; i++) {
        AIL_DA(AIL_SV) parts = ail_sv_split_whitespace(ail_sv_from_cstr(cmds.data[i]), true);
        cmds.cmds[i] = ail_da_new_t(str);
        for (u32 j = 0; j < parts.len; j++) {
            ail_da_push(&cmds.cmds[i], ail_sv_copy_to_cstr(parts.data[j]));
        }
        // ail_da_push(&cmds.cmds[i], NULL);
    }

    dmon_init();
    log_info("Watching for file changes...");
    log_info("Quit with 'q'...");
    for (u32 i = 0; i < dirs.len; i++) {
        dmon_watch(dirs.data[i], watch_callback, DMON_WATCHFLAGS_RECURSIVE, NULL);
    }
    while ((getc(stdin) | 0x20) != 'q') {}
    dmon_deinit();

    return 0;
}
