#pragma once
#include <stddef.h>
#ifndef RESPONSE
#define RESPONSE
#define HEADER_KEY_SIZE  256
#define HEADER_VALUE_SIZE 512
#define HEADER_LINE_BYTES (HEADER_KEY_SIZE+HEADER_VALUE_SIZE+4)
// Define common status code reasons:
extern const char* const REASON_INTERNAL_SERVER_ERROR; // 500
extern const char* const REASON_NOT_FOUND; // 404
extern const char* const REASON_METHOD_NOT_ALLOWED; // 405
extern const char* const REASON_FORBIDDEN; // 403
extern const char* const REASON_BAD_REQUEST; // 400
extern const char* const REASON_OK; // 200
extern const char* const REASON_TEMP_UNAVAILABLE; // 503


typedef struct {
    int status_code;
    int header_count;
    size_t body_size;
    const char* body;
    int owns_body_flag; // 0= does not own body (malloc'd elsewhere), 1= owns body (must be free'd)
    char reason_phrase[256]; // Reason phrase usually small enough to just copy
    struct {
        char key[256];
        char value[512];
    } headers[32];

} http_response_t;

/* Functions populate it */
void response_set_status(http_response_t *resp, int code, const char *reason);
void response_add_header(http_response_t *resp, const char *key, const char *value);
void response_set_body(http_response_t *resp, const char *body, int length, int owns_body);

// Helper method for populating a response as an error. Errors follow the same format w/ differing code+reason
void response_fill_as_error(http_response_t *resp, int error_code, const char* reason);

// DEPRECATED SINCE TRD04
int response_send(int fd, http_response_t *resp);

// Performs cleanup & memory freeing 
void response_clean(http_response_t *resp);
#endif