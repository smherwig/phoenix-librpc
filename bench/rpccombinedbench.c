#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>

#include <rho/rho.h>
#include <rpc.h>

#include "bench.h"

#define RPCBENCH_USAGE \
    "usage: rpcbench [options] URL PAYLOAD_SIZE REQUESTS\n" \
    "\n" \
    "OPTIONS:\n" \
    "   -a\n" \
    "       Treat URL path as an abstract socket\n" \
    "       (adds a leading nul byte to path)\n" \
    "\n" \
    "   -c RPC_COMMAND\n" \
    "       Must be UPLOAD or DOWNLOAD.  Default is DOWNLOAD.\n" \
    "\n" \
    "   -h\n" \
    "       Show this help message and exit\n" \
    "\n" \
    "   -l LOG_FILE\n" \
    "       Log file to use.  If not specified, logs are printed to stderr.\n" \
    "       If specified, stderr is also redirected to the log file.\n" \
    "\n" \
    "   -v\n" \
    "       Verbose logging.\n" \
    "\n" \
    "   -Z  CACERT CERT PRIVKEY\n" \
    "       Sets the path to the server certificate file and private key\n" \
    "       in PEM format.  This also causes the server to start SSL mode\n" \
    "\n" \
    "\n" \
    "ARGUMENTS:\n" \
    "   URL\n" \
    "       The URL to listen to connection on.\n" \
    "       (e.g., unix:///tmp/foo, tcp://127.0.0.1:8089)\n" \
    "\n" \
    "   PAYLOAD_SIZE\n" \
    "       The size of the download/upload body (must be <= 10 MiB)\n" \
    "\n" \
    "   REQUESTS\n" \
    "       The number of requests to perform\n"

struct rpcserver {
    struct rho_sock *srv_sock;
    struct rho_ssl_ctx *srv_sc;
    /* TODO: don't hardcode 108 */
    uint8_t srv_udspath[108];
};

struct rpcserver_client {
    struct rpc_agent *cli_agent;
};

typedef void (*bench_opcall)(struct rpcserver_client *client);

/**************************************
 * FORWARD DECLARATIONS
 **************************************/
static void rpcserver_upload_proxy(struct rpcserver_client *client);
static void rpcserver_download_proxy(struct rpcserver_client *client);

static struct rpcserver_client * rpcserver_client_alloc(void);
static struct rpcserver_client * rpcserver_client_create(struct rho_sock *sock);
static void rpcserver_client_destroy(struct rpcserver_client *client);

static void rpcserver_client_dispatch_call(struct rpcserver_client *client);
static void rpcserver_client_cb(struct rho_event *event, int what,
        struct rho_event_loop *loop);

static void rpcserver_destroy(struct rpcserver *server);
static void rpcserver_config_ssl(struct rpcserver *server,
        const char *cafile, const char *certfile, const char *keyfile);
static void rpcserver_socket_create(struct rpcserver *server,
        const char *url, bool anonymous);
static void rpcserver_cb(struct rho_event *event, int what,
        struct rho_event_loop *loop);

static void bench_log_init(const char *logfile, bool verbose);

static void usage(int exitcode);

/**************************************
 * GLOBALS
 **************************************/

static struct rho_log *bench_log = NULL;

static uint8_t *g_bench_payload = NULL;
static uint32_t g_bench_payload_size = 0;
static uint32_t g_bench_op_code = BENCH_OP_DOWNLOAD;
static int g_bench_num_requests = 0;

static bench_opcall bench_opcalls[] = {
    [BENCH_OP_UPLOAD]   = rpcserver_upload_proxy,
    [BENCH_OP_DOWNLOAD] = rpcserver_download_proxy,
};

/**************************************
 * RPCS
 **************************************/
/* 
 * client sends non-empty body
 * server does not process body
 * server responds with empty body
 */
static void
rpcserver_upload_proxy(struct rpcserver_client *client)
{
    struct rpc_agent *agent = client->cli_agent;
    struct rho_buf *buf = agent->ra_bodybuf;
    uint32_t size = 0;

    RHO_TRACE_ENTER("bodylen=%"PRIu32, agent->ra_hdr.rh_bodylen);

    size = agent->ra_hdr.rh_bodylen;
    if (size > 0)
        rho_buf_read(buf, g_bench_payload, size);

    rpc_agent_new_msg(agent, 0);

    RHO_TRACE_EXIT();
    return;
}

