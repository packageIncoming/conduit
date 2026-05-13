CC= gcc
FLAGS = -g -Wall -Wextra -Werror -pedantic -std=c11


conduit:dochandler.o response.o
	$(CC) $(FLAGS) conduit.c dochandler.o  response.o -o conduit

dochandler.o:
	$(CC) $(FLAGS) -c dochandler.c  -o dochandler.o

response.o:
	$(CC) $(FLAGS) -c response.c  -o response.o

.PHONY: clean
clean:
	rm -f *.o conduit