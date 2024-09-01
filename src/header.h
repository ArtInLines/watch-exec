#ifndef _HEADER_H
#define _HEADER_H

#define VERSION "1.2"

// @TODO: Features to add:
// - ignore folders
// - allow specifying seperate commands for seperate dirs/matches
// - provide non-recursive option
// - run subprocess in seperate thread so they can be killed at any time to start again
// - work with unicode instead of ascii
// - provide non-regex options (maybe glob? maybe flat text?)

#include <stdio.h>
#define DMON_IMPL
#include "../deps/dmon/dmon.h"
#define AIL_ALL_IMPL
#include "../deps/ail/ail.h"
#define AIL_SV_IMPL
#include "../deps/ail/ail_sv.h"

#define SV_LIT AIL_SV_FROM_LITERAL
#define SV_LIT_T AIL_SV_FROM_LITERAL_T

#include "log.c"
#define RE_DOT_MATCHES_NEWLINE 0
#include "re.c"
#include "subproc.c"

#define BUFFER_LEN 32
typedef struct StrList {
    u32 len;
    char* data[BUFFER_LEN];
} StrList;
typedef struct RegexList {
    u32 len;
    re_t* data[BUFFER_LEN];
} RegexList;
typedef struct CmdList {
    u32 len;
    str data[BUFFER_LEN];
    AIL_DA(str) cmds[BUFFER_LEN];
} CmdList;
#define list_push(list, el) ((list).data[(list).len++] = (el))

#endif // _HEADER_H
