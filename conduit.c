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
    char *method;
    char *path;
    char *version;
    char  *header_keys[256];
    char *header_values[256];
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
        struct sockaddr_in clientAddr;
        memset(&clientAddr, 0, sizeof(clientAddr));        // zero the struct, so clear out any garbage data
        
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
                read_byte_count = -1;
                break;
                
            } else {
                read_byte_count = read(clientFD,&buff[buffPtr],256); // read 256 bytes from clientFD into the buff
            }

        }
        // null-terminate the buffer
        buff[read_byte_count] = '\0';
        // now we are done reading from the client, begin manipulating the buffer

        // this http_request will store the parsed data from the raw reads that were stored in the buffer
        http_request_t http_request;
        memset(&http_request,0,sizeof(http_request));


        // parse the buffer
        char *delim = "\r\n"; // every line ends in \r\n
        char *space_delim = " "; // every item 
        char *saveptr; // strtok_r needs a saveptr
        
        // The first call to strtok_r is the only one that needs a reference to the original string (buff in this case)
        char *request_line = (char *)strtok_r(buff,delim,&saveptr);
        char *saveptr2;

        // parse this first line uniquely
        // 3 main sections, MTHD URI VER
        char *method = strtok_r(request_line,space_delim,&saveptr2);
        char *uri = strtok_r(NULL,space_delim,&saveptr2);
        char *version = strtok_r(NULL,space_delim,&saveptr2);

        int header_count =0;
        char *token = strtok_r(NULL,delim,&saveptr);
        char *token_delim = ": "; // deliminates between header key and header value

        while(token != NULL){
            char *saveptr3;

            char *header_key = strtok_r(token,token_delim,&saveptr3);
            char *header_value = strtok_r(NULL,token_delim,&saveptr3);
            http_request.header_keys[header_count]=header_key;
            http_request.header_values[header_count]=header_value;

            // consume for next iteration            
            token = strtok_r(NULL,delim,&saveptr); // subsequent calls don't need to include the original string
            header_count++;
        }



        // now perform validations on the request
        int status_code =200; // OK by default
        // 1) Check if request line malformed
        // 1a. are there missing fields
        if (method == NULL || uri == NULL || version == NULL){
            status_code = 400;
        } else {
            http_request.method=method;
            http_request.path=uri;
            http_request.version=version;
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