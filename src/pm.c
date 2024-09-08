// @Note: pm stands for pattern match
// @Note: This implementation was lightly inspired by the tiny-regex-c library here: https://github.com/kokke/tiny-regex-c


/* Documentation

Currently, regex and glob patterns are supported (see list of supported syntax below)

To match a pattern, you first need to compile it with `pm_compile`
Then you can match strings against the pattern with `pm_match`

 *** REGEX Support ***
 * '.'        Dot, matches any character
 * '^'        Start anchor, matches beginning of string
 * '$'        End anchor, matches end of string
 * '*'        Asterisk, match zero or more (greedy)
 * '+'        Plus, match one or more (greedy)
 * '?'        Question, match zero or one (non-greedy)
 * '[abc]'    Character class, match if one of {'a', 'b', 'c'}
 * '[^abc]'   Inverted class, match if NOT one of {'a', 'b', 'c'} -- @Bug: feature is currently broken!
 * '[a-zA-Z]' Character ranges, the character set of the ranges { a-z | A-Z }
 * '\s'       Whitespace, \t \f \r \n \v and spaces
 * '\S'       Non-whitespace
 * '\w'       Alphanumeric, [a-zA-Z0-9_]
 * '\W'       Non-alphanumeric
 * '\d'       Digits, [0-9]
 * '\D'       Non-digits

 *** GLOB Support ***
 * '*'        Asterisk, matches any character zero or more times
 * '?'        Question, matches one or zero characters 
 * '[abc]'    Character class, match if one of {'a', 'b', 'c'}
 * '[a-zA-Z]' Character ranges, the character set of the ranges { a-z | A-Z }

*/

#include "header.h"

typedef enum PM_Exp_Type {
    PM_EXP_REGEX,
    PM_EXP_GLOB,
    PM_EXP_COUNT,
} PM_Exp_Type;

typedef enum PM_Count_Type {
    PM_COUNT_ONCE,
    PM_COUNT_ZERO_PLUS,
    PM_COUNT_ONE_PLUS,
    PM_COUNT_ONE_OR_NONE,
    PM_COUNT_COUNT,
} PM_Count_Type;

typedef enum PM_El_Type {
    PM_EL_CHAR,
    PM_EL_ANY,
    PM_EL_ONE_OF,
    PM_EL_DIGIT,
    PM_EL_ALPHA,
    PM_EL_ALPHANUM,
    PM_EL_WHITESPACE,
    PM_EL_COUNT,
} PM_El_Type;

typedef struct PM_Range {
    char start;
    char end;
} PM_Range;
AIL_DA_INIT(PM_Range);
AIL_LIST_INIT(PM_Range);

// @Memory: This struct takes up much more space than neccessary rn (pack attributes together to improve this)
// @Note: The implementation uses the assumption that the 0-value for PM_El means that exactly one non-inverted character with c=='\0'
typedef struct PM_El {
    PM_El_Type    type;
    PM_Count_Type count;
    b32           inverted;
    union {
        char     c;
        PM_Range r;
        AIL_LIST(char)     cs;
        AIL_LIST(PM_Range) rs;
    };
} PM_El;
AIL_DA_INIT(PM_El);
AIL_LIST_INIT(PM_El);

// Used as bitmasks
typedef enum PM_Pattern_Attr {
    PM_ATTR_START = 1,
    PM_ATTR_END   = 2,
} PM_Pattern_Attr;

typedef struct PM_Pattern {
    PM_El          *els;
    u32             len;
    PM_Pattern_Attr attrs;
} PM_Pattern;

typedef enum PM_Err_Type {
    PM_ERR_UNKNOWN_EXP_TYPE,
    PM_ERR_LATE_START_MARKER,
    PM_ERR_EARLY_END_MARKER,
    PM_ERR_INCOMPLETE_ESCAPE,
    PM_ERR_INVALID_COUNT_QUALIFIER,
    PM_ERR_MISSING_BRACKET,
    PM_ERR_INVALID_BRACKET,
    PM_ERR_INVALID_RANGE,
    PM_ERR_INVALID_RANGE_SYNTAX,
    PM_ERR_EMPTY_GROUP,
    PM_ERR_INCOMPLETE_RANGE,
    PM_ERR_INVALID_SPECIAL_CHAR,
    PM_ERR_COUNT,
} PM_Err_Type;

typedef struct PM_Err {
    PM_Err_Type type;
    u32         idx;
} PM_Err;

typedef struct PM_Comp_Char_Res {
    char c;
    PM_Err_Type e; // No error occured, if this is equal to PM_ERR_COUNT
} PM_Comp_Char_Res;

typedef struct PM_Comp_El_Res {
    b32 failed;
    union {
        PM_El  el;
        PM_Err err;
    };
} PM_Comp_El_Res;

typedef struct PM_Comp_Res {
    b32 failed;
    union {
        PM_Pattern pattern;
        PM_Err     err;
    };
} PM_Comp_Res;



