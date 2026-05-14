#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/epoll.h>
#include <errno.h>

#include "epoll_handler.h"
#include "request.c"
#include "response.c"

void setnonblocking(int fd){
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}


void connection_free(connection_t* connection){
    free(connection->read_buffer);
    free(connection->write_buffer);
    free(connection->request);
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
        connection_t* conn = (connection_t*)malloc(sizeof(connection_t));
        memset(conn,0,sizeof(*conn));
        conn->fd=clientFD;
        conn->status=CONN_READING;
        conn->wb_offset=0;
        http_request_t* request = (http_request_t*)malloc(sizeof(http_request_t));
        http_response_t* response = (http_response_t*)malloc(sizeof(http_response_t));
        conn->request=request;
        conn->response=response;
        response->owns_body_flag=0;
        // construct epoll_event object to pass to epoll_ctl
        struct epoll_event ev;
        ev.data.ptr = conn;
        ev.data.fd=clientFD;
        ev.events = EPOLLIN | EPOLLET;
        // now try adding
        epoll_ctl(epollFD,EPOLL_CTL_ADD,clientFD,&ev); // Discarding error for now
    }    
}


// --------------------- EPOLLIN-BASED METHODS --------------------- //

int _connection_read_to_buffer(int fd, connection_t* conn){
    // read from the client
    while (1){
        ssize_t read_byte_count = read(fd,conn->read_buffer[conn->rb_offset],256); // read 256 bytes from clientFD into the buff
        if (read_byte_count == -1){
            break;
        }
        conn->rb_offset+= read_byte_count;
    }

    // null-terminate the buffer
    conn->read_buffer[conn->rb_offset] = '\0';
    char *first_crlf = strstr(conn->read_buffer, CRLF);
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
    return 0; // 
}



int connection_on_epollin(int fd,connection_t* conn,const char* docroot){
    if (conn->status!= CONN_READING) {
        fprintf(stderr,"WARNING: TRYING TO READ FROM CONNECTION WITH STATUS != CONN_READING\n");
        return -1;
    }
    if(_connection_read_to_buffer(fd,conn) == 0){
        // did not find \r\n\r\n
        return 0;
    } else{
        // did find \r\n\r\n implies reading is done
        // create http_request_t struct
        if(_connection_populate_request(conn) == 1){
            // malformed, create the response as an error response
            response_fill_as_error(conn->response,400,REASON_BAD_REQUEST);
        } else {
            // maybe not malformed? Go through checks 
            if (strcmp(conn->request->method,"GET")!=0){
                // Non-GET -> 405
                response_fill_as_error(conn->response,405,REASON_METHOD_NOT_ALLOWED);
            }
        }
        // Request is structurally valid and a GET, begin making response
        // first construct the actual http_response_t object
        _connection_construct_response(conn,docroot);
        // then serialize it into the write_buffer body
        _connection_serialize_response(conn);
        return 1;
    }
}

// --------------------- EPOLLOUT-BASED METHODS --------------------- //

void _connection_read_file_to_write_buffer(connection_t* conn){

}

void _connection_construct_response(connection_t* conn, const char* docroot){
    // Now begin to construct GET response
    // 1. Construct raw path
    int filepath_size=1024;
    char filepath[filepath_size];
    memset(filepath,0,filepath_size);
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
        response_fill_as_error(conn->response,500,REASON_INTERNAL_SERVER_ERROR);
        return;
    }

    // 5. Figure out MIME type
    const char* mimetype = get_mime_from_filepath(filepath);

}

void _connection_serialize_response(connection_t* conn){

}

int _connection_write_to_client(int fd, connection_t* conn){

    return 0;
}

int connection_on_epollout(int fd,connection_t* conn){
    return 0;
}