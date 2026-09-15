#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <getopt.h>
#include "args.h"

static int parse_int_strict(const char *str, int lo, int hi, int *out)
{
    char *end = NULL;
    long v;

    if (str == NULL || *str == '\0') { return 1; }

    errno = 0;
    v = strtol(str, &end, 10);

    if (errno != 0)       { return 1; }   /* ERANGE             */
    if (end == str)       { return 1; }   /* no digits consumed */
    if (*end != '\0')     { return 1; }   /* trailing garbage   */
    if (v < lo || v > hi) { return 1; }   /* out of range       */

    *out = (int)v;
    return 0;
}

void print_help(int to_stderr)
{
    FILE *out = to_stderr ? stderr : stdout;

    fprintf(out,
        "Usage: conduit [-t <threads>] [-c <conns_per_thread>] <port> <docroot>\n"
        "\n"
        "Required:\n"
        "  <port>                 TCP port to listen on (1-65535)\n"
        "  <docroot>              Directory to serve files from\n"
        "\n"
        "Server Options:\n"
        "  -t <threads>           Worker threads (default: 4, max: %d)\n"
        "  -c <conns_per_thread>  Max concurrent connections per thread\n"
        "                         (default: 20, max: %d). Connections beyond\n"
        "                         threads * conns_per_thread are refused with 503.\n"
        "\n"
        "  -h, --help             Show this help message and exit\n",
        THREAD_COUNT_MAX, CONNS_PER_THREAD_MAX);
}

void parse_argv(int argc, char *argv[], conduit_args *args)
{
    static struct option long_opts[] = {
        {"threads",           required_argument, NULL, 't'},
        {"conns-per-thread",  required_argument, NULL, 'c'},
        {"help",              no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;

    args->port             = 0;
    args->docroot          = NULL;
    args->thread_count     = 4;
    args->conns_per_thread = 20;

    while ((opt = getopt_long(argc, argv, "t:c:h", long_opts, NULL)) != -1)
    {
        switch (opt)
        {
            case 't':
            {
                if (parse_int_strict(optarg, 1, THREAD_COUNT_MAX,
                                     &args->thread_count) != 0)
                {
                    fprintf(stderr,
                        "-t requires an integer in [1, %d], got '%s'\n",
                        THREAD_COUNT_MAX, optarg);
                    exit(EXIT_USAGE_ERROR);
                }
                break;
            }
            case 'c':
            {
                if (parse_int_strict(optarg, 1, CONNS_PER_THREAD_MAX,
                                     &args->conns_per_thread) != 0)
                {
                    fprintf(stderr,
                        "-c requires an integer in [1, %d], got '%s'\n",
                        CONNS_PER_THREAD_MAX, optarg);
                    exit(EXIT_USAGE_ERROR);
                }
                break;
            }
            case 'h':
            {
                print_help(0);
                exit(EXIT_SUCCESS);
            }
            default:
            {
                print_help(1);
                exit(EXIT_USAGE_ERROR);
            }
        }
    }

    if (argc - optind < 2)
    {
        fprintf(stderr, "missing required arguments: <port> <docroot>\n\n");
        print_help(1);
        exit(EXIT_USAGE_ERROR);
    }
    if (argc - optind > 2)
    {
        fprintf(stderr, "unexpected extra argument: '%s'\n\n", argv[optind + 2]);
        print_help(1);
        exit(EXIT_USAGE_ERROR);
    }

    if (parse_int_strict(argv[optind], 1, 65535, &args->port) != 0)
    {
        fprintf(stderr, "<port> must be an integer in [1, 65535], got '%s'\n",
                argv[optind]);
        exit(EXIT_USAGE_ERROR);
    }
    args->docroot = argv[optind + 1];

}