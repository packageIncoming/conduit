#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <inttypes.h>
#include <signal.h>

//  Conduit-specific includes
#include "epoll_handler.h"
#include "args.h"

volatile sig_atomic_t ACTIVE=1;


void* thread_init(void* _args)
{
    conduit_args* args = (conduit_args*)_args;
    printf("started thread\n");
    printf("thread configuration:\n\tport=%d\n\tdocroot='%s'\n\tmax connections=%d\n",
            args->port,
            args->docroot,
            args->conns_per_thread);

    int port = args->port;
    const char* docroot = args->docroot;

    //  Make sure the given docroot path resolves
    char* docroot_absolute_path = realpath(args->docroot,NULL);
    if (docroot_absolute_path == NULL){
        perror("The given docroot does not resolve properly");
        return (void*)(NULL); // docroot itself does not resolve; refuse rather than deref NULL
    }


    // Part 0: making and priming the socket
    int listenFD = socket(AF_INET, SOCK_STREAM, 0);                         // We are creating a TCP socket on IPv4 
    if (listenFD < 0){
        perror("socket");
        exit(EXIT_FAILURE);
    }
    int optval=1;
    if (setsockopt(listenFD,SOL_SOCKET,SO_REUSEPORT,&optval,sizeof optval) <0){ 
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
    if(listen(listenFD,SOMAXCONN) == -1){  // listen on listenFD with MAX_CONNECTIONS conn backlog
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

    // Initialze threadpool
    // Initialize (reading) connections linked list
    conn_list_t* conn_list = conn_list_init();

    thread_state state;
    state.active_connections = 0;
    state.max_connections = args->conns_per_thread;




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
        conn_list_sweep(conn_list,TIMEOUT_SECONDS,&state);

        for(int i=0;i<n;i++){
            if (events[i].data.fd == listenFD){
                // We are receiving new connection(s)
                add_new_connections(epollFD,listenFD,&state,conn_list);
            } else {
                connection_t* conn = events[i].data.ptr;

                // Socket is dead, nothing to read or write. Check this first
                // since EPOLLERR/EPOLLHUP can arrive alongside EPOLLIN/EPOLLOUT
                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    // Remove from 'reading connections'
                    conn_list_remove_by_connection(conn_list,conn);
                    conn->status = CONN_DONE;
                    epoll_ctl(epollFD,EPOLL_CTL_DEL,conn->fd,NULL);
                    close(conn->fd);
                    connection_free(conn,&state);
                    continue;
                }

                // Finishing a write that didn't complete in one go
                if (events[i].events & EPOLLOUT) {
                    int w = connection_on_epollout(conn->fd,conn);
                    if (w == 1){
                        // done writing, so close up this socket
                        conn_list_remove_by_connection(conn_list,conn);
                        conn->status = CONN_DONE;
                        epoll_ctl(epollFD,EPOLL_CTL_DEL,conn->fd,NULL);
                        close(conn->fd);
                        connection_free(conn, &state);
                    } else if (w < 0){
                        // Write failed outright
                        conn_list_remove_by_connection(conn_list,conn);
                        conn->status = CONN_DONE;
                        epoll_ctl(epollFD,EPOLL_CTL_DEL,conn->fd,NULL);
                        close(conn->fd);
                        connection_free(conn,&state);
                    }
                    // w == 0: still partial, stays armed for EPOLLOUT
                    continue;
                }

                // We are handling some existing connection
                if (events[i].events & EPOLLIN ) {
                    // Reading from client
                    int result = connection_on_epollin(conn->fd,conn,docroot, docroot_absolute_path);
                    if(result ==1){

                        // Reading done, write the response out.
                        // Stay in the LL and in epoll until the write actually
                        // finishes, otherwise a partial write orphans the conn
                        conn->status=CONN_WRITING;
                        if (connection_on_epollout(conn->fd,conn) == 1){
                            //  done writing too, so close up this socket
                            conn_list_remove_by_connection(conn_list,conn);
                            conn->status = CONN_DONE;
                            if (epoll_ctl(epollFD,EPOLL_CTL_DEL,conn->fd,NULL) == -1){
                                perror("epoll_ctl: request parse done");
                            }
                            close(conn->fd);
                            connection_free(conn, &state);
                        } else {
                            //  We didn't finish writing, so switch this fd over
                            //  to watching for writability and finish later
                            struct epoll_event ev_out;
                            ev_out.data.ptr = conn;
                            ev_out.events = EPOLLOUT | EPOLLET;
                            if (epoll_ctl(epollFD, EPOLL_CTL_MOD, conn->fd, &ev_out) == -1){
                                perror("epoll_ctl: rearm for EPOLLOUT");
                            }
                        }

                    } else if (result == -1){
                        // Error occurred
                        // Remove from 'reading connections' LL
                        conn_list_remove_by_connection(conn_list,conn);
                        conn->status = CONN_DONE;
                        epoll_ctl(epollFD,EPOLL_CTL_DEL,conn->fd,NULL);
                        close(conn->fd);
                        connection_free(conn,&state);
                    }
                }
            }
        }
    }
    // close everything up:
    close(listenFD);
    close(epollFD);
    epollFD = -1;
    conn_list_destroy(conn_list,&state);
    conn_list=NULL;
    free(docroot_absolute_path);
    docroot_absolute_path=NULL;

    return (void*)NULL;
}


int main(int argc, char *argv[]){
    setvbuf(stdout, NULL, _IOLBF, 0);
    conduit_args args;
    parse_argv(argc, argv, &args);

    sigset_t set;
    int sig;

    // Block SIGINT and SIGTERM so the default actions don't run
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);


    //  create #[args.thread_count] threads
    pthread_t* threads = calloc(args.thread_count,sizeof(pthread_t));
    
    for (int i=0; i < args.thread_count; i++)
    {
        pthread_create(&threads[i],NULL,thread_init,(void*)&args);
    }


    // This blocks synchronously until a signal arrives
    sigwait(&set, &sig); 

    printf("\nSignal %d caught synchronously. Cleaning up threads...\n", sig);
    ACTIVE = 0;
    
    // Now join the threads
    for (int i = 0; i < args.thread_count; i++) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
    exit(0);


}