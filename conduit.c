#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>

typedef struct {
    char method[8];
    char path[32];
    char version[8];
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
            printf("read %li bytes\n",read_byte_count);
            printf("%.*s\n",(int)read_byte_count,buff+buffPtr);
            
            buffPtr+= read_byte_count;
            // check if we now have the sentinel within our read buffer, if so we can end this reading
            if (strstr(buff,req_end_sentinel)){
                printf("sentinel received, ending read\n");
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
        char *saveptr; // strtok_r needs a saveptr
        
        char *token = strtok_r(buff,delim,&saveptr);
        // parse this first line uniquely
        printf("%s is the request line and should be treated differently\n",token);
        int header_count =0;
        while(token != NULL){
            //TODO Finish token consumption loop
            token = strtok_r(buff,delim,&saveptr);

        }




        

        // send the response ot the client
        const char *response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 26\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Conduit is alive \xe2\x80\x94 TRD00";

        write(clientFD, response, strlen(response));
        close(clientFD);

    }
    // close everything up:
    close(socketFD);


}