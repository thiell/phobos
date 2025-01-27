/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
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
 * \brief  Application-friendly API to perform SCSI operations.
 */
#include "scsi_api.h"
#include "pho_common.h"
#include "pho_cfg.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <malloc.h>
#include <endian.h>
#include <assert.h>
#include <scsi/scsi.h>
#include <endian.h>
#include <unistd.h>

/* #define DEBUG 1 */

/* Some libraries don't support querying too much elements in a single
 * ELEMENT_STATUS request.
 * Start with no limit of chunks, and decrease later (starting from 256)
 * if the SCSI request fails.
 */
#define MAX_ELEMENT_STATUS_CHUNK 256

/** List of SCSI configuration parameters */
enum pho_cfg_params_scsi {
    /* SCSI common parameters */
    PHO_CFG_SCSI_retry_count, /**< Retry count for all SCSI requests */
    PHO_CFG_SCSI_retry_short, /**< Retry delay for EAGAIN */
    PHO_CFG_SCSI_retry_long,  /**< Retry delay for EBUSY */

    PHO_CFG_SCSI_max_element_status, /**< Max chunk size for
                                          ELEMENT_STATUS request. */
    PHO_CFG_SCSI_query_timeout_ms, /**< Timeout of a SCSI query request */
    PHO_CFG_SCSI_move_timeout_ms, /**< Timeout of a SCSI move request */
    PHO_CFG_SCSI_inquiry_timeout_ms, /**< Timeout of a SCSI inquiry request */

    /* Delimiters, update when modifying options */
    PHO_CFG_SCSI_FIRST = PHO_CFG_SCSI_retry_count,
    PHO_CFG_SCSI_LAST  = PHO_CFG_SCSI_move_timeout_ms,
};

#define _STR(X) #X
#define STR(X) _STR(X)
#define DEFAULT_QUERY_TIMEOUT_MS     1000 /* 1 s */
#define DEFAULT_MOVE_TIMEOUT_MS    300000 /* 5 min */
#define DEFAULT_INQUIRY_TIMEOUT_MS     10 /* 10 ms */

/** Definition and default values of SCSI configuration parameters */
const struct pho_config_item cfg_scsi[] = {
    [PHO_CFG_SCSI_retry_count] = {
        .section = "scsi",
        .name    = "retry_count",
        .value   = "5",
    },
    [PHO_CFG_SCSI_retry_short] = {
        .section = "scsi",
        .name    = "retry_short",
        .value   = "1",
    },
    [PHO_CFG_SCSI_retry_long] = {
        .section = "scsi",
        .name    = "retry_long",
        .value   = "5",
    },
    [PHO_CFG_SCSI_max_element_status] = {
        .section = "scsi",
        .name    = "max_element_status",
        .value   = "0", /* unlimited */
    },
    [PHO_CFG_SCSI_query_timeout_ms] = {
        .section = "scsi",
        .name    = "query_timeout_ms",
        .value   = STR(DEFAULT_QUERY_TIMEOUT_MS),
    },
    [PHO_CFG_SCSI_move_timeout_ms] = {
        .section = "scsi",
        .name    = "move_timeout_ms",
        .value   = STR(DEFAULT_MOVE_TIMEOUT_MS),
    },
    [PHO_CFG_SCSI_inquiry_timeout_ms] = {
        .section = "scsi",
        .name    = "inquiry_timeout_ms",
        .value   = STR(DEFAULT_INQUIRY_TIMEOUT_MS),
    },
};

/** Return retry count (get it once) */
static int scsi_retry_count(void)
{
    static int retry_count = -1;

    if (retry_count != -1)
        return retry_count;

    /* fallback to no-retry (0) on failure */
    retry_count = PHO_CFG_GET_INT(cfg_scsi, PHO_CFG_SCSI, retry_count, 0);

    return retry_count;
}

