set -xe

clang -o watch-exec src/main.c -framework CoreFoundation -framework CoreServices -Wall -Wextra -Wpedantic -Wno-unused-function -Werror -g