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
 * \brief  Phobos I/O adapters.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_cfg.h"
#include "pho_common.h"
#include "pho_io.h"
#include "pho_module_loader.h"

#define IO_BLOCK_SIZE_ATTR_KEY "io_block_size"

/**
 * List of configuration parameters for this module
 */
enum pho_cfg_params_io {
    /* Actual parameters */
    PHO_CFG_IO_io_block_size,

    /* Delimiters, update when modifying options */
    PHO_CFG_IO_FIRST = PHO_CFG_IO_io_block_size,
    PHO_CFG_IO_LAST  = PHO_CFG_IO_io_block_size,
};

const struct pho_config_item cfg_io[] = {
    [PHO_CFG_IO_io_block_size] = {
        .section = "io",
        .name    = IO_BLOCK_SIZE_ATTR_KEY,
        .value   = "0" /** default value = not set */
    },
};

int get_io_block_size(size_t *size)
{
    const char *string_io_block_size;
    int64_t sz;

    string_io_block_size = PHO_CFG_GET(cfg_io, PHO_CFG_IO, io_block_size);
    if (!string_io_block_size) {
        /* If not forced by configuration, the io adapter will retrieve it
         * from the backend storage system.
         */
        *size = 0;
        return 0;
    }

    sz = str2int64(string_io_block_size);
    if (sz < 0) {
        *size = 0;
        LOG_RETURN(-EINVAL, "Invalid value '%s' for parameter '%s'",
                   string_io_block_size, IO_BLOCK_SIZE_ATTR_KEY);
    }

    *size = sz;
    return 0;
}

void get_preferred_io_block_size(size_t *io_size,
                                 const struct io_adapter_module *ioa,
                                 struct pho_io_descr *iod)
{
    ssize_t sz;

    get_io_block_size(io_size);
    if (*io_size != 0)
        return;

    sz = ioa_preferred_io_size(ioa, iod);
    if (sz > 0) {
        *io_size = sz;
        return;
    }

    /* fallback: get the system page size */
    *io_size = sysconf(_SC_PAGESIZE);
}

/** retrieve IO functions for the given filesystem and addressing type */
int get_io_adapter(enum fs_type fstype, struct io_adapter_module **ioa)
{
    int rc = 0;

    switch (fstype) {
    case PHO_FS_POSIX:
        rc = load_module("io_adapter_posix", sizeof(**ioa), phobos_context(),
                         (void **)ioa);
        break;
    case PHO_FS_LTFS:
        rc = load_module("io_adapter_ltfs", sizeof(**ioa), phobos_context(),
                         (void **)ioa);
        break;
    case PHO_FS_RADOS:
        rc = load_module("io_adapter_rados", sizeof(**ioa), phobos_context(),
                         (void **)ioa);
        break;
    default:
        pho_error(-EINVAL, "Invalid FS type %#x", fstype);
        return -EINVAL;
    }

    return rc;
}

int copy_extent(struct io_adapter_module *ioa_source,
                struct pho_io_descr *iod_source,
                struct io_adapter_module *ioa_target,
                struct pho_io_descr *iod_target)
{
    size_t left_to_read;
    size_t buf_size;
    char *buffer;
    int rc2;
    int rc;

    /* retrieve the preferred IO size to allocate the buffer */
    get_preferred_io_block_size(&buf_size, ioa_target, iod_target);

    buffer = xcalloc(buf_size, sizeof(*buffer));

    /* open source IO descriptor then copy address to the target */
    rc = ioa_open(ioa_source, NULL, NULL, iod_source, false);
    if (rc)
        LOG_GOTO(memory, rc, "Unable to open source object");

    iod_target->iod_loc->addr_type = iod_source->iod_loc->addr_type;
    iod_target->iod_loc->extent->address.size =
        iod_source->iod_loc->extent->address.size;
    iod_target->iod_loc->extent->address.buff =
        xstrdup(iod_source->iod_loc->extent->address.buff);
    iod_target->iod_attrs = iod_source->iod_attrs;

    left_to_read = iod_source->iod_size;

    /* open target IO descriptor */
    rc = ioa_open(ioa_target, NULL, NULL, iod_target, true);
    if (rc)
        LOG_GOTO(close_source, rc, "Unable to open target object");

    /* do the actual copy */
    while (left_to_read) {
        size_t iter_size = buf_size < left_to_read ? buf_size : left_to_read;
        ssize_t nb_read_bytes;

        nb_read_bytes = ioa_read(ioa_source, iod_source, buffer, iter_size);
        if (nb_read_bytes < 0)
            LOG_GOTO(close, nb_read_bytes, "Unable to read %zu bytes",
                     iter_size);

        left_to_read -= nb_read_bytes;

        rc = ioa_write(ioa_target, iod_target, buffer, nb_read_bytes);
        if (rc != 0)
            LOG_GOTO(close, rc, "Unable to write %zu bytes", nb_read_bytes);
    }

close:
    rc2 = ioa_close(ioa_target, iod_target);
    if (rc)
        rc2 = ioa_del(ioa_target, iod_target);
    if (!rc && rc2)
        rc = rc2;

close_source:
    rc2 = ioa_close(ioa_source, iod_source);
    if (!rc && rc2)
        rc = rc2;

memory:
    free(buffer);

    return rc;
}