/* 
 * client should sends empty body, 
 * server responds with non-empty body
 */
#if 0
static uint32_t tot_rpcs = 0;
#endif
static void
rpcserver_download_proxy(struct rpcserver_client *client)
{
    struct rpc_agent *agent = client->cli_agent;
    struct rho_buf *buf = agent->ra_bodybuf;

    RHO_TRACE_ENTER("bodylen=%"PRIu32, agent->ra_hdr.rh_bodylen);

    rpc_agent_new_msg(agent, 0);
    rpc_agent_set_bodylen(agent, g_bench_payload_size);
    /* XXX: ideally, the bench measuremnts wouldn't include this memcpy */
    rho_buf_write(buf, g_bench_payload, g_bench_payload_size);

#if 0
    rho_log_info(bench_log, "RPC %"PRIu32, tot_rpcs);
    tot_rpcs++;
#endif

    RHO_TRACE_EXIT();
    return;
}

/*********************************************************
 * RPCSERVER_CLIENT
 *********************************************************/

static struct rpcserver_client *
rpcserver_client_alloc(void)
{
    struct rpcserver_client *client = NULL;

    RHO_TRACE_ENTER();

    client = rhoL_zalloc(sizeof(*client));
    client->cli_agent = rpc_agent_create(NULL, NULL);

    RHO_TRACE_EXIT();
    return (client);
}

static struct rpcserver_client *
rpcserver_client_create(struct rho_sock *sock)
{
    struct rpcserver_client *client = NULL;
    struct rpc_agent *agent = NULL;

    RHO_TRACE_ENTER();

    client = rpcserver_client_alloc();
    agent = client->cli_agent;
    agent->ra_sock = sock;

    /* has an ssl_ctx */
    if (sock->ssl != NULL)
        agent->ra_state = RPC_STATE_HANDSHAKE;
    else
        agent->ra_state = RPC_STATE_RECV_HDR;

    RHO_TRACE_EXIT();
    return (client);
}

static void
rpcserver_client_destroy(struct rpcserver_client *client)
{
    RHO_ASSERT(client != NULL);

    RHO_TRACE_ENTER();

    rpc_agent_destroy(client->cli_agent);
    rhoL_free(client);

    RHO_TRACE_EXIT();
}

static void
rpcserver_client_dispatch_call(struct rpcserver_client *client)
{
    struct rpc_agent *agent = client->cli_agent;
    uint32_t opcode = agent->ra_hdr.rh_code;
    bench_opcall opcall = NULL;

    RHO_ASSERT(agent->ra_state == RPC_STATE_DISPATCHABLE);
    RHO_ASSERT(rho_buf_tell(agent->ra_bodybuf) == 0);

    RHO_TRACE_ENTER("fd=%d, opcode=%d", agent->ra_sock->fd, opcode);

    if (opcode >= RHO_C_ARRAY_SIZE(bench_opcalls)) {
        rho_log_warn(bench_log, "bad opcode (%"PRIu32")", opcode);
        rpc_agent_new_msg(agent, ENOSYS);
        goto done;
    } 

    opcall = bench_opcalls[opcode];
    opcall(client);

done:
    rpc_agent_ready_send(agent);
    RHO_TRACE_EXIT();
    return;
}

static void
rpcserver_client_cb(struct rho_event *event, int what, struct rho_event_loop *loop)
{
    int ret = 0;
    struct rpcserver_client *client = NULL;
    struct rpc_agent *agent = NULL;

    RHO_ASSERT(event != NULL);
    RHO_ASSERT(event->userdata != NULL);
    RHO_ASSERT(loop != NULL);

    (void)what;

    client = event->userdata;
    agent = client->cli_agent;

    if (agent->ra_state == RPC_STATE_HANDSHAKE) {
        ret = rho_ssl_do_handshake(agent->ra_sock);
        if (ret == 0) {
            /* ssl handshake complete */
            agent->ra_state  = RPC_STATE_RECV_HDR;
            event->flags = RHO_EVENT_READ;
        } else if (ret == 1) {
            /* ssl handshake still in progress */
            event->flags = RHO_EVENT_READ;
            goto again;
        } else if (ret == 2) {
            /* ssl handshake still in progress: want_write */
            event->flags = RHO_EVENT_WRITE;
            goto again;
        } else {
            /* an error occurred during the handshake */
            agent->ra_state = RPC_STATE_ERROR; /* not needed */
            goto done;
        }
    }

    if (agent->ra_state == RPC_STATE_RECV_HDR)
        rpc_agent_recv_hdr(agent);

    if (agent->ra_state == RPC_STATE_RECV_BODY) 
        rpc_agent_recv_body(agent);

    if (agent->ra_state == RPC_STATE_DISPATCHABLE)
        rpcserver_client_dispatch_call(client);

    if (agent->ra_state == RPC_STATE_SEND_HDR)
        rpc_agent_send_hdr(agent);

    if (agent->ra_state == RPC_STATE_SEND_BODY)
        rpc_agent_send_body(agent);

    if ((agent->ra_state == RPC_STATE_ERROR) ||
            (agent->ra_state == RPC_STATE_CLOSED)) {
        goto done;
    }

again:
    rho_event_loop_add(loop, event, NULL); 
    return;

done:
    rho_log_info(bench_log, "client disconnect");
    rpcserver_client_destroy(client);
    return;
}

