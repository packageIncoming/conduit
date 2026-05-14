#ifndef REQUEST
#define REQUEST

extern const char* const CRLF;

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



// Reads from client fd into a buffer, parses buffer contents into http_request's attributes
// Returns 0 if a valid request, 1 if invalid
int request_populate_from_fd(int clientFD, http_request_t* http_request);

#endif