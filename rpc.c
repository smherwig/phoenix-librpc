#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <rho/rho_buf.h>
#include <rho/rho_event.h>
#include <rho/rho_log.h>
#include <rho/rho_mem.h>
#include <rho/rho_sock.h>

#include "rpc.h"

/*********************************************************
 * SERIALIZING/DESERIALIZING HEADER
 *********************************************************/
static void
rpc_agent_pack_hdr(struct rpc_agent *agent)
{
    struct rho_buf *buf = agent->ra_hdrbuf;

    rho_buf_rewind(buf);
    rho_buf_writeu32be(buf, agent->ra_hdr.rh_code);
    rho_buf_writeu32be(buf, agent->ra_hdr.rh_bodylen);
    rho_buf_rewind(buf);
}

static int 
rpc_agent_unpack_hdr(struct rpc_agent *agent)
{
    int error = 0;
    struct rho_buf *buf = agent->ra_hdrbuf;
    uint32_t code = 0;
    uint32_t bodylen = 0;

    RHO_TRACE_ENTER();

    if (rho_buf_length(buf) != RPC_HDR_LENGTH) {
        rho_warn("rho_buf_length(buf)=%zu != RPC_HDR_LENGTH",
                rho_buf_length(buf));
        error = EPROTO;
        goto out;
    }

    rho_buf_rewind(buf);

    error = rho_buf_readu32be(buf, &code);
    if (error == -1) {
        /* tried to read past end of buffer */
        error = EPROTO; /* XXX: would ERMOTEIO be a better choice? */
        goto out;
    }

    error = rho_buf_readu32be(buf, &bodylen);
    if (error == -1) {
        /* tried to read past end of buffer */
        error = EPROTO; /* XXX: would ERMOTEIO be a better choice? */
        goto out;
    }

    agent->ra_hdr.rh_code = code;
    agent->ra_hdr.rh_bodylen = bodylen;
    rho_buf_clear(buf);

    rho_debug("rh_code=%"PRIu32", rh_bodylen=%"PRIu32,
            agent->ra_hdr.rh_code, agent->ra_hdr.rh_bodylen);

out:
    RHO_TRACE_EXIT();
    return (error);
}

/*********************************************************
 * STATE CHANGE HELPERS
 *********************************************************/
const char *
rpc_state_to_str(int state)
{
    const char *s = NULL;
    switch (state) {
    case RPC_STATE_HANDSHAKE:
        s = "handshake";
        break;
    case RPC_STATE_RECV_HDR:
        s = "recv_hdr";
        break;
    case RPC_STATE_RECV_BODY:
        s = "recv_body";
        break;
    case RPC_STATE_DISPATCHABLE:
        s = "dispatchable";
        break;
    case RPC_STATE_SEND_HDR:
        s = "send_hdr";
        break;
    case RPC_STATE_SEND_BODY:
        s = "send_body";
        break;
    case RPC_STATE_CLOSED:
        s = "closed";
        break;
    case RPC_STATE_ERROR:
        s = "error";
        break;
    default:
        rho_die("unknown rpc agent state: %d", state);
    }

    return (s);
}

static void
rpc_agent_set_dispatchable(struct rpc_agent *agent)
{
    agent->ra_state = RPC_STATE_DISPATCHABLE;
    /* buf is at the start of body */
    rho_buf_rewind(agent->ra_bodybuf);
}

void
rpc_agent_ready_send(struct rpc_agent *agent)
{
    RHO_ASSERT(rho_buf_length(agent->ra_bodybuf) == agent->ra_hdr.rh_bodylen);

    rpc_agent_pack_hdr(agent);
    rho_buf_rewind(agent->ra_bodybuf);
    agent->ra_state = RPC_STATE_SEND_HDR;
}

/*********************************************************
 * CONSTRUCTOR / DESTRUCTOR
 *********************************************************/
struct rpc_agent *
rpc_agent_create(struct rho_sock *sock, struct rho_event *event)
{
    struct rpc_agent *agent = NULL;

    RHO_TRACE_ENTER();

    agent = rhoL_zalloc(sizeof(*agent));
    agent->ra_state = RPC_STATE_HANDSHAKE;
    agent->ra_hdrbuf = rho_buf_bounded_create(RPC_HDR_LENGTH);
    agent->ra_bodybuf = rho_buf_create();
    agent->ra_event = event;
    agent->ra_sock = sock;

    RHO_TRACE_EXIT();
    return (agent);
}

