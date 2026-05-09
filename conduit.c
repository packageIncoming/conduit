#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>

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
    if (argc <2){
        fprintf(stderr,"Usage: ./conduit <port number>\n");
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    printf("Using port %i\n",port);

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
            // do stuff
            //printf("read %li bytes\n",read_byte_count);
            //printf("%.*s\n",(int)read_byte_count,buff+buffPtr);
            
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
        //char *space_CRLF = " "; // every item 
        //char *saveptr; // strtok_r needs a saveptr

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

        // now perform validations on the request
        int status_code =200; // OK by default
        // 1) Check if request line malformed
        // 1a. are there missing fields
        if (request_line_match_count!=3){
            status_code = 400;
        } 
        // 1b. is it the wrong version
        if (status_code == 200 && strcmp(http_request.version,"HTTP/1.1")!=0){
            status_code=400;
        }

        // 2) Is it a non-GET request?
        if (status_code == 200 && strcmp(http_request.method,"GET")!=0){
            status_code = 405;
        }

        // 3) Is it routing to ANYTHING OTHER THAN '/'?
        if (status_code == 200 && strcmp(http_request.path,"/")!=0){
            status_code = 404;
        }





        

        // send the response to the client
        // figure out the correct response based on status code
        const char* response;
        if (status_code == 200){
            response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 26\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Conduit is alive \xe2\x80\x94 TRD00";
        } else if(status_code == 404) {
            response = 
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 26\r\n"
                "\r\n"
                "Conduit is alive \xe2\x80\x94 TRD00";
        } else if (status_code == 400) {
            response = 
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 26\r\n"
                "\r\n"
                "Conduit is alive \xe2\x80\x94 TRD00";
        } else if (status_code == 405){
            response = 
                "HTTP/1.1 405 Method Not Allowed\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 26\r\n"
                "\r\n"
                "Conduit is alive \xe2\x80\x94 TRD00";
        }


        write(clientFD, response, strlen(response));
        close(clientFD);

    }
    // close everything up:
    close(socketFD);


}