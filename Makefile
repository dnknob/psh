CFLAGS = -Wall -include stdio.h

compile:
	gcc $(CFLAGS) -o psh main.c builtins.c helpers.c -lreadline
