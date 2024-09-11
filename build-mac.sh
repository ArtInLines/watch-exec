set -xe

clang -o watch-exec src/main.c -framework CoreFoundation -framework CoreServices -lpthread -Wall -Wextra -Wpedantic -Wno-unused-function -Werror -g