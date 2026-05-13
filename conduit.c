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

typedef struct {
    char method[8];
    char path[1024];
    char version[16];
    struct {
        char key[256];
        char value[512];
    } headers[32]; // Creates a struct 'header' that has a 'key' and a 'value'. Initializes an array of 32 of these.
    int header_count;

} http_request_t;

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

        // read from the client
        char buff[4096];
        // clear the buffer
        memset(buff,0,sizeof(buff));
        int buffPtr=0; // where we are in the buffer 
        ssize_t read_byte_count = read(clientFD,&buff[buffPtr],256); // read 256 bytes from clientFD into the buff
        char *req_end_sentinel = "\r\n\r\n"; 
        while (read_byte_count > 0){
            
            buffPtr+= read_byte_count;
            // check if we now have the sentinel within our read buffer, if so we can end this reading
            if (strstr(buff,req_end_sentinel)){
                break;
                
            } else {
                read_byte_count = read(clientFD,&buff[buffPtr],256); // read 256 bytes from clientFD into the buff
            }

        }
        // null-terminate the buffer
        buff[buffPtr] = '\0';
        // now we are done reading from the client, begin manipulating the buffer

        // this http_request will store the parsed data from the raw reads that were stored in the buffer
        http_request_t http_request;
        memset(&http_request,0,sizeof(http_request));


        // Begin parsing the buffer
        char *CRLF = "\r\n"; // every line ends in \r\n

        // 1. Parse out the request line
        char *first_crlf = strstr(buff, CRLF);

        // set to \0, parse out vals
        *first_crlf = '\0';


        int request_line_match_count  = sscanf(
            buff,
            "%7s %1023s %15s",
            http_request.method,
            http_request.path,
            http_request.version
            );

        if (request_line_match_count == 3){
            // begin parsing out the headers
            char *line_start = first_crlf+2;
            char *line_end = strstr(line_start,CRLF);
            int headerCount = 0;
            while (line_end!= NULL){
                int n = sscanf(
                    line_start, 
                    "%[^:]: %[^\r\n]", 
                    http_request.headers[headerCount].key,
                    http_request.headers[headerCount].value);
                if (n != 2){ break;}
                headerCount+=1;
                line_start = line_end+2;
                line_end = strstr(line_start,CRLF);
            }
        }



        //-------------------RESPONSE CRAFTING-------------------//

        http_response_t response_t;
        memset(&response_t,0,sizeof(response_t));



        // now perform validations on the request
        // 1) Check if request line malformed
        // 1a. are there missing fields or is it the wrong HTTP version
        if (request_line_match_count!=3 || strcmp(http_request.version,"HTTP/1.1")!=0){
            response_add_header(&response_t,"Content-Type",TEXT);
            response_add_header(&response_t,"Connection","close");

            response_set_status(&response_t,400,REASON_BAD_REQUEST);
            response_set_body(&response_t,response_t.reason_phrase,strlen(response_t.reason_phrase),0);
            response_send(clientFD,&response_t);
            response_clean(&response_t);
            close(clientFD);
            continue;
        } 

        // 2) Is it a non-GET request?
        if (strcmp(http_request.method,"GET")!=0){
            response_add_header(&response_t,"Content-Type",TEXT);
            response_add_header(&response_t,"Connection","close");

            response_set_status(&response_t,405,REASON_METHOD_NOT_ALLOWED);
            response_set_body(&response_t,response_t.reason_phrase,strlen(response_t.reason_phrase),0);
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
            response_add_header(&response_t,"Content-Type",TEXT);
            response_add_header(&response_t,"Connection","close");

            response_set_status(&response_t,403,REASON_FORBIDDEN);
            response_set_body(&response_t,response_t.reason_phrase,strlen(response_t.reason_phrase),0);
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
            response_add_header(&response_t,"Content-Type",TEXT);
            response_add_header(&response_t,"Connection","close");

            response_set_status(&response_t,500,REASON_INTERNAL_SERVER_ERROR);
            response_set_body(&response_t,response_t.reason_phrase,strlen(response_t.reason_phrase),0);
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
            response_add_header(&response_t,"Content-Type",TEXT);
            response_add_header(&response_t,"Connection","close");

            response_set_status(&response_t,500,REASON_INTERNAL_SERVER_ERROR);
            response_set_body(&response_t,response_t.reason_phrase,strlen(response_t.reason_phrase),0);
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