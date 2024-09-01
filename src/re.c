/*
* This is a modified version of the tiny-regex-c library: https://github.com/kokke/tiny-regex-c
* The original code was inspired by Rob Pike (http://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html)
* The original code was released into the public domain (see below).
* I adapted the code for my own purposes and release the modified version under the MIT license (see below)

*** License of original tiny-regex-c library ***

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org>


*** License regarding modified code ***

Copyright (c) 2024 Lily Val Richter

Permission is hereby granted, free_one of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/* Documentation

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
 * '*'        Asterisk, matches any character
 * '?'        Question, matches one or zero characters 
 * '[abc]'    Character class, match if one of {'a', 'b', 'c'}
 * '[a-zA-Z]' Character ranges, the character set of the ranges { a-z | A-Z }

*/

#include "header.h"

#ifndef RE_MAX_REGEXPS // Max number of regex symbols in an expression
#   define RE_MAX_REGEXPS 64
#endif

#ifndef RE_MAX_CHAR_CLASS_LEN // Max number of characters in a character-class
#   define RE_MAX_CHAR_CLASS_LEN 64
#endif

typedef enum RE_Type {
    RE_UNUSED,
    RE_DOT,
    RE_BEGIN,
    RE_END,
    RE_QUESTIONMARK,
    RE_STAR,
    RE_PLUS,
    RE_CHAR,
    RE_CHAR_CLASS,
    RE_INV_CHAR_CLASS,
    RE_DIGIT,
    RE_NOT_DIGIT,
    RE_ALPHA,
    RE_NOT_ALPHA,
    RE_WHITESPACE,
    RE_NOT_WHITESPACE,
    /* RE_BRANCH */
} RE_Type;

typedef struct re_t
{
  unsigned char  type;   /* RE_CHAR, RE_STAR, etc.                      */
  union
  {
    unsigned char  ch;   /*      the character itself             */
    unsigned char* ccl;  /*  OR  a pointer to characters in class */
  } u;
} re_t;

internal int re_match(const char* pattern, const char* text, int* matchlength);
internal int re_matchp(re_t* pattern, const char* text, int* matchlength);
internal re_t* re_compile(const char* pattern);
internal int re_to_str(re_t* pattern, char *buffer, int bufferlen);

internal int matchpattern(re_t* pattern, const char* text, int* matchlength);
internal int matchcharclass(char c, const char* str);
internal int matchstar(re_t p, re_t* pattern, const char* text, int* matchlength);
internal int matchplus(re_t p, re_t* pattern, const char* text, int* matchlength);
internal int matchone(re_t p, char c);
internal int matchdigit(char c);
internal int matchalpha(char c);
internal int matchwhitespace(char c);
internal int matchmetachar(char c, const char* str);
internal int matchrange(char c, const char* str);
internal int matchdot(char c);
internal int ismetachar(char c);



int re_match(const char* pattern, const char* text, int* matchlength)
{
  return re_matchp(re_compile(pattern), text, matchlength);
}

int re_matchp(re_t* pattern, const char* text, int* matchlength)
{
  *matchlength = 0;
  if (pattern != 0)
  {
    if (pattern[0].type == RE_BEGIN)
    {
      return ((matchpattern(&pattern[1], text, matchlength)) ? 0 : -1);
    }
    else
    {
      int idx = -1;

      do
      {
        idx += 1;

        if (matchpattern(pattern, text, matchlength))
        {
          if (text[0] == '\0')
            return -1;

          return idx;
        }
      }
      while (*text++ != '\0');
    }
  }
  return -1;
}