/** Return short retry delay (get it once) */
static int scsi_retry_short(void)
{
    static int short_retry_delay = -1;

    if (short_retry_delay != -1)
        return short_retry_delay;

    /* fallback to 1s on failure */
    short_retry_delay = PHO_CFG_GET_INT(cfg_scsi, PHO_CFG_SCSI, retry_short, 1);

    return short_retry_delay;
}

/** Return long retry delay (get it once) */
static int scsi_retry_long(void)
{
    static int long_retry_delay = -1;

    if (long_retry_delay != -1)
        return long_retry_delay;

    /* fallback to 5s on failure */
    long_retry_delay = PHO_CFG_GET_INT(cfg_scsi, PHO_CFG_SCSI, retry_long, 5);

    return long_retry_delay;
}

/** Return query timeout ms (get it once) */
static int scsi_query_timeout_ms(void)
{
    static int query_timeout_ms = -1;

    if (query_timeout_ms != -1)
        return query_timeout_ms;

    query_timeout_ms = PHO_CFG_GET_INT(cfg_scsi, PHO_CFG_SCSI, query_timeout_ms,
                                       DEFAULT_QUERY_TIMEOUT_MS);

    return query_timeout_ms;
}

/** Return move timeout ms (get it once) */
static int scsi_move_timeout_ms(void)
{
    static int move_timeout_ms = -1;

    if (move_timeout_ms != -1)
        return move_timeout_ms;

    /* fallback to 1 s on failure */
    move_timeout_ms = PHO_CFG_GET_INT(cfg_scsi, PHO_CFG_SCSI, move_timeout_ms,
                                      DEFAULT_MOVE_TIMEOUT_MS);

    return move_timeout_ms;
}

/** Return inquiry timeout ms (get it once) */
static int scsi_inquiry_timeout_ms(void)
{
    static int inquiry_timeout_ms = -1;

    if (inquiry_timeout_ms != -1)
        return inquiry_timeout_ms;

    inquiry_timeout_ms = PHO_CFG_GET_INT(cfg_scsi, PHO_CFG_SCSI,
                                         inquiry_timeout_ms,
                                         DEFAULT_INQUIRY_TIMEOUT_MS);

    return inquiry_timeout_ms;
}

int scsi_mode_sense(int fd, struct mode_sense_info *info, json_t *message)
{
    struct mode_sense_result_EAAP *res_element_addr;
    unsigned char buffer[MODE_SENSE_BUFF_LEN] = "";
    struct mode_sense_result_header *res_hdr;
    struct scsi_req_sense error = {0};
    struct scsi_error scsi_err = {0};
    struct mode_sense_cdb req = {0};
    json_t *log_object;
    int rc;

    if (!info)
        return -EINVAL;

    pho_debug("scsi_execute: MODE_SENSE, buffer_len=%u", MODE_SENSE_BUFF_LEN);

    log_object = json_object();

    json_insert_element(log_object, "SCSI action", json_string("MODE_SENSE"));

    req.opcode = MODE_SENSE;
    req.dbd = 1;            /* disable block descriptors */
    req.page_code = PAGECODE_ELEMENT_ADDRESS;
    req.page_control = 0;   /* last/current */
    req.allocation_length = MODE_SENSE_BUFF_LEN;
    /* all other fields are zeroed */

    PHO_RETRY_LOOP(rc, scsi_retry_func, &scsi_err, scsi_retry_count(),
                   scsi_execute, fd, SCSI_GET, (unsigned char *)&req,
                   sizeof(req), &error, sizeof(error), buffer, sizeof(buffer),
                   scsi_query_timeout_ms(), log_object);

    if (rc) {
        /* log_object is a json_object here, so it cannot be used with the macro
         * since we would have to create a json_object containing a json_object,
         * so just use json_object_set_new directly.
         */
        json_object_set_new(message, "scsi_execute", log_object);

        return rc;
    }

    destroy_json(log_object);

    res_hdr = (struct mode_sense_result_header *)buffer;
    if (res_hdr->mode_data_length < sizeof(struct mode_sense_result_header) +
                                    sizeof(struct mode_sense_result_EAAP) - 1)
        LOG_RETURN(-EIO, "Unexpected result size %u < %lu",
                   res_hdr->mode_data_length,
                   sizeof(struct mode_sense_result_header) +
                   sizeof(struct mode_sense_result_EAAP) - 1);

    res_element_addr = (struct mode_sense_result_EAAP *)((ptrdiff_t)res_hdr
                        + sizeof(struct mode_sense_result_header));
    if (res_element_addr->page_code != PAGECODE_ELEMENT_ADDRESS)
        LOG_RETURN(-EIO, "Invalid page_code %#x != %#x",
                   res_element_addr->page_code, PAGECODE_ELEMENT_ADDRESS);

    info->arms.first_addr
        = be16toh(res_element_addr->first_medium_transport_elt_addr);
    info->arms.nb = be16toh(res_element_addr->medium_transport_elt_nb);

    info->slots.first_addr
        = be16toh(res_element_addr->first_storage_elt_addr);
    info->slots.nb = be16toh(res_element_addr->storage_elt_nb);

    info->impexp.first_addr = be16toh(res_element_addr->first_ie_elt_addr);
    info->impexp.nb = be16toh(res_element_addr->ie_elt_nb);

    info->drives.first_addr
        = be16toh(res_element_addr->first_data_transfer_elt_addr);
    info->drives.nb = be16toh(res_element_addr->data_transfer_elt_nb);

    return 0;
}

