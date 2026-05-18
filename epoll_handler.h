#pragma once
#include "request.h"
#include "response.h"
#ifndef EPOLL_HANDLER
#define EPOLL_HANDLER
#define MAXEVENTS 100
#define READBUFFER_SIZE 8192
#define TIMEOUT_SECONDS 30


enum CONN_STATUS {CONN_READING, CONN_WRITING, CONN_DONE};

typedef struct threadpool threadpool_t; 
typedef struct {
    int fd;
    enum CONN_STATUS status ;
    char* write_buffer;
    size_t wb_size;
    int wb_offset;
    int rb_offset;
    time_t last_active;
    http_request_t* request;
    http_response_t* response;
    char read_buffer[READBUFFER_SIZE];
} connection_t; 

typedef struct conn_node_s {
    connection_t* connection;
    struct conn_node_s* prev;
    struct conn_node_s* nxt;
     
} conn_node_t;

// FIFO Queue LinkedList implementation; keeps track of connections with state CONN_READING; used to close connections that timeout
typedef struct{
    int conn_count;
    conn_node_t* head; // dummy head 
    conn_node_t* tail; // Append new connections to the end
} conn_list_t; 

// allocates conn_list_t
conn_list_t* conn_list_init(); 

// destructor for conn_list_t
void conn_list_destroy(conn_list_t* conn_list);

// sweeps through the given conn_list and closes the connections who have timed out (now - last_active >= timeout); returns # of timed out connections
int conn_list_sweep(conn_list_t* conn_list, int timeout);

// Removes a connection from the linked list by searching through using the pointer 
void conn_list_remove_by_connection(conn_list_t* conn_list, connection_t* connection);

// Appends conn_node to end of conn_list (Linked List)
void conn_list_enqueue(conn_list_t* conn_list, conn_node_t* conn_node);

// Sets a file descriptor to be nonblocking using fcntl(fd, F_SETFL, flags | O_NONBLOCK)
void setnonblocking(int fd);

// Frees the write and read buffers that were malloc'd
void connection_free(connection_t* connection);

// Adds new connections to the associated epoll instance until accept() returns -1 (EAGAIN)
// Adds with flags EPOLLIN | EPOLLET
// Adds to conn_list 
void add_new_connections(int epollFD, int listenFD,threadpool_t* threadpool,conn_list_t* conn_list);


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
// Handles EPOLLOUT ET event. Returns 1 if done (fully sent message), 
// 0 if not done (parts still remain, wb_offset<wb_size), -1 on error
int connection_on_epollout(int fd,connection_t* conn);

#endif