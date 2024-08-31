watch-exec: watch-exec.c
	clang -o watch-exec.exe watch-exec.c deps\tiny-regex-c\re.c