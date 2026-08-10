#pragma once
#ifndef REQUEST
#define REQUEST

extern const char* const CRLF;

#define MAX_REQUEST_HEADERS 32

typedef struct {
    char method[8];
    char path[1024];
    char version[16];
    struct {
        char key[256];
        char value[512];
    } headers[MAX_REQUEST_HEADERS]; // Creates a struct 'header' that has a 'key' and a 'value'.
    int header_count;

} http_request_t;



// Reads from client fd into a buffer, parses buffer contents into http_request's attributes
// Returns 0 if a valid request, 1 if invalid
int request_populate_from_fd(int clientFD, http_request_t* http_request);

#endif