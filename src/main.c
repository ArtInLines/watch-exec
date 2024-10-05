#include "header.h"

global StrList   dirs;
global RegexList regexs;
global CmdList   cmds;

internal void print_help(char *program)
{
    printf("%s: Execute commands whenever specific files are changed...\n", program);
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
    printf("When using option 3, the following syntax variants are available for specifying options:\n");
    printf("  1. <flag>=<value>\n");
    printf("  2. <flag> <value> [<value>]*\n");
    printf("Each flag is allowed to be provided several times\n");
    printf("The ordering of options doesn't matter except for the order of commands\n");
    printf("All commands are executed in the order that they are provided in when the specified files are changed\n");
    printf("\n");
    printf("Option flags:\n");
    printf("  -d|--dir:     Directory to match files inside of\n");
    printf("  -g|--glob:    Glob pattern to match file-names against\n");
    printf("  -r|--regex:   Regular Expression to match file-names against\n");
    printf("  -c|--cmd:     Command to execute when a matching file was changed\n");
    printf("  -h|--help:    Show this help message\n");
    printf("  -v|--version: Show the program's version\n");
    printf("\n");
    printf("The following syntax for regular expressions is supported:\n");
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
    printf("The following syntax for glob patterns is supported:\n");
    printf("  - '*':        match zero or more of any character\n");
    printf("  - '?':        match zero or one of any character\n");
    printf("  - '[abc]':    match if one of {'a', 'b', 'c'}\n");
    printf("  - '[^abc]':   match if NOT one of {'a', 'b', 'c'}\n");
    printf("  - '[a-zA-Z]': match the character set of the ranges { a-z | A-Z }\n");
    printf("\n");
    printf("While the program is running, you use the following commands:\n");
    printf("- 'q': quit the program\n");
    printf("- 'r': rerun all commands immediately\n");
}

internal void print_version(char *program)
{
    printf("Watch-Exec (%s): v%s\n", program, VERSION);
    printf("Copyright (C) 2024 Lily Val Richter\n");
}

internal void log_ail_pm_comp_err(AIL_PM_Exp_Type exp_type, AIL_PM_Err err, const char *str_to_compile)
{
    b32 show_idx = false;
    char *desc = "";
    switch (err.type) {
        case AIL_PM_ERR_UNKNOWN_EXP_TYPE:        AIL_UNREACHABLE(); break;
        case AIL_PM_ERR_LATE_START_MARKER:       desc = "Start Marker must be placed at the beginning or be escaped if you mean the character literal"; break;
        case AIL_PM_ERR_EARLY_END_MARKER:        desc = "End Marker must be placed at the end or be escaped if you mean the character literal"; break;
        case AIL_PM_ERR_INCOMPLETE_ESCAPE:       desc = "Incomplete Escape Sequence: If you mean the character literal, escape the escape character"; break;
        case AIL_PM_ERR_INVALID_COUNT_QUALIFIER: desc = "Unescaped Count Qualifier must appear after a valid element"; break;
        case AIL_PM_ERR_MISSING_BRACKET:         desc = "A bracket is missing to complete the character grouping"; break;
        case AIL_PM_ERR_INVALID_BRACKET:         desc = "Literal Closing Brackets must be escaped"; break;
        case AIL_PM_ERR_INVALID_RANGE:           desc = "Ranges must have a lower character on the left of the dash"; break;
        case AIL_PM_ERR_INVALID_RANGE_SYNTAX:    desc = "Invalid syntax for character range"; break;
        case AIL_PM_ERR_EMPTY_GROUP:             desc = "Empty character groups are not allowed"; break;
        case AIL_PM_ERR_INCOMPLETE_RANGE:        desc = "Incompletes character ranges are not allowed"; break;
        case AIL_PM_ERR_INVALID_SPECIAL_CHAR:    desc = "Special Characters are not allowed here - escape the character if you mean the character literal"; break;
        case AIL_PM_ERR_COUNT:                   AIL_UNREACHABLE(); break;
    }
    log_err("Failed to parse the following '%s' pattern: %s:", ail_pm_exp_to_str(exp_type), desc);
    log_err("  '%s'", str_to_compile);
    if (show_idx) log_err("   %*c", err.idx, '^');
}

