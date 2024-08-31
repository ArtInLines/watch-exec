watch-exec: watch-exec.c
	clang -o watch-exec.exe watch-exec.c -Wall -Wextra -Wpedantic -Wno-unused-function -Werror -g