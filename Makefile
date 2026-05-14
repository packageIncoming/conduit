CC= gcc
FLAGS = -g -Wall -Wextra -Werror -pedantic -std=c11


conduit:dochandler.o response.o request.o epoll_handler.o
	$(CC) $(FLAGS) conduit.c dochandler.o  response.o request.o epoll_handler.o -o conduit

dochandler.o:
	$(CC) $(FLAGS) -c dochandler.c  -o dochandler.o

response.o:
	$(CC) $(FLAGS) -c response.c  -o response.o


request.o:
	$(CC) $(FLAGS) -c request.c  -o request.o

epoll_handler.o:
	$(CC) $(FLAGS) -c epoll_handler.c  -o epoll_handler.o

.PHONY: clean
clean:
	rm -f *.o conduit