/**************************************
 * RPCSERVER
 **************************************/

static struct rpcserver *
rpcserver_alloc(void)
{
    struct rpcserver *server = NULL;
    server = rhoL_zalloc(sizeof(*server));
    return (server);
}

static void
rpcserver_destroy(struct rpcserver *server)
{
    int error = 0;

    if (server->srv_sock != NULL) {
        if (server->srv_udspath[0] != '\0') {
            error = unlink((const char *)server->srv_udspath);
            if (error != 0)
                rho_errno_warn(errno, "unlink('%s') failed", server->srv_udspath);
        }
        rho_sock_destroy(server->srv_sock);
    }

    rhoL_free(server);
}

static void
rpcserver_config_ssl(struct rpcserver *server,
        const char *cafile, const char *certfile, const char *keyfile)
{
    struct rho_ssl_params *params = NULL;
    struct rho_ssl_ctx *sc = NULL;

    RHO_TRACE_ENTER("cafile=%s, certfile=%s, keyfile=%s",
            cafile, certfile, keyfile);

    params = rho_ssl_params_create();
    rho_ssl_params_set_mode(params, RHO_SSL_MODE_SERVER);
    rho_ssl_params_set_protocol(params, RHO_SSL_PROTOCOL_TLSv1_2);
    rho_ssl_params_set_private_key_file(params, keyfile);
    rho_ssl_params_set_certificate_file(params, certfile);
    rho_ssl_params_set_ca_file(params, cafile);
    rho_ssl_params_set_verify(params, false);
    sc = rho_ssl_ctx_create(params);

    server->srv_sc = sc;

    /* TODO: destroy params? */

    RHO_TRACE_EXIT();
}

static void
rpcserver_socket_create(struct rpcserver *server, const char *url,
        bool anonymous)
{
    size_t pathlen = 0;
    struct rho_sock *sock = NULL;
    struct rho_url *purl = NULL;
    short port = 0;

    purl = rho_url_parse(url);
    if (purl == NULL)
        rho_die("invalid url \"%s\"", url);

    /* TODO: add rho_sock_server_create_from_url function */
    if (rho_str_equal(purl->scheme, "tcp")) {
        port = rho_str_toshort(purl->port, 10);
        sock = rho_sock_tcp4server_create(purl->host, port, 5);
        rhoL_setsockopt_disable_nagle(sock->fd);
    } else if (rho_str_equal(purl->scheme, "unix")) {
        pathlen = strlen(purl->path) + 1;
        if (anonymous) {
            strcpy((char *)(server->srv_udspath + 1), purl->path);
            pathlen += 1;
        } else {
            strcpy((char *)server->srv_udspath, purl->path);
        }
        sock = rho_sock_unixserver_create(server->srv_udspath, pathlen, 5);
    } else {
        rho_die("invalid url scheme \"%s\" (url=\"%s\")", purl->scheme, url);
    }

    rho_sock_setnonblocking(sock);
    server->srv_sock = sock;
}

