# Watch Exec

Watch files and execute specified commands whenever they are changed.

It should work on Linux and Windows, but only Windows has been tested so far.

## Building

Use any C-compiler to compile [watch-exec.c](./watch-exec.c) and you're done.

For example, with clang:

```
clang -o watch-exec.exe src/main.c
```

## Usage

There are different ways to use this program
Each of the following variants is more powerful than the previous options
Usage variants:
  1. `watch-exec.exe <dir> <cmd>`
  2. `watch-exec.exe <dir> <match> <cmd> [<cmd>]*`
  3. `watch-exec.exe [<flag>]+`

Usage options 3 allows providing several directories/match-strings/commands

When using option 3, the following syntax variants are available for specifying options:
  1. `<flag>=<value>`
  2. `<flag> <value> [<value>]*`
Each flag is allowed to be provided several times
The ordering of options doesn't matter except for the order of commands
All commands are executed in the order that they are provided in when the specified files are changed

Option flags:
  - `-d`|`--dir`:     Directory to match files inside of
  - `-g`|`--glob`:    Glob pattern to match file-names against
  - `-r`|`--regex`:   Regular Expression to match file-names against
  - `-c`|`--cmd`:     Command to execute when a matching file was changed
  - `-h`|`--help`:    Show this help message
  - `-v`|`--version`: Show the program's version

The following syntax for regular expressions is supported:
  - `.`:         matches any character
  - `^`:         matches beginning of string
  - `$`:         matches end of string
  - `*`:         match zero or more (greedy)
  - `+`:         match one or more (greedy)
  - `?`:         match zero or one (non-greedy)
  - `[abc]`:     match if one of {'a', 'b', 'c'}
  - `[^abc]`:    match if NOT one of {'a', 'b', 'c'}
  - `[a-zA-Z]`:  match the character set of the ranges { a-z | A-Z }
  - `\s`:        Whitespace, \t \f \r \n \v and spaces
  - `\S`:        Non-whitespace
  - `\w`:        Alphanumeric, [a-zA-Z0-9_]
  - `\W`:        Non-alphanumeric
  - `\d`:        Digits, [0-9]
  - `\D`:        Non-digits

The following syntax for glob patterns is supported:
  - `*`:        match zero or more of any character
  - `?`:        match zero or one of any character
  - `[abc]`:    match if one of {'a', 'b', 'c'}
  - `[^abc]`:   match if NOT one of {'a', 'b', 'c'}
  - `[a-zA-Z]`: match the character set of the ranges { a-z | A-Z }