internal char* pm_exp_to_str(PM_Exp_Type type)
{
    char *s = "";
    switch (type) {
        case PM_EXP_GLOB:  s = "Glob"; break;
        case PM_EXP_REGEX: s = "Regular Expression"; break;
        case PM_EXP_COUNT: AIL_UNREACHABLE();
    }
    return s;
}

internal PM_Comp_Char_Res _pm_comp_group_char(const char *p, u32 plen, u32 *idx)
{
    PM_Comp_Char_Res res = {0};
    u32 i = *idx;
    if (p[i] == '\\') {
        if (++i >= plen) res.e = PM_ERR_INCOMPLETE_ESCAPE;
    } else if (p[i] == '^' || p[i] == '-' || p[i] == '[') {
         res.e = PM_ERR_INVALID_SPECIAL_CHAR;
    } else {
        res.c = p[i];
    }
    *idx = i;
    return res;
}

internal PM_Comp_El_Res _pm_comp_group(const char *p, u32 plen, u32 *idx, AIL_Allocator allocator)
{
    u32 i = *idx;
    PM_El el = { .type = PM_EL_ONE_OF };
    if (++i >= plen || i+1 >= plen) goto missing_bracket;
    if (p[i] == '^') {
        el.inverted = 1;
        i++;
    }
    
    if (p[i+1] == '-') {
        AIL_DA(PM_Range) ranges = ail_da_new_with_alloc(PM_Range, 4, allocator);
        for (; i < plen && p[i] != ']'; i += 3) {
            PM_Err_Type err_type = PM_ERR_COUNT;
            PM_Range r;
            PM_Comp_Char_Res x = _pm_comp_group_char(p, plen, &i);
            if (x.e != PM_ERR_COUNT) { err_type = x.e; goto report_err; }
            r.start = x.c;
                        
            if (i+2 >= plen)   { err_type = PM_ERR_INCOMPLETE_RANGE;     goto report_err; }
            if (p[++i] != '-') { err_type = PM_ERR_INVALID_RANGE_SYNTAX; goto report_err; } 
            ++i;
            
            x = _pm_comp_group_char(p, plen, &i);
            if (x.e != PM_ERR_COUNT) { err_type = x.e; goto report_err; }
            r.end = x.c;

report_err:
            if (err_type != PM_ERR_COUNT) return (PM_Comp_El_Res){.failed=1, .err={.type=err_type, .idx=i}};
            ail_da_push(&ranges, r);
        }
        
        if (ranges.len == 0) {
            return (PM_Comp_El_Res){.failed=1, .err={.type=PM_ERR_EMPTY_GROUP, .idx=i-1}};
        } else if (ranges.len == 1) {
            el.r = ranges.data[i];
            ail_da_free(&ranges);
        } else {
            el.rs = ail_list_from_da_t(PM_Range, ranges);
        }
    }
    else {
        AIL_DA(char) chars = ail_da_new_with_alloc(char, 4, allocator);
        for (; i < plen && p[i] != ']'; i++) {
            PM_Comp_Char_Res x = _pm_comp_group_char(p, plen, &i);
            if (x.e != PM_ERR_COUNT) return (PM_Comp_El_Res) {.failed=1, .err={.type=x.e, .idx=i}};
            ail_da_push(&chars, x.c);
        }
        
        if (chars.len == 0) {
            return (PM_Comp_El_Res){.failed=1, .err={.type=PM_ERR_EMPTY_GROUP, .idx=i-1}};
        } else if (chars.len == 1) {
            el.c = chars.data[i];
            ail_da_free(&chars);
        } else {
            el.cs = ail_list_from_da_t(char, chars);
        }
    }

    *idx = i;
    if (i < plen) { // No missing brackets
        AIL_ASSERT(p[*idx++] == ']');
        return (PM_Comp_El_Res){ .el = el };
    }
missing_bracket:
    return (PM_Comp_El_Res){.failed=1, .err={.type=PM_ERR_MISSING_BRACKET, .idx=i}};
    
}

