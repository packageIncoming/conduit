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

#include "dochandler.h"
#include "response.h"
#include "request.h"
#include "epoll_handler.h"



int main(int argc, char *argv[]){
    if (argc <3){
        fprintf(stderr,"Usage: ./conduit <port number> <docroot>\n");
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    printf("Using port %i\n",port);

    char *docroot = argv[2];
    printf("Docroot at %s\n",docroot);

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
    if(listen(listenFD,128) == -1){  // listen on listenFD with 128 conn backlog
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


    while (1){

        // Get current number of events
        int n = epoll_wait(epollFD,events,MAXEVENTS,-1);

        for(int i=0;i<n;i++){
            if (events[i].data.fd == listenFD){
                // We are receiving new connection(s)
                add_new_connections(epollFD,listenFD);
            } else {
                connection_t* conn = (connection_t *)events[i].data.ptr;
                // We are handling some existing connection
                if (events[i].events == EPOLLIN | EPOLLET) {
                    // Reading from client
                    if(connection_on_epollin(events[i].data.fd,events[i].data.ptr,docroot) ==1){
                        // Reading done, change to CONN_WRITING mode and change to EPOLLOUT
                        conn->status=CONN_WRITING;
                        // Modify ev to have the values we want to update using
                        ev.data.fd = conn->fd;
                        ev.events = EPOLLOUT | EPOLLET;
                        if(epoll_ctl(epollFD,EPOLL_CTL_MOD,conn->fd,&ev)==-1){
                            perror("epoll_ctl: update CONN_READING->CONN_WRITING error");
                        }
                    } 
                } else if (events[i].events == EPOLLOUT | EPOLLET) {
                    // Writing to client
                    connection_on_epollout(events[i].data.fd,events[i].data.ptr);
                } else {
                    // Either EPOLLERR or EPOLLHUP so just kill the connection and free the associated connection struct
                    connection_free(events[i].data.ptr);
                }

            }
        }
        






        
        

        // // create the buffer to hold file contents
        // char file_contents_buffer[filesize];
        // // 6. Read to buffer (if fail then return 500 ISE)
        // if (read_file_contents_to_buffer(fileFd,file_contents_buffer,filesize) == 1){
        //     response_fill_as_error(&response_t,500,REASON_INTERNAL_SERVER_ERROR);
        //     response_send(clientFD,&response_t);
        //     response_clean(&response_t);
        //     close(clientFD);
        //     continue;
        // }


        // // Construct the final, successful response
        // response_add_header(&response_t,"Content-Type",mimetype);
        // response_add_header(&response_t,"Connection","close");
        // response_set_status(&response_t,200,REASON_OK);
        // response_set_body(&response_t,file_contents_buffer,filesize,0);
        // response_send(clientFD,&response_t);
        // response_clean(&response_t);
        // close(clientFD);


    }
    // close everything up:
    close(listenFD);


}