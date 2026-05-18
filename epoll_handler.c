#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>

#include "epoll_handler.h"
#include "dochandler.h"
#include "request.h"
#include "response.h"

void setnonblocking(int fd){
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}


void connection_free(connection_t* connection){
    free(connection->write_buffer);
    free(connection->request);
    response_clean(connection->response);
    free(connection->response);
    free(connection);
}

void add_new_connections(int epollFD, int listenFD){
    while (1){
        int clientFD = accept(listenFD,NULL,NULL);
        if (clientFD == -1){
            break; // no more connections AND/OR blocking
        }
        setnonblocking(clientFD);
        // construct connection_t object
        connection_t* conn = malloc(sizeof(connection_t));
        memset(conn,0,sizeof(*conn));
        conn->fd=clientFD;
        conn->status=CONN_READING;
        conn->wb_offset=0;
        http_request_t* request = malloc(sizeof(http_request_t));
        http_response_t* response = malloc(sizeof(http_response_t));
        memset(request,0,sizeof(*request));
        memset(response,0,sizeof(*response));
        conn->request=request;
        conn->response=response;
        response->owns_body_flag=0;
        // construct epoll_event object to pass to epoll_ctl
        struct epoll_event ev;
        ev.data.ptr = conn;
        ev.events =( EPOLLIN | EPOLLET);
        // now try adding
        if(epoll_ctl(epollFD,EPOLL_CTL_ADD,clientFD,&ev) == -1){
            perror("epoll_ctl, add_new_connections");
            close(clientFD);
            connection_free(conn);
        } 
    }    
}


// --------------------- EPOLLIN-BASED METHODS --------------------- //

int _connection_read_to_buffer(int fd, connection_t* conn){
    // read from the client
    while (1){
        size_t remaining = READBUFFER_SIZE - conn->rb_offset - 1;
        if (remaining == 0){
            return -1;
        }
        ssize_t read_byte_count = read(fd,&(conn->read_buffer[conn->rb_offset]),remaining); // read 256 bytes from clientFD into the buff
        if (read_byte_count == -1){
            if( errno == EAGAIN){ break;}
            return -1; // Unexpected error
        } else if (read_byte_count == 0){
            break;
        }
        conn->rb_offset+= read_byte_count;
    }
    // null-terminate the buffer

    conn->read_buffer[conn->rb_offset] = '\0';
    if (conn->rb_offset == READBUFFER_SIZE-1 ){
        return -1; // Too large of a request
    }
    char *first_crlf = strstr(conn->read_buffer, "\r\n\r\n");
    

    if (first_crlf == NULL){
        // We have not yet finished reading, return 0
        return 0;
    } else {
        // found CRLF
        return 1;
    }
}

int _connection_populate_request(connection_t* conn){
    // set to \0, parse out vals
    char *first_crlf = strstr(conn->read_buffer, CRLF);
    *first_crlf = '\0';
    int request_line_match_count  = sscanf(
        conn->read_buffer,
        "%7s %1023s %15s",
        conn->request->method,
        conn->request->path,
        conn->request->version
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
            conn->request->headers[headerCount].key,
            conn->request->headers[headerCount].value);
        if (n != 2){ break;}
        headerCount+=1;
        line_start = line_end+2;
        line_end = strstr(line_start,CRLF);
    }
    return 0; 
}



int connection_on_epollin(int fd,connection_t* conn,const char* docroot){

    if (conn->status!= CONN_READING) {
        fprintf(stderr,"WARNING: TRYING TO READ FROM CONNECTION WITH STATUS != CONN_READING\n");
        return -1;
    }
    int result = _connection_read_to_buffer(fd,conn);
    if(result== 0){
        // did not find \r\n\r\n
        return 0;
    } else if (result == -1){
        if (conn->rb_offset >= READBUFFER_SIZE-1){
            // Too big of a request, construct response and send
            response_fill_as_error(conn->response,400,REASON_BAD_REQUEST);
            response_set_body(conn->response,"Request too large",17,0);
            _connection_serialize_response(conn);
        } else {
            // Unexpected Error
            response_fill_as_error(conn->response,500,REASON_INTERNAL_SERVER_ERROR);
        }
        return 1; 
    } else{
        // did find \r\n\r\n implies reading is done
        // create http_request_t struct
        if(_connection_populate_request(conn) == 1){
            // malformed, create the response as an error response
            response_fill_as_error(conn->response,400,REASON_BAD_REQUEST);
        } else {
            // maybe not malformed? Go through checks 
            if (strcmp(conn->request->method,"GET")!=0 ){
                // Non-GET -> 405
                response_fill_as_error(conn->response,405,REASON_METHOD_NOT_ALLOWED);
            } else if ( strcmp(conn->request->version,"HTTP/1.1")!=0){
                response_fill_as_error(conn->response,400,REASON_BAD_REQUEST);
            }else {
                // Request is structurally valid and a GET, begin making response
                // first construct the actual http_response_t object
                _connection_construct_response(conn,docroot);
            }
        }
        // then serialize it into the write_buffer body
        _connection_serialize_response(conn);
        return 1;
    }
}