void
rpc_agent_destroy(struct rpc_agent *agent)
{
    RHO_TRACE_ENTER();

    rho_buf_destroy(agent->ra_hdrbuf);
    rho_buf_destroy(agent->ra_bodybuf);
    if (agent->ra_sock != NULL)
        rho_sock_destroy(agent->ra_sock);
    rhoL_free(agent);

    RHO_TRACE_EXIT();
}

/*********************************************************
 * NEW, EMPTY MESSAGE
 *********************************************************/
void
rpc_agent_new_msg(struct rpc_agent *agent, uint32_t code)
{
    rho_buf_clear(agent->ra_hdrbuf);
    rho_memzero(&agent->ra_hdr, sizeof(agent->ra_hdr));
    rho_buf_clear(agent->ra_bodybuf);
    
    agent->ra_hdr.rh_code = code;
}

/*********************************************************
 * CORE EVENT-LOOP METHODS
 *********************************************************/
void
rpc_agent_recv_hdr(struct rpc_agent *agent)
{
    struct rho_buf *buf = agent->ra_hdrbuf;
    struct rho_sock *sock = agent->ra_sock;
    size_t need = 0;
    ssize_t got = 0;

    RHO_ASSERT(agent->ra_state == RPC_STATE_RECV_HDR);
    RHO_ASSERT(rho_buf_length(buf) < RPC_HDR_LENGTH);

    RHO_TRACE_ENTER();

    need = RPC_HDR_LENGTH - rho_buf_length(buf);
    RHO_ASSERT(need != 0);
    got = rho_sock_recv_buf(sock, buf, need);

    if (got == -1) {
        if (errno != EAGAIN) {
            agent->ra_state = RPC_STATE_ERROR;
            rho_errno_warn(errno, "rho_sock_recv_buf(sock->fd=%d) failed",
                    sock->fd);
        }
    } else if (got == 0) {
        agent->ra_state = RPC_STATE_CLOSED;
    } else if ((size_t)got == need) {
        (void)rpc_agent_unpack_hdr(agent);
        rho_debug("bodylen: %"PRIu32, rpc_agent_get_bodylen(agent));
        if (rpc_agent_get_bodylen(agent) > 0) {
            agent->ra_state = RPC_STATE_RECV_BODY;
        } else {
            agent->ra_event->flags = RHO_EVENT_WRITE;
            rpc_agent_set_dispatchable(agent);
        }
    }

    RHO_TRACE_EXIT("need=%zu, got=%zd, state=%s",
            need, got, rpc_state_to_str(agent->ra_state));
}

void
rpc_agent_recv_body(struct rpc_agent *agent)
{
    struct rho_buf *buf = agent->ra_bodybuf;
    struct rho_sock *sock = agent->ra_sock;
    size_t need = 0;
    ssize_t got = 0;

    RHO_ASSERT(agent->ra_state == RPC_STATE_RECV_BODY);

    RHO_TRACE_ENTER();

    /* 
     * XXX: is this necessary -- should we even be in this function
     * if this condition is met?
     */
    need = agent->ra_hdr.rh_bodylen - rho_buf_length(buf);
    RHO_ASSERT(need != 0);
#if 0
    if (need == 0) {
        agent->ra_event->flags = RHO_EVENT_WRITE;
        rpc_agent_set_dispatchable(agent);
        goto done;
    }
#endif

    got = rho_sock_recv_buf(sock, buf, need);
    if (got == -1) {
        if (errno != EAGAIN) {
            agent->ra_state = RPC_STATE_ERROR;
            rho_errno_warn(errno, "rho_sock_recv_buf(sock->fd=%d) failed",
                    sock->fd);
        }
    } else if (got == 0) {
        agent->ra_state = RPC_STATE_CLOSED;
    } else if ((size_t)got == need) {
        agent->ra_event->flags = RHO_EVENT_WRITE;
        rpc_agent_set_dispatchable(agent);
    }

    RHO_TRACE_EXIT("need=%zu, got=%zd, state=%s", need, got, 
            rpc_state_to_str(agent->ra_state));
}

