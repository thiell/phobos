/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2023 CEA/DAM.
 *
 *  This file is part of Phobos.
 *
 *  Phobos is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  Phobos is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * \brief  TLC main interface -- Tape Library Controller
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "pho_cfg.h"
#include "pho_comm.h"
#include "pho_common.h"
#include "pho_daemon.h"
#include "pho_srl_lrs.h"

#include "tlc_cfg.h"

static bool should_tlc_stop(void)
{
    return !running;
}

struct tlc {
    struct pho_comm_info comm; /*!< Communication handle */
};

static int tlc_init(struct tlc *tlc)
{
    union pho_comm_addr sock_addr;
    int rc;

    sock_addr.tcp.hostname = PHO_CFG_GET(cfg_tlc, PHO_CFG_TLC, hostname);
    sock_addr.tcp.port = PHO_CFG_GET_INT(cfg_tlc, PHO_CFG_TLC, port, -1);
    if (sock_addr.tcp.port == -1)
        LOG_RETURN(-EINVAL, "Unable to get a valid integer TLC port value");

    if (sock_addr.tcp.port > 65535)
        LOG_RETURN(-EINVAL, "TLC port value %d cannot be greater than 65535",
                   sock_addr.tcp.port);

    rc = pho_comm_open(&tlc->comm, &sock_addr, PHO_COMM_TCP_SERVER);
    if (rc)
        LOG_RETURN(rc, "Error while opening the TLC socket");

    return rc;
}

static void tlc_fini(struct tlc *tlc)
{
    int rc;

    ENTRY;

    if (tlc == NULL)
        return;

    rc = pho_comm_close(&tlc->comm);
    if (rc)
        pho_error(rc, "Error on closing the TLC socket");
}

static void process_ping_request(struct tlc *tlc, pho_req_t *req,
                                 int client_socket)
{
    struct pho_comm_data msg;
    pho_resp_t resp;
    int rc;

    pho_srl_response_ping_alloc(&resp);
    resp.req_id = req->id;

    rc = pho_srl_response_pack(&resp, &msg.buf);
    if (rc)
        LOG_GOTO(out, rc, "TLC ping response cannot be packed");

    msg.fd = client_socket;
    rc = pho_comm_send(&msg);
    if (rc)
        pho_error(rc, "TLC error on sending ping response");

    free(msg.buf.buff);
out:
    pho_srl_response_free(&resp, false);
}

static int recv_work(struct tlc *tlc)
{
    struct pho_comm_data *data = NULL;
    int n_data;
    int rc, i;

    rc = pho_comm_recv(&tlc->comm, &data, &n_data);
    if (rc) {
        for (i = 0; i < n_data; ++i)
            free(data[i].buf.buff);

        free(data);
        LOG_RETURN(rc, "TLC error on reading input data");
    }

    for (i = 0; i < n_data; i++) {
        pho_req_t *req;

        if (data[i].buf.size == -1) /* close notification, ignore */
            continue;

        req = pho_srl_request_unpack(&data[i].buf);
        if (!req)
            continue;

        if (pho_request_is_ping(req)) {
            process_ping_request(tlc, req, data[i].fd);
            goto out_request;
        }

out_request:
        pho_srl_request_free(req, true);
    }

    return 0;
}

int main(int argc, char **argv)
{
    int write_pipe_from_child_to_father;
    struct daemon_params param;
    struct tlc tlc = {};
    int rc;

    rc = daemon_creation(argc, argv, &param, &write_pipe_from_child_to_father,
                         "tlc");
    if (rc)
        return -rc;

    rc = daemon_init(param);

    if (!rc)
        rc = tlc_init(&tlc);

    if (param.is_daemon)
        daemon_notify_init_done(write_pipe_from_child_to_father, &rc);

    if (rc)
        return -rc;

    while (true) {
        if (should_tlc_stop())
            break;

        /* recv_work waits on input sockets */
        rc = recv_work(&tlc);
        if (rc) {
            pho_error(rc, "TLC error when receiving requests");
            break;
        }
    }

    tlc_fini(&tlc);
    return EXIT_SUCCESS;
}