re_t* re_compile(const char* pattern)
{
  /* The sizes of the two static arrays below substantiates the static RAM usage of this module.
     RE_MAX_REGEXPS is the max number of symbols in the expression.
     RE_MAX_CHAR_CLASS_LEN determines the size of buffer for chars in all char-classes in the expression. */
  static re_t re_compiled[RE_MAX_REGEXPS];
  static unsigned char ccl_buf[RE_MAX_CHAR_CLASS_LEN];
  int ccl_bufidx = 1;

  char c;     /* current char in pattern   */
  int i = 0;  /* index into pattern        */
  int j = 0;  /* index into re_compiled    */

  while (pattern[i] != '\0' && (j+1 < RE_MAX_REGEXPS))
  {
    c = pattern[i];

    switch (c)
    {
      /* Meta-characters: */
      case '^': {    re_compiled[j].type = RE_BEGIN;           } break;
      case '$': {    re_compiled[j].type = RE_END;             } break;
      case '.': {    re_compiled[j].type = RE_DOT;             } break;
      case '*': {    re_compiled[j].type = RE_STAR;            } break;
      case '+': {    re_compiled[j].type = RE_PLUS;            } break;
      case '?': {    re_compiled[j].type = RE_QUESTIONMARK;    } break;
/*    case '|': {    re_compiled[j].type = BRANCH;          } break; <-- not working properly */

      /* Escaped character-classes (\s \w ...): */
      case '\\':
      {
        if (pattern[i+1] != '\0')
        {
          /* Skip the escape-char '\\' */
          i += 1;
          /* ... and check the next */
          switch (pattern[i])
          {
            /* Meta-character: */
            case 'd': {    re_compiled[j].type = RE_DIGIT;            } break;
            case 'D': {    re_compiled[j].type = RE_NOT_DIGIT;        } break;
            case 'w': {    re_compiled[j].type = RE_ALPHA;            } break;
            case 'W': {    re_compiled[j].type = RE_NOT_ALPHA;        } break;
            case 's': {    re_compiled[j].type = RE_WHITESPACE;       } break;
            case 'S': {    re_compiled[j].type = RE_NOT_WHITESPACE;   } break;

            /* Escaped character, e.g. '.' or '$' */
            default:
            {
              re_compiled[j].type = RE_CHAR;
              re_compiled[j].u.ch = pattern[i];
            } break;
          }
        }
        /* '\\' as last char in pattern -> invalid regular expression. */
/*
        else
        {
          re_compiled[j].type = RE_CHAR;
          re_compiled[j].ch = pattern[i];
        }
*/
      } break;

      /* Character class: */
      case '[':
      {
        /* Remember where the char-buffer starts. */
        int buf_begin = ccl_bufidx;

        /* Look-ahead to determine if negated */
        if (pattern[i+1] == '^')
        {
          re_compiled[j].type = RE_INV_CHAR_CLASS;
          i += 1; /* Increment i to avoid including '^' in the char-buffer */
          if (pattern[i+1] == 0) /* incomplete pattern, missing non-zero char after '^' */
          {
            return 0;
          }
        }
        else
        {
          re_compiled[j].type = RE_CHAR_CLASS;
        }

        /* Copy characters inside [..] to buffer */
        while (    (pattern[++i] != ']')
                && (pattern[i]   != '\0')) /* Missing ] */
        {
          if (pattern[i] == '\\')
          {
            if (ccl_bufidx >= RE_MAX_CHAR_CLASS_LEN - 1)
            {
              //fputs("exceeded internal buffer!\n", stderr);
              return 0;
            }
            if (pattern[i+1] == 0) /* incomplete pattern, missing non-zero char after '\\' */
            {
              return 0;
            }
            ccl_buf[ccl_bufidx++] = pattern[i++];
          }
          else if (ccl_bufidx >= RE_MAX_CHAR_CLASS_LEN)
          {
              //fputs("exceeded internal buffer!\n", stderr);
              return 0;
          }
          ccl_buf[ccl_bufidx++] = pattern[i];
        }
        if (ccl_bufidx >= RE_MAX_CHAR_CLASS_LEN)
        {
            /* Catches cases such as [00000000000000000000000000000000000000][ */
            //fputs("exceeded internal buffer!\n", stderr);
            return 0;
        }
        /* Null-terminate string end */
        ccl_buf[ccl_bufidx++] = 0;
        re_compiled[j].u.ccl = &ccl_buf[buf_begin];
      } break;

      /* Other characters: */
      default:
      {
        re_compiled[j].type = RE_CHAR;
        re_compiled[j].u.ch = c;
      } break;
    }
    /* no buffer-out-of-bounds access on invalid patterns - see https://github.com/kokke/tiny-regex-c/commit/1a279e04014b70b0695fba559a7c05d55e6ee90b */
    if (pattern[i] == 0)
    {
      return 0;
    }

    i += 1;
    j += 1;
  }
  /* 'RE_UNUSED' is a sentinel used to indicate end-of-pattern */
  re_compiled[j].type = RE_UNUSED;

  return re_compiled;
}