void
rpc_agent_send_hdr(struct rpc_agent *agent)
{
    struct rho_sock *sock = agent->ra_sock;
    struct rho_buf *buf = agent->ra_hdrbuf;
    size_t left = 0;
    ssize_t nput = 0;

    RHO_ASSERT(agent->ra_state == RPC_STATE_SEND_HDR);

    RHO_TRACE_ENTER();

    left = rho_buf_left(buf);
    nput = rho_sock_send_buf(sock, buf, left);

    if (nput == -1) {
        if (errno != EAGAIN) {
            agent->ra_state = RPC_STATE_ERROR;
            rho_errno_warn(errno, "rho_sock_send_hdr(sock->fd=%d) failed",
                    sock->fd);
        }
    } else if ((size_t)nput == left) {
        if (agent->ra_hdr.rh_bodylen > 0) {
            agent->ra_state = RPC_STATE_SEND_BODY;
            agent->ra_event->flags = RHO_EVENT_WRITE;
        } else {
            agent->ra_state = RPC_STATE_RECV_HDR;
            agent->ra_event->flags = RHO_EVENT_READ;
        }
        
        rho_buf_clear(agent->ra_hdrbuf);
        rho_memzero(&agent->ra_hdr, sizeof(agent->ra_hdr));
    }

    RHO_TRACE_EXIT();
}

void
rpc_agent_send_body(struct rpc_agent *agent)
{
    struct rho_sock *sock = agent->ra_sock;
    struct rho_buf *buf = agent->ra_bodybuf;
    size_t left = 0;
    ssize_t nput = 0;

    RHO_ASSERT(agent->ra_state == RPC_STATE_SEND_BODY);

    RHO_TRACE_ENTER();

    left = rho_buf_left(buf);
    nput = rho_sock_send_buf(sock, buf, left);

    if (nput == -1) {
        if (errno != EAGAIN) {
            agent->ra_state = RPC_STATE_ERROR;
            rho_errno_warn(errno, "rho_sock_send_body(sock->fd=%d) failed",
                    sock->fd);
        }
    } else if ((size_t)nput == left) {
        agent->ra_state = RPC_STATE_RECV_HDR;
        agent->ra_event->flags = RHO_EVENT_READ;
        rho_buf_clear(agent->ra_bodybuf);
    }

    RHO_TRACE_EXIT();
}

/*********************************************************
 * SIMPLE, SERIAL INTERFACE (e.g., NON EVENT-LOOP)
 *********************************************************/
/* 
 * on success, ra_bodybuf is the response body 
 * TODO: what does this function return?
 */
int
rpc_agent_request(struct rpc_agent *agent)
{
    int error = 0;
    ssize_t n = 0;
    struct rho_sock *sock = agent->ra_sock;
    struct rho_buf *hdrbuf = agent->ra_hdrbuf;
    struct rho_buf *bodybuf = agent->ra_bodybuf;
    struct rpc_hdr *hdr = &agent->ra_hdr;

    RHO_TRACE_ENTER();

    rpc_agent_ready_send(agent);

    n = rho_sock_sendn_buf(sock, hdrbuf, rho_buf_length(hdrbuf));
    if (n == -1) {
        error = -1;
        goto done;
    }

    n = rho_sock_sendn_buf(sock, bodybuf, rho_buf_length(bodybuf));
    if (n == -1) {
        error = -1;
        goto done;
    }

    rho_buf_clear(hdrbuf);
    rho_buf_clear(bodybuf);

    n = rho_sock_precvn_buf(sock, hdrbuf, RPC_HDR_LENGTH);
    rho_debug("rho_sock_prcevn_buf for the hdr returned %zd", n);
    if (n == -1) {
        error = -1;
        goto done;
    }

    (void)rpc_agent_unpack_hdr(agent);
    if (hdr->rh_bodylen > 0) {
        rho_debug("response code=%"PRIu32", bodylen=%"PRIu32, hdr->rh_code,
                hdr->rh_bodylen);
        rho_debug("receiving rpc body");
        n = rho_sock_precvn_buf(sock, bodybuf, hdr->rh_bodylen);
        if (n == -1) {
            error = -1;
            goto done;
        }
    }

done:
    RHO_TRACE_EXIT();
    return (error);
}
