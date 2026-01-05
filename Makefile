CFLAGS = -Wall -include stdio.h

compile:
	gcc $(CFLAGS) -o main main.c builtins.c helpers.c -lreadline
