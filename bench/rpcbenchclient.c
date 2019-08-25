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

static uint8_t *bench_payload = NULL;
static uint32_t bench_upload_size = 0;
static uint32_t bench_download_size = 0;
static uint32_t bench_op_code = BENCH_OP_DOWNLOAD;
static int bench_num_requests = 0;

static double
do_upload_bench(struct rpc_agent *agent)
{
    int i = 0;
    int error = 0;
    struct rho_buf *buf = agent->ra_bodybuf;
    struct timeval start;
    struct timeval end;
    struct timeval elapsed;

    (void)gettimeofday(&start, NULL);
    for (i = 0; i < bench_num_requests; i++) {
        rpc_agent_new_msg(agent, bench_op_code);
        rpc_agent_set_bodylen(agent, bench_upload_size);
        rho_buf_write(buf, bench_payload, bench_upload_size);

        error = rpc_agent_request(agent);
        if (error != 0)
            rho_die("rpc_agent_request returned %d", error);

        rho_debug("%d/%d status=%"PRIu32", download size=%"PRIu32,
                i, bench_num_requests,
                agent->ra_hdr.rh_code, agent->ra_hdr.rh_bodylen);
    }
    (void)gettimeofday(&end, NULL);

     rho_timeval_subtract(&end, &start, &elapsed);

    return (rho_timeval_to_sec_double(&elapsed) / (1.0 * bench_num_requests));
}

static double
do_download_bench(struct rpc_agent *agent)
{
    int i = 0;
    int error = 0;
    struct rho_buf *buf = agent->ra_bodybuf;
    struct timeval start;
    struct timeval end;
    struct timeval elapsed;

    (void)gettimeofday(&start, NULL);
    for (i = 0; i < bench_num_requests; i++) {
        rpc_agent_new_msg(agent, bench_op_code);

        error = rpc_agent_request(agent);
        if (error != 0)
            rho_die("rpc_agent_request returned %d", error);

        bench_download_size = agent->ra_hdr.rh_bodylen;
        if (bench_download_size > 0)
            rho_buf_read(buf, bench_payload, bench_download_size);

        rho_debug("%d/%d status=%"PRIu32", download size=%"PRIu32,
                i, bench_num_requests, 
                agent->ra_hdr.rh_code, agent->ra_hdr.rh_bodylen);
    }
    (void)gettimeofday(&end, NULL);

    rho_timeval_subtract(&end, &start, &elapsed);

    return (rho_timeval_to_sec_double(&elapsed) / (1.0 * bench_num_requests));
}

static struct rpc_agent *
do_connect(const char *url, const char *root_crt_path)
{
    int error = 0;
    struct rho_sock *sock = NULL;
    struct rpc_agent *agent = NULL;
    struct rho_ssl_params *params = NULL;
    struct rho_ssl_ctx *ctx = NULL;

    sock = rho_sock_from_url(url);
    if (sock == NULL)
        rho_die("unable to form socket for url \"%s\"", url);

    error = rho_sock_connect_url(sock, url);
    if (error == -1)
        rho_errno_die(errno, "cannot connect to url \"%s\"", url);

    if (rho_str_startswith(url, "tcp:") || rho_str_startswith(url, "tcp4:")) { 
        fprintf(stderr, "disabling nagle's algorithm for tcp\n");
        rhoL_setsockopt_disable_nagle(sock->fd);
    }

    if (root_crt_path != NULL) {
        rho_debug("using TLS");
        rho_ssl_init();
        params = rho_ssl_params_create();
        rho_ssl_params_set_mode(params, RHO_SSL_MODE_CLIENT);
        rho_ssl_params_set_protocol(params, RHO_SSL_PROTOCOL_TLSv1_2);
        rho_ssl_params_set_ca_file(params, root_crt_path);
        rho_ssl_params_set_verify(params, true);
        ctx = rho_ssl_ctx_create(params);
        rho_ssl_wrap(sock, ctx);
        rho_ssl_params_destroy(params);

        while (1) {
            error = rho_ssl_do_handshake(sock);
            if (error == 0)
                goto done;
            if (error == -1)
                rho_die("rho_ssl_do_handshake failed");
        }
    }

done:
    agent = rpc_agent_create(sock, NULL);
    return (agent);
}

#define BENCHCLIENT_USAGE \
    "usage: benchclient [options] URL\n" \
    "\n" \
    "OPTIONS:\n" \
    "   -c RPC_COMMAND\n" \
    "       Must be UPLOAD or DOWNLOAD.  Default is DOWNLOAD.\n" \
    "\n" \
    "   -h\n" \
    "       Show this help message and exit\n" \
    "\n" \
    "   -r ROOT_CRT\n" \
    "       The root certificate path.  If specified, the RPCs\n" \
    "       use server-authenticated TLS.\n" \
    "\n" \
    "   -s SECONDS\n" \
    "       Sleep for SECONDS before starting the test.\n" \
    "       This can be useful if using external profile tools\n" \
    "       that need the client's PID or TID.\n" \
    "\n" \
    "   -u UPLOAD_SIZE\n" \
    "       If testing UPLOADS, the size of the request body.\n" \
    "       Must be <= 10MB \n" \
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
    int c = 0;
    struct rpc_agent *agent = NULL;
    const char *root_crt = NULL;
    double mean = 0;
    uint32_t sleep_secs = 0;


    while ((c = getopt(argc, argv, "c:hr:s:u:")) != -1) {
        switch (c) {
        case 'c':
            if (rho_str_equal_ci(optarg, "UPLOAD")) {
                bench_op_code = BENCH_OP_UPLOAD;
            } else if (rho_str_equal_ci(optarg, "DOWNLOAD")) {
                bench_op_code = BENCH_OP_DOWNLOAD;
            } else {
                fprintf(stderr, "invalid option for -c \"%s\"", optarg);
                exit(1);
            }
            break;
        case 'h':
            usage(EXIT_SUCCESS);
            break;
        case 'r':
            root_crt = optarg;
            break;
        case 's':
            sleep_secs = rho_str_touint32(optarg, 10);
            break;
        case 'u':
            bench_upload_size = rho_str_touint32(optarg, 10);
            if (bench_upload_size > BENCH_MAX_PAYLOAD_SIZE) {
                fprintf(stderr, "upload size must be less than %u",
                        BENCH_MAX_PAYLOAD_SIZE);
                exit(1);
            }
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

    bench_payload = rhoL_zalloc(BENCH_MAX_PAYLOAD_SIZE);

    agent = do_connect(argv[0], root_crt);

    if (sleep_secs > 0)
        sleep(sleep_secs);

    printf("starting test\n");

    if (bench_op_code == BENCH_OP_UPLOAD) {
        rho_debug("doing %d upload requests", bench_num_requests);
        mean = do_upload_bench(agent);
        printf("mean time for a BENCH_OP_UPLOAD RPC of %"PRIu32" bytes (based on %d runs): %.9f s, (%.9g)\n",
                bench_upload_size, bench_num_requests, mean, mean);
    } else if (bench_op_code == BENCH_OP_DOWNLOAD) {
        rho_debug("doing %d download requests", bench_num_requests); 
        mean = do_download_bench(agent);
        printf("mean time for a BENCH_OP_DOWNLOAD RPC of %"PRIu32" bytes (based on %d runs): %.9f s, (%.9g)\n",
                bench_download_size, bench_num_requests, mean, mean);
    } else {
        RHO_ASSERT("invalid bench op code");
    }

    rpc_agent_destroy(agent);
    rhoL_free(bench_payload);

    return (0);
}