static void
rpcserver_cb(struct rho_event *event, int what, struct rho_event_loop *loop)
{
    int cfd = 0;
    struct sockaddr_un addr;
    socklen_t addrlen = sizeof(addr);
    struct rho_event *cevent = NULL;
    struct rpcserver_client *client = NULL;
    struct rpcserver *server = NULL;
    struct rho_sock *csock = NULL;

    RHO_ASSERT(event != NULL);
    RHO_ASSERT(loop != NULL);
    RHO_ASSERT(event->userdata != NULL);
    server = event->userdata;

    (void)what;

    cfd = accept(event->fd, (struct sockaddr *)&addr, &addrlen);
    if (cfd == -1)
        rho_errno_die(errno, "accept failed");
    /* TODO: check that addrlen == sizeof struct soackaddr_un */

    csock = rho_sock_unix_from_fd(cfd);
    rho_sock_setnonblocking(csock);
    if (server->srv_sc != NULL)
        rho_ssl_wrap(csock, server->srv_sc);
    client = rpcserver_client_create(csock);
    rho_log_info(bench_log, "new connection");
    /* 
     * XXX: do we have a memory leak with event -- where does it get destroyed?
     */
    cevent = rho_event_create(cfd, RHO_EVENT_READ, rpcserver_client_cb, client);
    client->cli_agent->ra_event = cevent;
    rho_event_loop_add(loop, cevent, NULL); 
}

/**************************************
 * LOG
 **************************************/

static void
bench_log_init(const char *logfile, bool verbose)
{
    int fd = STDERR_FILENO;

    RHO_TRACE_ENTER();

    if (logfile != NULL) {
        fd = open(logfile, O_WRONLY|O_APPEND|O_CREAT,
                S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,S_IWOTH);
        if (fd == -1)
            rho_errno_die(errno, "can't open or creat logfile \"%s\"", logfile);
    }

    bench_log = rho_log_create(fd, RHO_LOG_INFO, rho_log_default_writer, NULL);

    if (verbose) 
        rho_log_set_level(bench_log, RHO_LOG_DEBUG);

    if (logfile != NULL) {
        rho_log_redirect_stderr(bench_log);
        (void)close(fd);
    }

    RHO_TRACE_EXIT();
}


/**************************************
 * RPCCLIENT
 **************************************/

static double
rpcclient_do_upload_bench(struct rpc_agent *agent)
{
    int i = 0;
    int error = 0;
    struct rho_buf *buf = agent->ra_bodybuf;
    struct timeval start;
    struct timeval end;
    struct timeval elapsed;

    (void)gettimeofday(&start, NULL);
    for (i = 0; i < g_bench_num_requests; i++) {
        rpc_agent_new_msg(agent, g_bench_op_code);
        rpc_agent_set_bodylen(agent, g_bench_payload_size);
        rho_buf_write(buf, g_bench_payload, g_bench_payload_size);

        error = rpc_agent_request(agent);
        if (error != 0)
            rho_die("rpc_agent_request returned %d", error);

        rho_debug("%d/%d status=%"PRIu32", download size=%"PRIu32,
                i, g_bench_num_requests,
                agent->ra_hdr.rh_code, agent->ra_hdr.rh_bodylen);
    }
    (void)gettimeofday(&end, NULL);

     rho_timeval_subtract(&end, &start, &elapsed);

    return (rho_timeval_to_sec_double(&elapsed) / (1.0 * g_bench_num_requests));
}

static double
rpcclient_do_download_bench(struct rpc_agent *agent)
{
    int i = 0;
    int error = 0;
    struct rho_buf *buf = agent->ra_bodybuf;
    uint32_t n = 0;
    struct timeval start;
    struct timeval end;
    struct timeval elapsed;

    (void)gettimeofday(&start, NULL);
    for (i = 0; i < g_bench_num_requests; i++) {
        rpc_agent_new_msg(agent, g_bench_op_code);

        error = rpc_agent_request(agent);
        if (error != 0)
            rho_die("rpc_agent_request returned %d", error);

        n = agent->ra_hdr.rh_bodylen;
        if (n > 0)
            rho_buf_read(buf, g_bench_payload, n);

        rho_debug("%d/%d status=%"PRIu32", download size=%"PRIu32,
                i, g_bench_num_requests, 
                agent->ra_hdr.rh_code, agent->ra_hdr.rh_bodylen);
    }
    (void)gettimeofday(&end, NULL);

    rho_timeval_subtract(&end, &start, &elapsed);

    return (rho_timeval_to_sec_double(&elapsed) / (1.0 * g_bench_num_requests));
}