int re_to_str(re_t* pattern, char *buffer, int bufferlen)
{
  const char* types[] = { "UNUSED", "DOT", "BEGIN", "END", "QUESTIONMARK", "STAR", "PLUS", "CHAR", "CHAR_CLASS", "INV_CHAR_CLASS", "DIGIT", "NOT_DIGIT", "ALPHA", "NOT_ALPHA", "WHITESPACE", "NOT_WHITESPACE", "BRANCH" };

  int i;
  int j;
  char c;
  int n = 0;
  for (i = 0; i < RE_MAX_REGEXPS; ++i)
  {
    if (pattern[i].type == RE_UNUSED)
    {
      break;
    }

    n += snprintf(buffer + n, bufferlen - n, "type: %s", types[pattern[i].type]);
    if (pattern[i].type == RE_CHAR_CLASS || pattern[i].type == RE_INV_CHAR_CLASS)
    {
      n += snprintf(buffer + n, bufferlen - n, " [");
      for (j = 0; j < RE_MAX_CHAR_CLASS_LEN; ++j)
      {
        c = pattern[i].u.ccl[j];
        if ((c == '\0') || (c == ']'))
        {
          break;
        }
        n += snprintf(buffer + n, bufferlen - n, "%c", c);
      }
      n += snprintf(buffer + n, bufferlen - n, "]");
    }
    else if (pattern[i].type == RE_CHAR)
    {
      n += snprintf(buffer + n, bufferlen - n, " '%c'", pattern[i].u.ch);
    }
    n += snprintf(buffer + n, bufferlen - n, "\n");
  }
  return n;
}

static int matchdigit(char c)
{
  return isdigit(c);
}
static int matchalpha(char c)
{
  return isalpha(c);
}
static int matchwhitespace(char c)
{
  return isspace(c);
}
static int matchalphanum(char c)
{
  return ((c == '_') || matchalpha(c) || matchdigit(c));
}
static int matchrange(char c, const char* str)
{
  return (    (c != '-')
           && (str[0] != '\0')
           && (str[0] != '-')
           && (str[1] == '-')
           && (str[2] != '\0')
           && (    (c >= str[0])
                && (c <= str[2])));
}
static int matchdot(char c)
{
#if defined(RE_DOT_MATCHES_NEWLINE) && (RE_DOT_MATCHES_NEWLINE == 1)
  (void)c;
  return 1;
#else
  return c != '\n' && c != '\r';
#endif
}
static int ismetachar(char c)
{
  return ((c == 's') || (c == 'S') || (c == 'w') || (c == 'W') || (c == 'd') || (c == 'D'));
}

static int matchmetachar(char c, const char* str)
{
  switch (str[0])
  {
    case 'd': return  matchdigit(c);
    case 'D': return !matchdigit(c);
    case 'w': return  matchalphanum(c);
    case 'W': return !matchalphanum(c);
    case 's': return  matchwhitespace(c);
    case 'S': return !matchwhitespace(c);
    default:  return (c == str[0]);
  }
}

static int matchcharclass(char c, const char* str)
{
  do
  {
    if (matchrange(c, str))
    {
      return 1;
    }
    else if (str[0] == '\\')
    {
      /* Escape-char: increment str-ptr and match on next char */
      str += 1;
      if (matchmetachar(c, str))
      {
        return 1;
      }
      else if ((c == str[0]) && !ismetachar(c))
      {
        return 1;
      }
    }
    else if (c == str[0])
    {
      if (c == '-')
      {
        return ((str[-1] == '\0') || (str[1] == '\0'));
      }
      else
      {
        return 1;
      }
    }
  }
  while (*str++ != '\0');

  return 0;
}