internal void run_cmds(void)
{
    for (u32 i = 0; i < cmds.len; i++) {
        SubProcRes proc = subproc_exec(&cmds.cmds[i], cmds.data[i], ail_default_allocator);
        if (!proc.finished) {
            log_err("'%s' couldn't be executed properly", cmds.data[i]);
            break;
        } else if (proc.exitCode) {
            log_warn("'%s' failed with exit Code %d", cmds.data[i], proc.exitCode);
            break;
        } else {
            log_succ("'%s' ran successfully", cmds.data[i]);
        }
    }
}

internal void watch_callback(dmon_watch_id watch_id, dmon_action action, const char* root_dir, const char* filepath, const char* oldfilepath, void* user_data)
{
    AIL_UNUSED(user_data);
    AIL_UNUSED(watch_id);
    AIL_SV fpath_sv = ail_sv_from_cstr((char*)filepath);
    b32 matched = regexs.len == 0;
    for (u32 i = 0; !matched && i < regexs.len; i++) {
        matched = ail_pm_matches_sv(regexs.data[i], fpath_sv);
        if (oldfilepath) matched |= ail_pm_matches_sv(regexs.data[i], fpath_sv);
    }
    if (!matched) return;

    switch (action) {
        case DMON_ACTION_CREATE:
            log_info("Created %s%s...", root_dir, filepath);
            break;
        case DMON_ACTION_DELETE:
            log_info("Deleted %s%s...", root_dir, filepath);
            break;
        case DMON_ACTION_MODIFY:
            log_info("Modified %s%s...", root_dir, filepath);
            break;
        case DMON_ACTION_MOVE:
            log_info("Renamed %s%s to %s%s...", root_dir, oldfilepath, root_dir, filepath);
            break;
    }
    run_cmds();
}

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
        for (i32 i = 1; i < argc; ) {
            AIL_SV arg = ail_sv_from_cstr(argv[i]);
            // @Cleanup: Lots of code duplication, but couldn't figure out how to compress it nicely ¯\_(ツ)_/¯
            if (ail_sv_starts_with(arg, SV_LIT_T("-d")) || ail_sv_starts_with(arg, SV_LIT_T("--dir"))) {
                i64 _eq_idx = ail_sv_find_char(arg, '=');
                if (_eq_idx >= 0) {
                    ail_sv_split_next_char(&arg, '=', true);
                    if (!arg.len) {
                        log_err("Expected a value after the equals sign in '%s'", argv[i]);
                        printf("See detailed usage info by running `%s --help`\n", program);
                    } else {
                        list_push(dirs, (char*)arg.str);
                    }
                } else {
                    for (++i; i < argc && argv[i][0] != '-'; i++) {
                        list_push(dirs, argv[i]);
                    }
                }
            } else if (ail_sv_starts_with(arg, SV_LIT_T("-g")) || ail_sv_starts_with(arg, SV_LIT_T("--glob"))) {
                i64 _eq_idx = ail_sv_find_char(arg, '=');
                if (_eq_idx >= 0) {
                    ail_sv_split_next_char(&arg, '=', true);
                    if (!arg.len) {
                        log_err("Expected a value after the equals sign in '%s'", argv[i]);
                        printf("See detailed usage info by running `%s --help`\n", program);
                    } else {
                        AIL_PM_Comp_Res comp_res = ail_pm_compile_sv_a(arg, AIL_PM_EXP_GLOB, ail_default_allocator);
                        if (comp_res.failed) {
                            log_ail_pm_comp_err(AIL_PM_EXP_GLOB, comp_res.err, arg.str);
                            return 1;
                        } else list_push(regexs, comp_res.pattern);
                    }
                } else {
                    for (++i; i < argc && argv[i][0] != '-'; i++) {
                        AIL_SV a = ail_sv_from_cstr(argv[i]);
                        AIL_PM_Comp_Res comp_res = ail_pm_compile_sv_a(a, AIL_PM_EXP_GLOB, ail_default_allocator);
                        if (comp_res.failed) {
                            log_ail_pm_comp_err(AIL_PM_EXP_GLOB, comp_res.err, a.str);
                            return 1;
                        } else list_push(regexs, comp_res.pattern);
                    }
                }
            } else if (ail_sv_starts_with(arg, SV_LIT_T("-r")) || ail_sv_starts_with(arg, SV_LIT_T("--regex"))) {
                i64 _eq_idx = ail_sv_find_char(arg, '=');
                if (_eq_idx >= 0) {
                    ail_sv_split_next_char(&arg, '=', true);
                    if (!arg.len) {
                        log_err("Expected a value after the equals sign in '%s'", argv[i]);
                        printf("See detailed usage info by running `%s --help`\n", program);
                    } else {
                        AIL_PM_Comp_Res comp_res = ail_pm_compile_sv_a(arg, AIL_PM_EXP_REGEX, ail_default_allocator);
                        if (comp_res.failed) {
                            log_ail_pm_comp_err(AIL_PM_EXP_REGEX, comp_res.err, arg.str);
                            return 1;
                        } else list_push(regexs, comp_res.pattern);
                    }
                } else {
                    for (++i; i < argc && argv[i][0] != '-'; i++) {
                        AIL_SV a = ail_sv_from_cstr(argv[i]);
                        AIL_PM_Comp_Res comp_res = ail_pm_compile_sv_a(a, AIL_PM_EXP_REGEX, ail_default_allocator);
                        if (comp_res.failed) {
                            log_ail_pm_comp_err(AIL_PM_EXP_REGEX, comp_res.err, a.str);
                            return 1;
                        } else list_push(regexs, comp_res.pattern);
                    }
                }
            } else if (ail_sv_starts_with(arg, SV_LIT_T("-c")) || ail_sv_starts_with(arg, SV_LIT_T("--cmd"))) {
                i64 _eq_idx = ail_sv_find_char(arg, '=');
                if (_eq_idx >= 0) {
                    ail_sv_split_next_char(&arg, '=', true);
                    if (!arg.len) {
                        log_err("Expected a value after the equals sign in '%s'", argv[i]);
                        printf("See detailed usage info by running `%s --help`\n", program);
                    } else {
                        list_push(cmds, (char*)arg.str);
                    }
                } else {
                    for (++i; i < argc && argv[i][0] != '-'; i++) {
                        list_push(cmds, argv[i]);
                    }
                }
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
            list_push(dirs, argv[1]);
            AIL_SV arg = ail_sv_from_cstr(argv[2]);
            AIL_PM_Comp_Res comp_res = ail_pm_compile_sv_a(arg, AIL_PM_EXP_GLOB, ail_default_allocator);
            if (comp_res.failed) {
                log_ail_pm_comp_err(AIL_PM_EXP_GLOB, comp_res.err, arg.str);
                return 1;
            } else list_push(regexs, comp_res.pattern);
            for (i32 i = 3; i < argc; i++) list_push(cmds, argv[i]);
        }
    }

    for (u32 i = 0; i < cmds.len; i++) {
        AIL_DA(AIL_SV) parts = ail_sv_split_whitespace(ail_sv_from_cstr(cmds.data[i]), true);
        cmds.cmds[i] = ail_da_new_t(str);
        for (u32 j = 0; j < parts.len; j++) {
            ail_da_push(&cmds.cmds[i], ail_sv_to_cstr(parts.data[j]));
        }
    }