/** convert an array of 3 bytes (big endian 24 bits)
 * to a 32bits little endian */
static inline uint32_t be24toh(uint8_t *a)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
        return ((uint32_t)(a[0]) << 16) + ((uint32_t)(a[1]) << 8)
               + (uint32_t)a[2];
#else
    #error  "Only little endian architectures are currently supported"
#endif
}

/** convert a 32bits little endian to an array of 3 bytes
 * (big endian 24 bits).
 */
static inline void htobe24(uint32_t h, uint8_t *be)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
        be[0] = (h >> 16) & 0xFF;
        be[1] = (h >> 8) & 0xFF;
        be[2] = h & 0xFF;
#else
    #error  "Only little endian architectures are currently supported"
#endif
}

/**
 * Read the next element from READ_ELEMENT_STATUS reply.
 * @param[in]     elmt          Pointer to the next element status.
 * @param[in]     page          Header of element status list.
 * @param[in,out] elem_out      Element status information to be filled in.
 * @return The number of bytes read, or < 0 on error.
 */
static int read_next_element_status(const struct element_descriptor *elmt,
                                    const struct element_status_page *page,
                                    struct element_status *elem_out)
{
    elem_out->type = page->type_code;
    elem_out->address = be16toh(elmt->address);

    elem_out->full = elmt->full;
    elem_out->impexp = elmt->impexp;
    elem_out->accessible = elmt->access;
    elem_out->exp_enabled = elmt->exp_enabled;
    elem_out->imp_enabled = elmt->imp_enabled;
    elem_out->invert = elmt->invert;

    elem_out->except = elmt->except;
    elem_out->error_code = elmt->asc;
    elem_out->error_code_qualifier = elmt->ascq;

    if (elmt->svalid) {
        elem_out->src_addr_is_set = true;
        elem_out->src_addr = be16toh(elmt->ssea);
    }

    if (page->pvoltag) {
        strncpy(elem_out->vol, elmt->pvti, VOL_ID_LEN);
        elem_out->vol[VOL_ID_LEN - 1] = '\0';
        rstrip(elem_out->vol);
    }

    if (elem_out->type == SCSI_TYPE_DRIVE) {
        const struct dev_i *dev_info = &elmt->alt_info.dev;
        int id_len;

        if (!page->pvoltag) {
            /* if pvoltag is not set, response is shifted by 36 bytes */
            dev_info = (const struct dev_i *)
                            ((ptrdiff_t)&elmt->alt_info.dev - 36);
        }

        /* id length (host endianess) */
        id_len = dev_info->id_len;

        /* ensure room for final '\0' */
        if (id_len >= DEV_ID_LEN)
            id_len = DEV_ID_LEN - 1;

        if (id_len > 0) {
            strncpy(elem_out->dev_id, dev_info->devid, id_len);
            elem_out->dev_id[id_len] = '\0';
            rstrip(elem_out->dev_id);
        }
    }

