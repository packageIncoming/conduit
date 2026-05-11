#ifndef DOCHANDLER
#define DOCHANDLER



//Construct a filesystem path from the document root and the request URI. GET / maps to <docroot>/index.html. All other paths map to <docroot> + path.
// Returns 0 if successful, 1 if not
int construct_filepath(const char *docroot, const  char *request_uri, char* buffer, size_t buffer_size);

// Resolve the constructed path with realpath(). 
// Verify the resolved path begins with the document root prefix,
// 0 if yes 1 if not (fail)
int verify_path_starts_with_docroot(const char* docroot, const char* filepath);

// Given a file descriptor fd opened with open(), this method will get its size using fstat()
int get_file_size_from_fd(int fd);

// Attempts to read the entire contents from the fd into the buffer with a size buffer_size. Returns 0 if successful, 1 if failed
int read_file_contents_to_buffer(int fd, void* buffer, size_t buffer_size);

#endif