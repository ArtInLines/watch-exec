set -xe

clang -o watch-exec src/main.c -lpthread -Wall -Wextra -Wpedantic -Wno-unused-function -Werror -g