CC = gcc
FLAGS = -g -Wall -Wextra -Werror -pedantic -std=c11 -pthread
DEPFLAGS = -MMD -MP

OBJS = conduit.o dochandler.o response.o request.o epoll_handler.o threadpool.o

conduit: $(OBJS)
	$(CC) $(FLAGS) $(OBJS) -o conduit

%.o: %.c
	$(CC) $(FLAGS) $(DEPFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)

.PHONY: clean
clean:
	rm -f *.o *.d conduit