// --------------------- EPOLLOUT-BASED METHODS --------------------- //


void _connection_construct_response(connection_t* conn, const char* docroot){
    // Now begin to construct GET response
    // 1. Construct raw path
    memset(conn->response,0,sizeof(*conn->response));
    int filepath_size=1024;
    char filepath[filepath_size];
    memset(&filepath,0,filepath_size);
    construct_filepath(docroot,conn->request->path,filepath,filepath_size);
    
    // 2. Verify the raw path starts with the docroot
    if (verify_path_starts_with_docroot(docroot,filepath) == 1){
        response_fill_as_error(conn->response,403,REASON_FORBIDDEN);
        return;
    }

    // 3. Verify the raw path leads to an actual file
    int fileFd = open(filepath,O_RDONLY);
    if (fileFd == -1) {
        if (errno == EACCES){
            // 403 Forbidden
            response_fill_as_error(conn->response,403,REASON_FORBIDDEN);
        } else {
            // 404 Not Found
           response_fill_as_error(conn->response,404,REASON_NOT_FOUND);
        }
        return;
    }

    // 4. Get the size of the file (if fail then return 500 ISE)
    int filesize = get_file_size_from_fd(fileFd);
    if (filesize == -1){
        close(fileFd);
        response_fill_as_error(conn->response,500,REASON_INTERNAL_SERVER_ERROR);
        return;
    }

    // 5. Figure out MIME type
    const char* mimetype = get_mime_from_filepath(filepath);

    // 6. Fill out response body
    // NOTE: BODY != SERIALIZED RESPONSE. BODY HOLDS CONTENTS OF THE FILE;
    // BODY MUST BE COMBINED WITH HEADERS AND SERIALIZED INTO THE 
    // CONNECTION'S WRITE BUFFER OUTSIDE OF THIS METHOD
    // create the buffer to hold file contents
    char* file_contents_buffer= (char*)malloc(filesize+1);

    // 6. Read to buffer (if fail then return 500 ISE)
    if (read_file_contents_to_buffer(fileFd,file_contents_buffer,filesize) == 1){
        response_fill_as_error(conn->response,500,REASON_INTERNAL_SERVER_ERROR);
        free(file_contents_buffer); // Unused
        close(fileFd);
        file_contents_buffer=NULL;
        return;
    }



    // Construct the final, successful response
    response_add_header(conn->response,"Content-Type",mimetype);
    response_add_header(conn->response,"Connection","close");
    response_set_status(conn->response,200,REASON_OK);
    // Set ownership so that file_contents_buffer gets free'd properly.
    response_set_body(conn->response,file_contents_buffer,filesize,1);
    close(fileFd);
}

void _connection_serialize_response(connection_t* conn){
    // calculate response size
    http_response_t* resp = conn->response;
    // Represents the MAXIMUM SIZE if ALL HEADERS (KEY+VALUE) COMPLETELY FILLED
    int response_size = resp->body_size + strlen(resp->reason_phrase) + HEADER_LINE_BYTES*resp->header_count + 18;
    char* final_response = (char *)malloc(response_size);
    conn->write_buffer=final_response; // Set ownership
    conn->wb_offset=0;
    int written_bytes=0;


    // create first line
    written_bytes+= sprintf(final_response,"HTTP/1.1 %i %s\r\n",resp->status_code,resp->reason_phrase);

    // add headers
    for(int i = 0; i < resp->header_count;i++){
        written_bytes+= sprintf(
            final_response+written_bytes,
            "%s: %s\r\n",
            resp->headers[i].key,
            resp->headers[i].value
        );
    }

    // add Content-Length header (generated based on body)
    written_bytes+= sprintf(
        final_response+written_bytes,
        "Content-Length: %li\r\n",
        resp->body_size
    );

    // add \r\n before body
    written_bytes+= sprintf(final_response+written_bytes,"\r\n");

    // add body
    written_bytes+=snprintf(final_response+written_bytes,resp->body_size+1,"%s",resp->body);
    // add nullbyte
    final_response[written_bytes]='\0';
    conn->wb_size=written_bytes;
}

int _connection_write_to_client(int fd, connection_t* conn){
    // read from the client
    while (1){
        if(conn->wb_offset >= (int)conn->wb_size){
            // Finished writing
            return 1;
        }
        ssize_t write_byte_count = write(fd,&(conn->write_buffer[conn->wb_offset]),conn->wb_size-conn->wb_offset); // read 256 bytes from clientFD into the buff
        if (write_byte_count == -1){
            if (errno == EAGAIN) {
                return 0;
            } else {
                return -1; // Unexpected Error
            }
        }
        conn->wb_offset+= write_byte_count;
    }
    return 0;
}

int connection_on_epollout(int fd,connection_t* conn){
    if (conn->status!= CONN_WRITING) {
        fprintf(stderr,"WARNING: TRYING TO WRITE TO CONNECTION WITH STATUS != CONN_WRITING\n");
        return -1;
    }

    return _connection_write_to_client(fd,conn);
}