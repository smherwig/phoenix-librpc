#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rho/rho.h>

/* 
 * Note that emmintrin.h also defines the intrinsic _mm_clflush,
 * as an alternative to writing in-line assembly
 */

#define CACHELINE_SIZE 64U

#define MEMCPY_BENCH_USAGE \
    "usage: memcpy_bench [options] SIZE ITERATIONS\n" \
    "\n" \
    "OPTIONS:\n" \
    "\n" \
    "   -b\n" \
    "       Do both the non-flushed and cache-flushed\n" \
    "       versions of the benchmark.\n" \
    "   -h\n" \
    "       Show this help message and exit\n" \
    "   -f\n" \
    "       Flush the src and dst buffers from the\n" \
    "       CPU cache lines before each mempcy\n" \
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

__attribute__((always_inline))
static inline void
flush(volatile void *addr)
{

    //asm volatile("clflush %0" : "+m" (addr));

    asm __volatile__ (
        "mfence         \n"
        "clflush 0(%0)  \n"
        :
        : "r" (addr)
        :
        );
}


__attribute__((always_inline))
static inline void
flushall(volatile void *addr, size_t size)
{
    size_t i = 0;
    for (i = 0; i < size; i += CACHELINE_SIZE)
        flush(addr + i);
}

static double
do_flush_buf_from_cache_bench(size_t size, int n)
{
    int i = 0;
    size_t offset = 0;
    struct timeval start;
    struct timeval tmp;
    struct timeval end;
    struct timeval elapsed;
    double mean_gtod = 0.0;
    double flush_total = 0.0;
    double mean_flush = 0.0;
    void *p = NULL;

    /* measure the cost of gettimeofday */
    (void)gettimeofday(&start, NULL);
    for (i = 0; i < n; i++)
        gettimeofday(&tmp, NULL);
    (void)gettimeofday(&end, NULL);

    rho_timeval_subtract(&end, &start, &elapsed);
    mean_gtod = (rho_timeval_to_sec_double(&elapsed) / (1.0 * n));
    printf("mean gettimeofday: %.9f (%.9g) s\n", mean_gtod, mean_gtod);

    p = rhoL_malloc(size);

    /* measure what a flush of a cacheline costs */
    for (i = 0; i < n; i++) {
        rho_rand_bytes(p, size);
        (void)gettimeofday(&start, NULL);
        for (offset = 0; offset < size; offset += CACHELINE_SIZE)
            flush(p + offset);
        (void)gettimeofday(&end, NULL);

        rho_timeval_subtract(&end, &start, &elapsed);
        flush_total += rho_timeval_to_sec_double(&elapsed);
    }
    
    /* we assume that gettimeofday is like a round-trip; 
     * we subtract the "coming back" time on the first and the
     * "going forth" time on the second call.
     */
    mean_flush = (flush_total / (1.0 * n)) - mean_gtod;
    printf("mean flush: %.9f (%.9g) s\n", mean_flush, mean_flush);

    rhoL_free(p);

    return (mean_flush);
}

static double
do_memcpy_bench(void *src, void *dst, size_t size, int n)
{
    int i = 0;
    struct timeval start;
    struct timeval end;
    struct timeval elapsed;
    double mean = 0;

    (void)gettimeofday(&start, NULL);
    for (i = 0; i < n; i++)
        memcpy(src, dst, size);
    (void)gettimeofday(&end, NULL);

    rho_timeval_subtract(&end, &start, &elapsed);
    mean = (rho_timeval_to_sec_double(&elapsed) / (1.0 * n));

    return (mean);
}

int
main(int argc, char *argv[])
{
    int c = 0;
    unsigned int size = 0;
    int n = 0;
    uint8_t *src = NULL;
    uint8_t *dst = NULL;
    bool do_cacheflush_bench = false;
    bool do_both = false;
    double mean = 0.0;

    while ((c = getopt(argc, argv, "bhf")) != -1) {
        switch (c) {
        case 'h':
            usage(EXIT_SUCCESS);
            break;
        case 'b':
            do_both = true;
            break;
        case 'f':
            do_cacheflush_bench = true;
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

    if (do_cacheflush_bench || do_both) {
        mean = do_flush_buf_from_cache_bench(size, n);
        printf("mean time to flush a buffer of size %u from cache (based on %d runs): %.9f s, (%.9g)\n",
            size, n, mean, mean);
    }

    if (!do_cacheflush_bench || do_both) {
        mean = do_memcpy_bench(src, dst, size, n);
        printf("mean time for a memcpy of %u bytes (based on %d runs): %.9f s, (%.9g)\n",
            size, n, mean, mean);
    }

    return (0);
}
