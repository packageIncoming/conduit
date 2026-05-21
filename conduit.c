#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <inttypes.h>
#include <signal.h>
#include <time.h>

#include "dochandler.h"
#include "response.h"
#include "request.h"
#include "epoll_handler.h"
#include "threadpool.h"

volatile sig_atomic_t ACTIVE=1;

void graceful_exit(){
    ACTIVE=0;
}


int main(int argc, char *argv[]){
    if (argc <3){
        fprintf(stderr,"Usage: ./conduit <port number> <docroot>\n");
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    printf("Using port %i\n",port);

    char *docroot = argv[2];
    printf("Docroot at %s\n",docroot);

    signal(SIGPIPE, SIG_IGN); // Prevent server crash on write to closed socket

    struct sigaction sa;

    // Clear the structure and set the handler function
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &graceful_exit;

    // Register SIGTERM/SIGINT to our handler
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        perror("Error binding SIGTERM handler");
        return 1;
    }
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        perror("Error binding SIGINT handler");
        return 1;
    }

    // Part 0: making and priming the socket
    int listenFD = socket(AF_INET, SOCK_STREAM, 0);                         // We are creating a TCP socket on IPv4 
    if (listenFD < 0){
        perror("socket");
        exit(EXIT_FAILURE);
    }
    int optval=1;
    if (setsockopt(listenFD,SOL_SOCKET,SO_REUSEADDR,&optval,sizeof optval) <0){ // We are setting SO_REUSEADDR to 1 (TRUE) 
        perror("setsockopt");
        close(listenFD);
        exit(EXIT_FAILURE);
    }

    // now we setup the sockaddr_in struct and call bind using it
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));        // zero the struct, so clear out any garbage data
    addr.sin_family = AF_INET;             // IPv4 (X.X.X.X) format
    addr.sin_addr.s_addr = INADDR_ANY;     // all interfaces
    addr.sin_port = htons(port);           // port, byte-swapped host to network 




    if (bind(listenFD,(struct sockaddr *)&addr, sizeof(addr)) == -1){   //bind here
        perror("bind");
        close(listenFD);
        exit(EXIT_FAILURE);
    }

    // now listen on that socket and forever accept connections
    if(listen(listenFD,MAX_CONNECTIONS) == -1){  // listen on listenFD with MAX_CONNECTIONS conn backlog
        perror("listen");
        close(listenFD);
        exit(EXIT_FAILURE);
    }

    setnonblocking(listenFD);

    // Create epoll file descriptor
    int epollFD = epoll_create1(0);
    if (epollFD == -1){
        perror("epoll_create1");
        close(listenFD);
        exit(EXIT_FAILURE);
    }
    struct epoll_event ev, events[MAXEVENTS]; // When we call epoll_wait, events[] gets populated with epoll_event ptrs wherein events[i] is the epoll_event data for the ith FD 

    // Fill ev with contents to add listenFD as the first FD in epollFD's list
    ev.data.fd=listenFD;
    ev.events = EPOLLIN | EPOLLET; // For server socket, we want to know when there is input (pending connection) and we are going for ET
    // Add ev to epollFD's interest list, if we can't then exit (can't get clients w/o it)
    if (epoll_ctl(epollFD,EPOLL_CTL_ADD,listenFD,&ev) == -1) {
        perror("epoll_ctl: socket");
        close(listenFD);
        exit(EXIT_FAILURE);
    }
    perror("epoll_ctl listenFD success");


    // Initialze threadpool
    threadpool_t* threadpool = threadpool_init(NUM_THREADS);
    // Initialize (reading) connections linked list
    conn_list_t* conn_list = conn_list_init(threadpool);


    // Main loop

    while (ACTIVE){

        // Get current number of events
        int n = epoll_wait(epollFD,events,MAXEVENTS,1000);


        if (n <0){
            if (errno == EINTR){
                break;
            }
            else {
                perror("epoll_wait");
                break;
            }
        }

        // Sweep the reading connections to get rid of timed-out connections
        conn_list_sweep(conn_list,TIMEOUT_SECONDS);

        for(int i=0;i<n;i++){
            if (events[i].data.fd == listenFD){
                // We are receiving new connection(s)
                add_new_connections(epollFD,listenFD,threadpool,conn_list);
            } else {
                connection_t* conn = events[i].data.ptr;

                // We are handling some existing connection
                if (events[i].events & EPOLLIN ) {
                    // Reading from client
                    int result = connection_on_epollin(conn->fd,conn,docroot);
                    if(result ==1){

                        // Reading done, create task_t struct and enqueue
                        // Also remove it from the 'reading connections' LL 
                        conn_list_remove_by_connection(conn_list,conn);
                        task_t* task = calloc(1,sizeof(task_t));
                        task->connection = conn;
                        conn->status=CONN_WRITING;
                        pthread_mutex_lock(&threadpool->mutex);
                        threadpool_enqueue(threadpool,task);
                        pthread_mutex_unlock(&threadpool->mutex);

                        // Remove the fd from the epoll since now the worker owns that connection
                        if (epoll_ctl(epollFD,EPOLL_CTL_DEL,conn->fd,NULL) == -1){
                            perror("epoll_ctl: request parse done");
                        }

                    } else if (result == -1){
                        // Error occurred
                        // Remove from 'reading connections' LL 
                        conn_list_remove_by_connection(conn_list,conn);
                        conn->status = CONN_DONE;
                        ev.data.ptr = conn;
                        epoll_ctl(epollFD,EPOLL_CTL_DEL,conn->fd,NULL);
                        close(conn->fd);
                        connection_free(conn,threadpool);
                        
                    }
                } else {
                    // Either EPOLLERR or EPOLLHUP so just kill the connection and free the associated connection struct
                    // Remove from 'reading connections'
                    conn_list_remove_by_connection(conn_list,conn);
                    conn->status = CONN_DONE;
                    ev.data.ptr = conn;
                    epoll_ctl(epollFD,EPOLL_CTL_DEL,conn->fd,NULL);
                    close(conn->fd);
                    connection_free(conn,threadpool); 
                }
            }
        }
    }
    // close everything up:
    printf("%i active at end\n",threadpool->active_connections);
    close(listenFD);
    close(epollFD);
    epollFD = -1;
    conn_list_destroy(conn_list);
    conn_list=NULL;
    threadpool_destroy(threadpool);
    threadpool=NULL;    

    exit(0);


}