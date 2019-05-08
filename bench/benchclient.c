#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rho/rho.h>
#include <rpc.h>

#include "bench.h"

static uint8_t *bench_upload_data = NULL;
static uint32_t bench_upload_size = 0;
static uint32_t bench_op_code = BENCH_OP_DOWNLOAD;
static int bench_num_requests = 0;
struct timeval *bench_results = NULL;

static int
timeval_cmp(const void *a, const void *b)
{
    return (rho_timeval_cmp(a,b));
}

static double
trimmed_mean(void)
{
    int i = 0;
    int x = 0;
    double sum = 0.0;

    qsort(bench_results, bench_num_requests, sizeof(struct timeval), timeval_cmp);

    x = (int)(0.3 * bench_num_requests);
    for (i = x; i < (bench_num_requests - x); i++)
        sum += rho_timeval_to_sec_double(&bench_results[i]);

    sum /= (1.0 * bench_num_requests - (2 * x));

    return (sum);
}

static void
do_upload_bench(struct rpc_agent *agent)
{
    int i = 0;
    int error = 0;
    struct rho_buf *buf = agent->ra_bodybuf;
    struct timeval start;
    struct timeval end;
    struct timeval elapsed;

    for (i = 0; i < bench_num_requests; i++) {
        rpc_agent_new_msg(agent, bench_op_code);
        rpc_agent_set_bodylen(agent, bench_upload_size);
        rho_buf_write(buf, bench_upload_data, bench_upload_size);

        (void)gettimeofday(&start, NULL);
        error = rpc_agent_request(agent);
        (void)gettimeofday(&end, NULL);
        rho_timeval_subtract(&end, &start, &elapsed);
        bench_results[i] = elapsed;

        if (error != 0)
            rho_die("rpc_agent_request returned %d", error);

        rho_debug("%d/%d status=%"PRIu32", download size=%"PRIu32,
                i, bench_num_requests,
                agent->ra_hdr.rh_code, agent->ra_hdr.rh_bodylen);
    }

    return;
}

static void
do_download_bench(struct rpc_agent *agent)
{
    int i = 0;
    int error = 0;
    struct timeval start;
    struct timeval end;
    struct timeval elapsed;

    for (i = 0; i < bench_num_requests; i++) {
        rpc_agent_new_msg(agent, bench_op_code);

        (void)gettimeofday(&start, NULL);
        error = rpc_agent_request(agent);
        (void)gettimeofday(&end, NULL);
        rho_timeval_subtract(&end, &start, &elapsed);
        bench_results[i] = elapsed;

        if (error != 0)
            rho_die("rpc_agent_request returned %d", error);

        rho_debug("%d/%d status=%"PRIu32", download size=%"PRIu32,
                i, bench_num_requests, 
                agent->ra_hdr.rh_code, agent->ra_hdr.rh_bodylen);
    }

    return;
}

#define BENCHCLIENT_USAGE \
    "usage: benchclient [options] URL\n" \
    "\n" \
    "OPTIONS:\n" \
    "   -c RCP_COMMAND\n" \
    "       Must be UPLOAD or DOWNLOAD.  Default is DOWNLOAD.\n" \
    "\n" \
    "   -h\n" \
    "       Show this help message and exit\n" \
    "\n" \
    "   -u UPLOAD_SIZE\n" \
    "       If testing UPLOADS, the size of the request body\n" \
    "\n" \
    "ARGUMENTS:\n" \
    "   URL\n" \
    "       The URL to connect to.\n" \
    "       (e.g., unix:///tmp/foo, tcp://127.0.0.1:8089)\n" \
    "\n" \
    "   REQUESTS\n" \
    "       The number of requests to perform\n"


static void
usage(int exitcode)
{
    fprintf(stderr, "%s\n", BENCHCLIENT_USAGE);
    exit(exitcode);
}

int
main(int argc, char *argv[])
{
    int error = 0;
    int c = 0;
    const char *url = NULL;
    struct rho_sock *sock = NULL;
    struct rpc_agent *agent = NULL;

    while ((c = getopt(argc, argv, "c:hu:")) != -1) {
        switch (c) {
        case 'c':
            if (rho_str_equal_ci(optarg, "UPLOAD"))
                bench_op_code = BENCH_OP_UPLOAD;
            else if (rho_str_equal_ci(optarg, "DOWNLOAD"))
                bench_op_code = BENCH_OP_DOWNLOAD;
            else
                fprintf(stderr, "invalid option for -c \"%s\"", optarg);
                exit(1);
            break;
        case 'h':
            usage(EXIT_SUCCESS);
            break;
        case 'u':
            bench_upload_size = rho_str_touint32(optarg, 10);
            bench_upload_data = rhoL_zalloc(bench_upload_size);
            break;
        default:
            usage(1);
        }
    }
    argc -= optind;
    argv += optind;

    if (argc != 2)
        usage(1);

    bench_num_requests = rho_str_toint(argv[1], 10);
    RHO_ASSERT(bench_num_requests > 0);
    bench_results = rhoL_mallocarray(bench_num_requests,
            sizeof(struct timeval), RHO_MEM_ZERO); 

    url = argv[0];
    sock = rho_sock_from_url(url);
    if (sock == NULL)
        rho_die("unable to form socket for url \"%s\"", url);

    error = rho_sock_connect_url(sock, url);
    if (error == -1)
        rho_errno_die(errno, "cannot connect to url \"%s\"", url);

    if (rho_str_startswith(url, "tcp:") || rho_str_startswith(url, "tcp4:"))
        rhoL_setsockopt_disable_nagle(sock->fd);

    agent = rpc_agent_create(sock, NULL);

    if (bench_op_code == BENCH_OP_UPLOAD) {
        rho_debug("doing %d upload requests", bench_num_requests);
        do_upload_bench(agent);
    } else if (bench_op_code == BENCH_OP_DOWNLOAD) {
        rho_debug("doing %d download requests", bench_num_requests); 
        do_download_bench(agent);
    } else {
        RHO_ASSERT("invalid bench op code");
    }

    rpc_agent_destroy(agent);
    if (bench_upload_data != NULL)
        rhoL_free(bench_upload_data);

    printf("30%% trimmed mean time for RPC request-response: %.9g\n",
            trimmed_mean());

    rhoL_free(bench_results);

    return (0);
}