static int matchone(re_t p, char c)
{
  switch (p.type)
  {
    case RE_DOT:            return matchdot(c);
    case RE_CHAR_CLASS:     return  matchcharclass(c, (const char*)p.u.ccl);
    case RE_INV_CHAR_CLASS: return !matchcharclass(c, (const char*)p.u.ccl);
    case RE_DIGIT:          return  matchdigit(c);
    case RE_NOT_DIGIT:      return !matchdigit(c);
    case RE_ALPHA:          return  matchalphanum(c);
    case RE_NOT_ALPHA:      return !matchalphanum(c);
    case RE_WHITESPACE:     return  matchwhitespace(c);
    case RE_NOT_WHITESPACE: return !matchwhitespace(c);
    default:             return  (p.u.ch == c);
  }
}

static int matchstar(re_t p, re_t* pattern, const char* text, int* matchlength)
{
  int prelen = *matchlength;
  const char* prepoint = text;
  while ((text[0] != '\0') && matchone(p, *text))
  {
    text++;
    (*matchlength)++;
  }
  while (text >= prepoint)
  {
    if (matchpattern(pattern, text--, matchlength))
      return 1;
    (*matchlength)--;
  }

  *matchlength = prelen;
  return 0;
}

static int matchplus(re_t p, re_t* pattern, const char* text, int* matchlength)
{
  const char* prepoint = text;
  while ((text[0] != '\0') && matchone(p, *text))
  {
    text++;
    (*matchlength)++;
  }
  while (text > prepoint)
  {
    if (matchpattern(pattern, text--, matchlength))
      return 1;
    (*matchlength)--;
  }

  return 0;
}

static int matchquestion(re_t p, re_t* pattern, const char* text, int* matchlength)
{
  if (p.type == RE_UNUSED)
    return 1;
  if (matchpattern(pattern, text, matchlength))
      return 1;
  if (*text && matchone(p, *text++))
  {
    if (matchpattern(pattern, text, matchlength))
    {
      (*matchlength)++;
      return 1;
    }
  }
  return 0;
}


#if 0

/* Recursive matching */
static int matchpattern(re_t* pattern, const char* text, int *matchlength)
{
  int pre = *matchlength;
  if ((pattern[0].type == RE_UNUSED) || (pattern[1].type == RE_QUESTIONMARK))
  {
    return matchquestion(pattern[1], &pattern[2], text, matchlength);
  }
  else if (pattern[1].type == RE_STAR)
  {
    return matchstar(pattern[0], &pattern[2], text, matchlength);
  }
  else if (pattern[1].type == RE_PLUS)
  {
    return matchplus(pattern[0], &pattern[2], text, matchlength);
  }
  else if ((pattern[0].type == RE_END) && pattern[1].type == RE_UNUSED)
  {
    return text[0] == '\0';
  }
  else if ((text[0] != '\0') && matchone(pattern[0], text[0]))
  {
    (*matchlength)++;
    return matchpattern(&pattern[1], text+1);
  }
  else
  {
    *matchlength = pre;
    return 0;
  }
}

#else

/* Iterative matching */
static int matchpattern(re_t* pattern, const char* text, int* matchlength)
{
  int pre = *matchlength;
  do
  {
    if ((pattern[0].type == RE_UNUSED) || (pattern[1].type == RE_QUESTIONMARK))
    {
      return matchquestion(pattern[0], &pattern[2], text, matchlength);
    }
    else if (pattern[1].type == RE_STAR)
    {
      return matchstar(pattern[0], &pattern[2], text, matchlength);
    }
    else if (pattern[1].type == RE_PLUS)
    {
      return matchplus(pattern[0], &pattern[2], text, matchlength);
    }
    else if ((pattern[0].type == RE_END) && pattern[1].type == RE_UNUSED)
    {
      return (text[0] == '\0');
    }
/*  Branching is not working properly
    else if (pattern[1].type == BRANCH)
    {
      return (matchpattern(pattern, text) || matchpattern(&pattern[2], text));
    }
*/
  (*matchlength)++;
  }
  while ((text[0] != '\0') && matchone(*pattern++, *text++));

  *matchlength = pre;
  return 0;
}

#endif


