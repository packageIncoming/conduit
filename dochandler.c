#define _XOPEN_SOURCE 500
#include "dochandler.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

const char* const HTML = "text/html";
const char* const CSS = "text/css";
const char* const JS = "application/javascript";
const char* const TEXT = "text/plain";
const char* const JPG = "image/jpeg";
const char* const PNG = "image/png";
const char* const UNKNOWN = "application/octet-stream";


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
    if (docroot_absolute_path == NULL){
        return 1; // docroot itself does not resolve; refuse rather than deref NULL
    }
    size_t root_len = strlen(docroot_absolute_path);

    // Must match at position 0, not merely appear somewhere in the path.
    if (strncmp(filepath, docroot_absolute_path, root_len) != 0){
        free(docroot_absolute_path);
        return 1;
    }

    // The match must end on a path-component boundary. Without this,
    // docroot "/srv/www" would accept "/srv/www-evil/secret.txt".
    char next = filepath[root_len];
    if (next != '/' && next != '\0'){
        free(docroot_absolute_path);
        return 1;
    }

    free(docroot_absolute_path);
    return 0;
}

int get_file_size_from_fd(int fd){
    struct stat sb;
    if (fstat(fd,&sb) == -1){
        return -1;
    }
    return sb.st_size;
}

const char*  get_mime_from_filepath(const char* filepath){
    char* extension = strrchr(filepath,'.');

    if (extension == NULL){
        return UNKNOWN;
    }
    
    if (strcmp(extension,".html")==0){
        return HTML;
    }
    if (strcmp(extension,".css")==0){
        return CSS;
    }
    if (strcmp(extension,".js")==0){
        return JS;
    }
    if (strcmp(extension,".jpg")==0){
        return JPG;
    }
    if (strcmp(extension,".png")==0){
        return PNG;
    }
    if (strcmp(extension,".txt")==0){
        return TEXT;
    }
    return UNKNOWN;
}

int read_file_contents_to_buffer(int fd, char* buffer, size_t buffer_size){
    int read_bytes = 0;
    int flag=0;
    while (1){
        int read_count = read(fd,buffer+read_bytes,buffer_size-read_bytes);
        if (read_count == 0){
            // reached EOF
            break;
        }
        if (read_count == -1){
            // error
            flag=1;
            break;
        }
        read_bytes += read_count;
    }
    buffer[read_bytes]='\0';

    return flag;
}