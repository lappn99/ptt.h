CC=gcc
CCFLAGS=-Wall -Werror -fsanitize=address,undefined

example: example.c ../ptt.h
	$(CC) $(CCFLAGS) $^ -o $@