#if 0
    printf("Dirs:\n");
    for (u32 i = 0; i < dirs.len; i++) {
        printf("  > %s\n", dirs.data[i]);
    }
    printf("Regexs:\n");
    for (u32 i = 0; i < regexs.len; i++) {
        char buf[1024];
        int n = ail_pm_pattern_to_str(regexs.data[i], buf, sizeof(buf));
        AIL_SV sv   = ail_sv_from_parts(buf, n);
        AIL_Str str = ail_sv_replace(sv, SV_LIT_T("\n"), SV_LIT_T("\n    "));
        printf("  > %s\n", str.str);
    }
    printf("Cmds:\n");
    for (u32 i = 0; i < cmds.len; i++) {
        printf("  > %s\n", cmds.data[i]);
    }
#endif

    term_init();
    subproc_init();
    dmon_init();
    log_info("Watching for file changes...");
    log_info("Quit with 'q', rerun all commands with 'r'...");
    for (u32 i = 0; i < dirs.len; i++) {
        dmon_watch(dirs.data[i], watch_callback, DMON_WATCHFLAGS_RECURSIVE, NULL);
    }
    for (;;) {
        char c = (term_get_char() | 0x20);
        if (c == 'q') break;
        if (c == 'r') run_cmds();
    }
    dmon_deinit();
    subproc_deinit();
    term_deinit();
    return 0;
}