    if (elem_out->type == SCSI_TYPE_DRIVE) {
        pho_debug("scsi_type: %d, addr: %#hx, %s, id='%s'", elem_out->type,
                  elem_out->address, elem_out->full ? "full" : "empty",
                  elem_out->dev_id);
    } else {
        pho_debug("scsi_type: %d, addr: %#hx, %s, vol='%s'", elem_out->type,
                  elem_out->address, elem_out->full ? "full" : "empty",
                  elem_out->vol);
    }

    return be16toh(page->ed_len);
}

/**
 * Perform the SCSI element status request and decode the returned elements.
 * @param elmt_list     Allocated element list.
 * @param elmt_count    Updated by this call.
 */
static int _scsi_element_status(int fd, enum element_type_code type,
                                uint16_t start_addr, uint16_t nb,
                                enum elem_status_flags flags,
                                struct element_status *elmt_list,
                                int *elmt_count,
                                json_t *message)
{
    struct element_status_header *res_hdr;
    struct scsi_req_sense error = {0};
    struct scsi_error scsi_err = {0};
    struct read_status_cdb req = {0};
    unsigned char *buffer = NULL;
    unsigned char *curr;
    int curr_index;
    int byte_count;
    int len = 0;
    int rc, i;
    int count;

    assert(elmt_list != NULL);
    assert(elmt_count != NULL);

    /* length to be allocated for the result buffer */
    len = sizeof(struct element_status_header)
          + nb * sizeof(struct element_status_page)
          + nb * READ_STATUS_MAX_ELT_LEN;

    buffer = calloc(1, len);
    if (!buffer)
        return -ENOMEM;

    pho_debug("scsi_execute: READ_ELEMENT_STATUS, type=%#x, start_addr=%#hx, "
              "count=%hu, buffer_len=%u", type, start_addr, nb, len);

    req.opcode = READ_ELEMENT_STATUS;
    req.voltag = !!(flags & ESF_GET_LABEL); /* return volume bar-code */
    req.element_type_code = type;
    req.starting_address = htobe16(start_addr);
    req.elements_nb = htobe16(nb);
    req.curdata = !!(flags & ESF_ALLOW_MOTION); /* allow moving arms */
    req.dvcid = !!(flags & ESF_GET_DRV_ID); /* query device identifier */
    htobe24(len, req.alloc_length);

    PHO_RETRY_LOOP(rc, scsi_retry_func, &scsi_err, scsi_retry_count(),
                   scsi_execute, fd, SCSI_GET, (unsigned char *)&req,
                   sizeof(req), &error, sizeof(error), buffer, len,
                   scsi_query_timeout_ms(), message);

    if (rc)
        goto free_buff;

    /* pointer to result header */
    res_hdr = (struct element_status_header *)buffer;

#ifdef DEBUG
    pho_debug("%hu elements returned (%d bytes/%u buff len)\n",
              be16toh(res_hdr->elements_nb), be24toh(res_hdr->byte_count), len);
#endif

    /* pointer to the first element */
    curr = (unsigned char *)res_hdr + sizeof(struct element_status_header);

    /* number of elements returned */
    count = be16toh(res_hdr->elements_nb);

    /* number of bytes returned */
    byte_count = be24toh(res_hdr->byte_count);

    curr_index = 0;

    for (i = 0; i < count && byte_count >= sizeof(struct element_status_page);
         i++) {
        /* current element page */
        struct element_status_page *page = (struct element_status_page *)curr;

        curr += sizeof(struct element_status_page);
        byte_count -=  sizeof(struct element_status_page);

#ifdef DEBUG
        pho_debug("        type=%hhu, vol=%u, avol=%u, descriptor_len=%hu, "
               "byte_count=%u\n", page->type_code, !!page->pvoltag,
               !!page->avoltag, be16toh(page->ed_len),
               be24toh(page->byte_count));
#endif

        while (byte_count > 0) {
            rc = read_next_element_status((struct element_descriptor *)curr,
                                          page, &(elmt_list[curr_index]));
            if (rc < 0)
                goto free_buff;

            curr_index++;
            byte_count -= rc;
            curr += rc;
#ifdef DEBUG
            pho_debug("%d bytes left\n", byte_count);
#endif
        }
    }

    (*elmt_count) += curr_index;
    rc = 0;

free_buff:
    free(buffer);

    return rc;
}

