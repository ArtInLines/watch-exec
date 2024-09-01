watch-exec: src/*.c
	clang -o watch-exec.exe src/main.c -Wall -Wextra -Wpedantic -Wno-unused-function -Werror -g