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
#include "dochandler.h"
#include "response.h"
#include "request.h"



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
    int socketFD = socket(AF_INET, SOCK_STREAM, 0);                         // We are creating a TCP socket on IPv4 
    if (socketFD < 0){
        perror("socket");
        exit(EXIT_FAILURE);
    }
    int optval=1;
    if (setsockopt(socketFD,SOL_SOCKET,SO_REUSEADDR,&optval,sizeof optval) <0){ // We are setting SO_REUSEADDR to 1 (TRUE) 
        perror("setsockopt");
        close(socketFD);
        exit(EXIT_FAILURE);
    }

    // now we setup the sockaddr_in struct and call bind using it
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));        // zero the struct, so clear out any garbage data
    addr.sin_family = AF_INET;             // IPv4 (X.X.X.X) format
    addr.sin_addr.s_addr = INADDR_ANY;     // all interfaces
    addr.sin_port = htons(port);           // port, byte-swapped host to network 




    if (bind(socketFD,(struct sockaddr *)&addr, sizeof(addr)) == -1){   //bind here
        perror("bind");
        close(socketFD);
        exit(EXIT_FAILURE);
    }

    // now listen on that socket and forever accept connections
    if(listen(socketFD,128) == -1){  // listen on socketFD with 128 conn backlog
        perror("listen");
        close(socketFD);
        exit(EXIT_FAILURE);
    }

    while (1){
        
        int clientFD = accept(socketFD,NULL,NULL);
        if (clientFD == -1){
            perror("accept");
            continue;
        }


        http_request_t http_request;
        memset(&http_request,0,sizeof(http_request));

        http_response_t response_t;
        memset(&response_t,0,sizeof(response_t));

        if(request_populate_from_fd(clientFD,&http_request) == 1){
            response_fill_as_error(&response_t,400,REASON_BAD_REQUEST);
            response_send(clientFD,&response_t);
            response_clean(&response_t);
            close(clientFD);
            continue;
        };


        if (strcmp(http_request.method,"GET")!=0){
            response_fill_as_error(&response_t,405,REASON_METHOD_NOT_ALLOWED);
            response_send(clientFD,&response_t);
            response_clean(&response_t);
            close(clientFD);
            continue;
        }


        // Now begin to construct GET response
        // 1. Construct raw path
        int filepath_size=1024;
        char filepath[filepath_size];
        memset(filepath,0,filepath_size);
        construct_filepath(docroot,http_request.path,filepath,filepath_size);
        // 2. Verify the raw path starts with the docroot
        if (verify_path_starts_with_docroot(docroot,filepath) == 1){
            response_fill_as_error(&response_t,403,REASON_FORBIDDEN);
            response_send(clientFD,&response_t);
            response_clean(&response_t);
            close(clientFD);
            continue;
        }

        // 3. Verify the raw path leads to an actual file
        int fileFd = open(filepath,O_RDONLY);
        if (fileFd == -1) {
            // either missing permissions or DNE
            response_add_header(&response_t,"Content-Type",TEXT);
            response_add_header(&response_t,"Connection","close");


            if (errno == EACCES){
                // 403 Forbidden
                response_set_status(&response_t,403,REASON_FORBIDDEN);
            } else {
                // 404 Not Found
                response_set_status(&response_t,404,REASON_NOT_FOUND);
            }
            response_set_body(&response_t,response_t.reason_phrase,strlen(response_t.reason_phrase),0);
            response_send(clientFD,&response_t);
            response_clean(&response_t);
            close(clientFD);
            continue;
        }

        // 4. Get the size of the file (if fail then return 500 ISE)
        int filesize = get_file_size_from_fd(fileFd);
        if (filesize == -1){
            response_fill_as_error(&response_t,500,REASON_INTERNAL_SERVER_ERROR);

            response_send(clientFD,&response_t);
            response_clean(&response_t);
            close(clientFD);
            continue;
        }
        
        
        // 5. Figure out MIME type
        const char* mimetype = get_mime_from_filepath(filepath);
        // create the buffer to hold file contents
        char file_contents_buffer[filesize];
        // 6. Read to buffer (if fail then return 500 ISE)
        if (read_file_contents_to_buffer(fileFd,file_contents_buffer,filesize) == 1){
            response_fill_as_error(&response_t,500,REASON_INTERNAL_SERVER_ERROR);
            response_send(clientFD,&response_t);
            response_clean(&response_t);
            close(clientFD);
            continue;
        }


        // Construct the final, successful response
        response_add_header(&response_t,"Content-Type",mimetype);
        response_add_header(&response_t,"Connection","close");
        response_set_status(&response_t,200,REASON_OK);
        response_set_body(&response_t,file_contents_buffer,filesize,0);
        response_send(clientFD,&response_t);
        response_clean(&response_t);
        close(clientFD);


    }
    // close everything up:
    close(socketFD);


}