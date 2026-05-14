#include "request.h"
#include <sys/types.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

const char* const CRLF= "\r\n"; // every line ends in \r\n

int request_populate_from_fd(int clientFD, http_request_t* http_request){
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

        // 1. Parse out the request line
        char *first_crlf = strstr(buff, CRLF);

        // set to \0, parse out vals
        *first_crlf = '\0';


        int request_line_match_count  = sscanf(
            buff,
            "%7s %1023s %15s",
            http_request->method,
            http_request->path,
            http_request->version
            );

        if (request_line_match_count != 3){
            return 1; // Invalid/malformed request line
        }

         // begin parsing out the headers
        char *line_start = first_crlf+2;
        char *line_end = strstr(line_start,CRLF);
        int headerCount = 0;
        while (line_end!= NULL){
            int n = sscanf(
                line_start, 
                "%[^:]: %[^\r\n]", 
                http_request->headers[headerCount].key,
                http_request->headers[headerCount].value);
            if (n != 2){ break;}
            headerCount+=1;
            line_start = line_end+2;
            line_end = strstr(line_start,CRLF);
        }
        if ( strcmp(http_request->version,"HTTP/1.1")!=0){
            return 1; // Invalid HTTP version
        } 

        return 0;
}