int scsi_element_status(int fd, enum element_type_code type,
                        uint16_t start_addr, uint16_t nb,
                        enum elem_status_flags flags,
                        struct element_status **elmt_list, int *elmt_count,
                        json_t *message)
{
    static int max_element_status_chunk = -1;
    uint16_t req_size = nb;
    json_t *log_object;
    char info[16];
    int rc;

    *elmt_count = 0;

    log_object = json_object();

    json_insert_element(log_object, "SCSI action",
                        json_string("READ_ELEMENT_STATUS"));

    /* Use an intermediary buffer to keep the relevant information for logs */
    sprintf(info, "%#x", type);
    json_insert_element(log_object, "Type", json_string(info));
    json_insert_element(log_object, "Count", json_integer(nb));

    /* check if there is a configured limitation */
    if (max_element_status_chunk == -1) {
        int val = PHO_CFG_GET_INT(cfg_scsi, PHO_CFG_SCSI, max_element_status,
                                  1);
        if (val > 0)
            max_element_status_chunk = val;
    }

    if (max_element_status_chunk != -1 && req_size > max_element_status_chunk)
        req_size = max_element_status_chunk;

    /* allocate the element list according to the requested count */
    *elmt_list = calloc(nb, sizeof(struct element_status));
    if (*elmt_list == NULL)
        return -ENOMEM;

    /* handle limitation of ELEMENT_STATUS request size:
     * Start with nb, then try with smaller chunks in case of error. */
    do {
        rc = _scsi_element_status(fd, type, start_addr, req_size, flags,
                                  *elmt_list, elmt_count, log_object);
        if (rc == 0) {
            if (*elmt_count < req_size)
                /* end reached */
                goto out_log;
            else
                /* read next chunks */
                break;
        }

        if (max_element_status_chunk == -1) {
            /* try with the power of 2 <= nb */
            max_element_status_chunk = MAX_ELEMENT_STATUS_CHUNK;
            while (max_element_status_chunk > req_size)
                max_element_status_chunk /= 2;
            pho_debug("Request failed for %u elements, reducing request size to %u",
                      req_size, max_element_status_chunk);
            req_size = max_element_status_chunk;
            continue;
        }

        if (max_element_status_chunk > 1) {
            /* try even smaller */
            max_element_status_chunk /= 2;
            pho_debug("Request failed for %u elements, reducing request size to %u",
                      req_size, max_element_status_chunk);
            req_size = max_element_status_chunk;
            continue;
        }

        /* return the error */
        goto out_log;
    } while (1);

    while (*elmt_count < nb) {
        rc = _scsi_element_status(fd, type, start_addr + *elmt_count, req_size,
                                  flags, (*elmt_list) + *elmt_count,
                                  elmt_count, log_object);
        if (rc)
            goto out_log;
    }

    pho_debug("Read %u elements out of %u", *elmt_count, nb);

out_log:
    if (rc)
        json_object_set_new(message, "scsi_execute", log_object);
    else
        destroy_json(log_object);

    return rc;
}

void element_status_list_free(struct element_status *elmt_list)
{
    free(elmt_list);
}

