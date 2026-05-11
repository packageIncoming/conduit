#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
int construct_filepath(const char *docroot, const  char *request_uri, char* buffer, size_t buffer_size){
    char buff_temp[buffer_size];
    if (strcmp(request_uri,"/")==0){
        if (docroot[strlen(docroot)-1] == '/') {
            snprintf(buff_temp,buffer_size, "%sindex.html",docroot);
        } else {
            snprintf(buff_temp, buffer_size,"%s/index.html",docroot);
        }
    } else {
        if (docroot[strlen(docroot)-1] == '/') {
            snprintf(buff_temp,buffer_size, "%s%s",docroot,request_uri+1);
        } else {
            snprintf(buff_temp, buffer_size,"%s%s",docroot,request_uri);
        }
    }
    if(realpath(buff_temp,buffer) != NULL){
        return 0;
    } else {
        return 1;
    }
}

int verify_path_starts_with_docroot(const char* docroot, const char* filepath){
    char* docroot_absolute_path = realpath(docroot,NULL);
    char* result = strstr(filepath,docroot_absolute_path);
    if (result == NULL){
        return 1; // did not find
    }
    if (filepath - result == 0){
        return 0; // found at the start
    }
    return 1; // Failed 
}