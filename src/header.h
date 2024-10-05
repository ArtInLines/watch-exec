#ifndef _HEADER_H
#define _HEADER_H

#define VERSION "1.5"

// @TODO: Features to add:
// - ignore folders
// - allow specifying seperate commands for seperate dirs/matches
// - provide non-recursive option
// - work with unicode instead of ascii

#include <stdio.h>
#include <threads.h>
#define DMON_IMPL
#include "../deps/dmon/dmon.h"
#define AIL_ALL_IMPL
#define AIL_SV_IMPL
#define AIL_PM_IMPL
#include "../deps/ail/ail.h"
#include "../deps/ail/ail_sv.h"
#include "../deps/ail/ail_pm.h"

#define SV_LIT   AIL_SV_FROM_LITERAL
#define SV_LIT_T AIL_SV_FROM_LITERAL_T

#include "term.c"
#include "log.c"
#include "subproc.c"
#include "thread.c"

#define BUFFER_LEN 32
typedef struct StrList {
    u32 len;
    char* data[BUFFER_LEN];
} StrList;
typedef struct RegexList {
    u32 len;
    AIL_PM_Pattern data[BUFFER_LEN];
} RegexList;
typedef struct CmdList {
    u32 len;
    str data[BUFFER_LEN];
    AIL_DA(str) cmds[BUFFER_LEN];
} CmdList;
#define list_push(list, el) ((list).data[(list).len++] = (el))

#endif // _HEADER_H