static void fill_json_message(uint16_t arm_addr, uint16_t src_addr,
                              uint16_t tgt_addr, json_t *log_object)
{
    char address[16];

    json_insert_element(log_object, "SCSI action", json_string("MOVE_MEDIUM"));

    /* Use an intermediary buffer to keep the hexadecimal format for
     * addresses
     */
    sprintf(address, "%#hx", arm_addr);
    json_insert_element(log_object, "Arm address", json_string(address));
    sprintf(address, "%#hx", src_addr);
    json_insert_element(log_object, "Source address", json_string(address));
    sprintf(address, "%#hx", tgt_addr);
    json_insert_element(log_object, "Target address", json_string(address));

}

int scsi_move_medium(int fd, uint16_t arm_addr, uint16_t src_addr,
                     uint16_t tgt_addr, json_t *message)
{
    json_t *log_object = json_object();
    struct scsi_req_sense error = {0};
    struct move_medium_cdb req = {0};
    struct scsi_error scsi_err = {0};
    int rc;

    pho_debug("scsi_execute: MOVE_MEDIUM, arm_addr=%#hx, src_addr=%#hx, "
              "tgt_addr=%#hx", arm_addr, src_addr, tgt_addr);

    fill_json_message(arm_addr, src_addr, tgt_addr, log_object);

    req.opcode = MOVE_MEDIUM;
    req.transport_element_address = htobe16(arm_addr);
    req.source_address = htobe16(src_addr);
    req.destination_address = htobe16(tgt_addr);

    PHO_RETRY_LOOP(rc, scsi_retry_func, &scsi_err, scsi_retry_count(),
                   scsi_execute, fd, SCSI_GET, (unsigned char *)&req,
                   sizeof(req), &error, sizeof(error), NULL, 0,
                   scsi_move_timeout_ms(), log_object);

    json_object_set_new(message, "scsi_execute", log_object);

    return rc;
}

int scsi_inquiry(int fd)
{
    struct standard_inquiry_data_page inquiry_response;
    struct scsi_req_sense error = {0};
    struct scsi_error scsi_err = {0};
    struct inquiry_cdb req = {0};
    int rc;

    pho_debug("scsi_execute: INQUIRY");

    req.opcode = INQUIRY;
    req.allocation_length = 36;

    PHO_RETRY_LOOP(rc, scsi_retry_func, &scsi_err, scsi_retry_count(),
                   scsi_execute, fd, SCSI_GET, (unsigned char *)&req,
                   sizeof(req), &error, sizeof(error), &inquiry_response,
                   sizeof(inquiry_response), scsi_inquiry_timeout_ms(), NULL);

    return rc;
}

/** Indicate whether a SCSI error must be retried after a delay */
static inline bool scsi_delayed_retry(int rc)
{
    return rc == -EBUSY || rc == -EIO || rc == -ETIMEDOUT;
}

/** Indicate whether a SCSI error can be retried immediatly */
static inline bool scsi_immediate_retry(int rc)
{
    return rc == -EAGAIN || rc == -EINTR;
}

void scsi_retry_func(const char *fnname, int rc, int *retry_cnt,
                     struct scsi_error *err)
{
    int delay = 0;

    (*retry_cnt)--;
    if ((*retry_cnt) < 0) {
        if (rc)
            pho_error(rc, "%s: all retries failed.", fnname);
        return;
    }

    switch (err->status) {
    case SCSI_FATAL_ERROR:
        /* non-retriable error: exit retry loop */
        pho_error(err->rc, "%s failed.", fnname);
        *retry_cnt = -1;
        break;
    case SCSI_SUCCESS:
        /* success: exit retry loop */
        *retry_cnt = -1;
        break;
    case SCSI_RETRY_SHORT:
        /* short retry */
        delay = scsi_retry_short();
        break;
    case SCSI_RETRY_LONG:
        /* longer retry delay */
        delay = scsi_retry_long();
        break;
    }

    if (delay) {
        pho_error(err->rc, "%s failed: retry in %d sec...", fnname, delay);
        sleep(delay);
    }
}