static struct rpc_agent *
rpcclient_do_connect(const char *url, const char *root_crt_path)
{
    int error = 0;
    struct rho_sock *sock = NULL;
    struct rpc_agent *agent = NULL;
    struct rho_ssl_params *params = NULL;
    struct rho_ssl_ctx *ctx = NULL;

    fprintf(stderr, "rpcclient_do_connect: url=\"%s\", root_crt_path=\"%s\"\n",
            url, root_crt_path);

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

static void
rpcclient_main(const char *url, const char *root_crt)
{
    struct rpc_agent *agent = NULL;
    double mean = 0;

    agent = rpcclient_do_connect(url, root_crt);
    if (g_bench_op_code == BENCH_OP_UPLOAD) {
        rho_debug("doing %d upload requests", g_bench_num_requests);
        mean = rpcclient_do_upload_bench(agent);
        printf("mean time for a BENCH_OP_UPLOAD RPC of %"PRIu32" bytes (based on %d runs): %.9f s, (%.9g)\n",
            g_bench_payload_size, g_bench_num_requests, mean, mean);
    } else if (g_bench_op_code == BENCH_OP_DOWNLOAD) {
        rho_debug("doing %d download requests", g_bench_num_requests); 
        mean = rpcclient_do_download_bench(agent);
        printf("mean time for a BENCH_OP_DOWNLOAD RPC of %"PRIu32" bytes (based on %d runs): %.9f s, (%.9g)\n",
                    g_bench_payload_size, g_bench_num_requests, mean, mean);
    } else {
        RHO_ASSERT("invalid bench op code");
    }

   rpc_agent_destroy(agent);
   rhoL_free(g_bench_payload);
}

static void
usage(int exitcode)
{
    fprintf(stderr, "%s\n", RPCBENCH_USAGE);
    exit(exitcode);
}

int
main(int argc, char *argv[])
{
    int c = 0;
    struct rpcserver *server = NULL;
    struct rho_event *event = NULL;
    struct rho_event_loop *loop = NULL;
    /* options */
    bool anonymous = false;
    const char *logfile = NULL;
    bool verbose = false;
    pid_t pid;
    const char *root_crt = NULL;

    rho_ssl_init();

    server  = rpcserver_alloc();
    while ((c = getopt(argc, argv, "ac:dhl:vZ:")) != -1) {
        switch (c) {
        case 'a':
            anonymous = true;
            break;
        case 'c':
            if (rho_str_equal_ci(optarg, "UPLOAD")) {
                g_bench_op_code = BENCH_OP_UPLOAD;
            } else if (rho_str_equal_ci(optarg, "DOWNLOAD")) {
                g_bench_op_code = BENCH_OP_DOWNLOAD;
            } else {
                fprintf(stderr, "invalid option for -c \"%s\"", optarg);
                exit(1);
            }
            break;
        case 'h':
            usage(EXIT_SUCCESS);
            break;
        case 'l':
            logfile = optarg;
            break;
        case 'v':
            verbose = true;
            break;
        case 'Z':
            /* make sure there's three arguments */
            if ((argc - optind) < 2)
                usage(EXIT_FAILURE);
            root_crt = optarg;
            rpcserver_config_ssl(server, optarg, argv[optind],
                    argv[optind + 1]);
            optind += 2;
            break;
        default:
            usage(1);
        }
    }
    argc -= optind;
    argv += optind;

    if (argc != 3)
        usage(EXIT_FAILURE);

    g_bench_payload_size = rho_str_touint32(argv[1], 10);
    if (g_bench_payload_size > BENCH_MAX_PAYLOAD_SIZE) {
        fprintf(stderr, "download size must e less than %u",
                BENCH_MAX_PAYLOAD_SIZE);
        exit(1);
    }
    
    g_bench_num_requests = rho_str_toint(argv[2], 10);
    RHO_ASSERT(g_bench_num_requests > 0);

    pid = fork();
    if (pid > 0) {
        /* server */
        g_bench_payload = rhoL_zalloc(BENCH_MAX_PAYLOAD_SIZE);

        bench_log_init(logfile, verbose);

        rpcserver_socket_create(server, argv[0], anonymous);
        event = rho_event_create(server->srv_sock->fd,
                RHO_EVENT_READ | RHO_EVENT_PERSIST, 
                rpcserver_cb, server); 

        loop = rho_event_loop_create();
        rho_event_loop_add(loop, event, NULL); 
        rho_event_loop_dispatch(loop);

        rpcserver_destroy(server);
        rhoL_free(g_bench_payload);
    } else if (pid == 0) {
        /* child */
        sleep(5);
        g_bench_payload = rhoL_zalloc(BENCH_MAX_PAYLOAD_SIZE);
        rpcclient_main(argv[0], root_crt);
    } else {
        rho_errno_die(errno, "fork failed");
    }

    return (0);
}
