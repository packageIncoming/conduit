// Define common status code reasons:
#include <stddef.h>
#include <stdlib.h>
#include "response.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "dochandler.h"

const char* const REASON_INTERNAL_SERVER_ERROR = "Internal Server Error"; // 500
const char* const REASON_NOT_FOUND = "Not Found"; // 404
const char* const REASON_METHOD_NOT_ALLOWED = "Method Not Allowed"; // 405
const char* const REASON_FORBIDDEN = "Forbidden"; // 403
const char* const REASON_BAD_REQUEST = "Bad Request"; // 400

const char* const REASON_OK = "OK"; // 200


void response_set_status(http_response_t *resp, int code, const char *reason){
    memcpy(resp->reason_phrase,reason,strlen(reason));
    resp->status_code=code;
}

void response_add_header(http_response_t *resp, const char *key, const char *value){
    char* resp_key = resp->headers[resp->header_count].key;
    char* resp_val = resp->headers[resp->header_count].value;

    memcpy(resp_key,key,strlen(key));
    memcpy(resp_val,value,strlen(value));


    resp->header_count+=1;
}

void response_set_body(http_response_t *resp, const char *body, int length, int owns_body){
    resp->body=body;
    resp->body_size=length;
    resp->owns_body_flag=owns_body;
}

void response_clean(http_response_t *resp){
    if (resp->owns_body_flag==1){
        free((char *)resp->body);
    }
}

// DEPRECATED
int response_send(int fd, http_response_t *resp){
    // calculate response size
    int response_size = resp->body_size + strlen(resp->reason_phrase) + HEADER_LINE_BYTES*resp->header_count + 18;
    char final_response[response_size];
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
    // write to fd
    write(fd, final_response, strlen(final_response));
    return 0;
}

void response_fill_as_error(http_response_t *resp, int error_code, const char* reason){
    response_add_header(resp,"Content-Type",TEXT);
    response_add_header(resp,"Connection","close");
    response_set_status(resp,error_code,reason);
    response_set_body(resp,resp->reason_phrase,strlen(resp->reason_phrase),0);
}