#ifndef ARGS_H
#define ARGS_H

//  args.c/h is used to parse the CLI arguments & pack them into a 'conduit_args' struct
//  for easier use.

#define EXIT_USAGE_ERROR 2

//  Sanity limits 
#define THREAD_COUNT_MAX      1024
#define CONNS_PER_THREAD_MAX  65536

typedef struct conduit_args {
    int         port;               /* positional 1                             */
    const char *docroot;            /* positional 2                             */
    int         thread_count;       /* -t, default 4                            */
    int         conns_per_thread;   /* -c, default 20                           */
} conduit_args;

//  Print out the help text 
void print_help(int to_stderr);

//  Parse argc and argv[] from main() into args
void parse_argv(int argc, char *argv[], conduit_args *args);

#endif