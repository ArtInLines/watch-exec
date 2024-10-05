set -xe

clang -o watch-exec src/main.c -Wall -Wextra -Wpedantic -Wno-unused-function -Werror -g