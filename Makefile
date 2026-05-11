CC= gcc
FLAGS = -g -Wall -Wextra -Werror -pedantic -std=c11


conduit:dochandler.o
	$(CC) $(FLAGS) conduit.c dochandler.o -o conduit

dochandler.o:
	$(CC) $(FLAGS) -c dochandler.c  -o dochandler.o

.PHONY: clean
clean:
	rm -f *.o conduit