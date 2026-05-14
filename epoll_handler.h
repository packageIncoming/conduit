#ifndef EPOLL_HANDLER
#define EPOLL_HANDLER
#define MAXEVENTS 100

#include "request.h"
#include "response.h"

enum CONN_STATUS {CONN_READING, CONN_WRITING, CONN_DONE};

typedef struct {
    int fd;
    enum CONN_STATUS status ;
    char* write_buffer;
    char read_buffer[8192];
    size_t wb_size;
    int wb_offset;
    int rb_offset;

    http_request_t* request;
    http_response_t* response;

} connection_t;


// Sets a file descriptor to be nonblocking using fcntl(fd, F_SETFL, flags | O_NONBLOCK)
void setnonblocking(int fd);

// Frees the write and read buffers that were malloc'd
void connection_free(connection_t* connection);

// Adds new connections to the associated epoll instance until accept() returns -1 (EAGAIN)
// Adds with flags EPOLLIN | EPOLLET
void add_new_connections(int epollFD, int listenFD);


// --------------------- EPOLLIN-BASED METHODS --------------------- //

// Reads into conn's buffer until read() errs with EAGAIN
// Returns 0 if \r\n\r\n not found, 1 if \r\n\r\n found
int _connection_read_to_buffer(int fd, connection_t* conn);

// Creates and populates connn's http_request_t struct (called once _connection_read_to_buffer returns 1/\r\n\r\n found)
// Returns 0 if it was able to properly form a http_request_t object, 1 if  not (400 bad request)
int _connection_populate_request(connection_t* conn);


// Handles EPOLLIN ET event. Returns 1 if done (ie \r\n\r\n is found, http_request_t is made, wb is serialized and ready to go), 0 if not
int connection_on_epollin(int fd,connection_t* conn, const char* docroot);

// --------------------- EPOLLOUT-BASED METHODS --------------------- //

// Constructs the http_response_t struct within the connection. Performs path validation and 
// will automatically handle error states (403, 404)
void _connection_construct_response(connection_t* conn, const char* docroot);


// Serializes conn's http_response_t by turning it into a string and saving it in conn's wb
void _connection_serialize_response(connection_t* conn);


// Writes from conn's write_buffer to the fd. Returns 0 if not done writing (wb_offset < wb_size), 1 if done writing (wb_offset == wb_size)
int _connection_write_to_client(int fd, connection_t* conn);
// Handles EPOLLOUT ET event. Returns 1 if done (fully sent message), 0 if not done (parts still remain, wb_offset<wb_size)
int connection_on_epollout(int fd,connection_t* conn);

#endif