internal PM_Comp_Res pm_compile(const char *p, u32 plen, PM_Exp_Type exp_type, AIL_Allocator allocator)
{
    AIL_DA(PM_El) els     = ail_da_new_with_alloc(PM_El, 32, allocator);
    PM_Pattern_Attr attrs = 0;    
    if (exp_type >= PM_EXP_COUNT) return (PM_Comp_Res){.failed=1, .err={.type=PM_ERR_UNKNOWN_EXP_TYPE}};
    for (u32 i = 0; i < plen; i++) {
        char c = p[i];
        switch (exp_type) {
            case PM_EXP_REGEX: switch (c) {
                case '.':
                    ail_da_push(&els, (PM_El){.type=PM_EL_ANY});
                    break;
                case '^':
                    if (i > 0) return (PM_Comp_Res){.failed=1, .err={.type=PM_ERR_LATE_START_MARKER, .idx=i}};
                    attrs |= PM_ATTR_START;
                    break;
                case '$':
                    if (i+1 < plen) return (PM_Comp_Res){.failed=1, .err={.type=PM_ERR_EARLY_END_MARKER, .idx=i}};
                    attrs |= PM_ATTR_END;
                    break;
                case '*':
                    if (els.len == 0 || els.data[els.len-1].count) return (PM_Comp_Res){.failed=1, .err={.type=PM_ERR_INVALID_COUNT_QUALIFIER, .idx=i}};
                    els.data[els.len-1].count = PM_COUNT_ZERO_PLUS;
                    break;
                case '+':
                    if (els.len == 0 || els.data[els.len-1].count) return (PM_Comp_Res){.failed=1, .err={.type=PM_ERR_INVALID_COUNT_QUALIFIER, .idx=i}};
                    els.data[els.len-1].count = PM_COUNT_ONE_PLUS;
                    break;
                case '?':
                    if (els.len == 0 || els.data[els.len-1].count) return (PM_Comp_Res){.failed=1, .err={.type=PM_ERR_INVALID_COUNT_QUALIFIER, .idx=i}};
                    els.data[els.len-1].count = PM_COUNT_ONE_OR_NONE;
                    break;
                case ']':
                    return (PM_Comp_Res){.failed=1, .err={.type=PM_ERR_INVALID_BRACKET, .idx=1}};
                case '[':
                    PM_Comp_El_Res x = _pm_comp_group(p, plen, &i, allocator);
                    if (x.failed) return (PM_Comp_Res){.failed=1, .err=x.err};
                    else ail_da_push(&els, x.el);
                    break;
                case '\\':
                    if (i+1 == plen) return (PM_Comp_Res){.failed=1, .err={.type=PM_ERR_INCOMPLETE_ESCAPE, .idx=i}};
                    PM_El el = {0};
                    switch (p[++i]) {
                        case 's': el.type=PM_EL_WHITESPACE; el.inverted=0; break;
                        case 'S': el.type=PM_EL_WHITESPACE; el.inverted=1; break;
                        case 'w': el.type=PM_EL_ALPHANUM;   el.inverted=0; break;
                        case 'W': el.type=PM_EL_ALPHANUM;   el.inverted=1; break;
                        case 'd': el.type=PM_EL_DIGIT;      el.inverted=0; break;
                        case 'D': el.type=PM_EL_DIGIT;      el.inverted=1; break;
                        default:  el.c = p[i];                             break;
                    }
                    ail_da_push(&els, el);
                    break;
                default:
                    ail_da_push(&els, (PM_El){.c=c});
                    break;
            } break;
            case PM_EXP_GLOB: switch (c) {
                case '*': {
                    PM_El el = {
                        .type  = PM_EL_ANY,
                        .count = PM_COUNT_ZERO_PLUS
                    };
                    ail_da_push(&els, el);
                } break;
                case '?': {
                    PM_El el = {
                        .type  = PM_EL_ANY,
                        .count = PM_COUNT_ONE_OR_NONE
                    };
                    ail_da_push(&els, el);
                } break;
                case ']':
                    return (PM_Comp_Res){.failed=1, .err={.type=PM_ERR_INVALID_BRACKET, .idx=1}};
                case '[':
                    PM_Comp_El_Res x = _pm_comp_group(p, plen, &i, allocator);
                    if (x.failed) return (PM_Comp_Res){.failed=1, .err=x.err};
                    else ail_da_push(&els, x.el);
                    break;
                case '\\':
                    if (i+1 == plen) return (PM_Comp_Res){.failed=1, .err={.type=PM_ERR_INCOMPLETE_ESCAPE, .idx=i}};
                    ail_da_push(&els, (PM_El){.c=p[++i]});
                    break;
                default:
                    ail_da_push(&els, (PM_El){.c=c});
                    break;
            } break;
            case PM_EXP_COUNT: AIL_UNREACHABLE();
        }
    }
    
    return (PM_Comp_Res) { .pattern = {
        .els    = els.data,
        .len    = els.len,
        .attrs  = attrs,
    }};
}

internal PM_Comp_Res pm_compile_sv(AIL_SV pattern, PM_Exp_Type type, AIL_Allocator allocator)
{
    return pm_compile(pattern.str, pattern.len, type, allocator);
}


internal void pm_free(PM_Pattern pattern, AIL_Allocator allocator)
{
    AIL_CALL_FREE(allocator, pattern.els);
}

// @TODO: pm_match()

internal bool pm_matches(PM_Pattern pattern, const char *s, u32 slen)
{
    AIL_UNUSED(pattern);
    AIL_UNUSED(s);
    AIL_UNUSED(slen);
    AIL_TODO();
    return true;
}

internal bool pm_matches_sv(PM_Pattern pattern, AIL_SV sv)
{
    return pm_matches(pattern, sv.str, sv.len);
}
