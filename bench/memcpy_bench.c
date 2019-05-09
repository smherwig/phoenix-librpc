#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rho/rho.h>

#define MEMCPY_BENCH_USAGE \
    "usage: memcpy_bench [options] SIZE ITERATIONS\n" \
    "\n" \
    "OPTIONS:\n" \
    "\n" \
    "   -h\n" \
    "       Show this help message and exit\n" \
    "\n" \
    "ARGUMENTS:\n" \
    "   SIZE\n" \
    "       The size of the buffer to memcpy\n" \
    "\n" \
    "   ITERATIONS\n" \
    "       The number of copies to perform\n"

static void
usage(int exitcode)
{
    fprintf(stderr, "%s\n", MEMCPY_BENCH_USAGE);
    exit(exitcode);
}

int
main(int argc, char *argv[])
{
    int c = 0;
    unsigned int size = 0;
    int i = 0;
    int n = 0;
    uint8_t *src = NULL;
    uint8_t *dst = NULL;
    struct timeval start;
    struct timeval end;
    struct timeval elapsed;
    double mean = 0;

    while ((c = getopt(argc, argv, "h")) != -1) {
        switch (c) {
        case 'h':
            usage(EXIT_SUCCESS);
            break;
        default:
            usage(1);
        }
    }
    argc -= optind;
    argv += optind;

    if (argc != 2)
        usage(1);

    size = rho_str_touint(argv[0], 10);
    n = rho_str_toint(argv[1], 10);

    src = rhoL_malloc(size);
    dst = rhoL_malloc(size);

    (void)gettimeofday(&start, NULL);
    for (i = 0; i < n; i++)
        memcpy(src, dst, size);
    (void)gettimeofday(&end, NULL);

    rho_timeval_subtract(&end, &start, &elapsed);

    mean = (rho_timeval_to_sec_double(&elapsed) / (1.0 * n));
    printf("mean time for a memcpy of %u bytes (based on %d runs): %.9f s, (%.9g)\n",
            size, n, mean, mean);

    return (0);
}



