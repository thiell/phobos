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
 * \brief  Phobos Local Device Manager.
 *
 * This modules implements low level device control on local host.
 */
#ifndef _PHO_LDM_H
#define _PHO_LDM_H

#include "pho_common.h"
#include "pho_types.h"
#include <assert.h>
#include <jansson.h>

/**
 * \defgroup dev_adapter (Device Adapter API)
 * @{
 */

/** device information */
struct ldm_dev_state {
    enum rsc_family      lds_family; /**< device family */
    char                *lds_model;  /**< device model */
    char                *lds_serial; /**< device serial */
};

enum ldm_fs_spc_flag {
    PHO_FS_READONLY = (1 << 0),
};

/** information about used and available space on a media */
struct ldm_fs_space {
    ssize_t                 spc_used;
    ssize_t                 spc_avail;
    enum ldm_fs_spc_flag    spc_flags;
};

/**
 * A device adapter is a vector of functions to operate on a device.
 * They should be invoked via their corresponding wrappers. Refer to
 * them for more precise explanation about each call.
 *
 * dev_query and dev_lookup are mandatory.
 * Other calls can be NULL if no operation is required, and do noop.
 */
struct pho_dev_adapter_module_ops {
    int (*dev_lookup)(const char *dev_id, char *dev_path, size_t path_size);
    int (*dev_query)(const char *dev_path, struct ldm_dev_state *lds);
    int (*dev_load)(const char *dev_path);
    int (*dev_eject)(const char *dev_path);
};

struct dev_adapter_module {
    struct module_desc desc;       /**< Description of this dev_adapter */
    const struct pho_dev_adapter_module_ops *ops;
                                   /**< Operations of this dev_adapter */
};

/**
 * Retrieve device adapter for the given device type.
 * @param[in]  dev_family Family of device.
 * @param[out] dev        Module that will contain the vector of functions to
 *                        handle the given device type.
 *
 * @return 0 on success, negative error code on failure.
 */
int get_dev_adapter(enum rsc_family dev_family,
                    struct dev_adapter_module **dev);

/**
 * Get device path from its identifier (eg. serial number).
 * @param[in]   dev         Device adapter module.
 * @param[in]   dev_id      Drive identifier.
 * @param[out]  dev_path    Path to access the device.
 * @param[in]   path_size   Max bytes that can be written to dev_path.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_dev_lookup(const struct dev_adapter_module *dev,
                                 const char *dev_id, char *dev_path,
                                 size_t path_size)
{
    assert(dev != NULL);
    assert(dev->ops != NULL);
    assert(dev->ops->dev_lookup != NULL);
    return dev->ops->dev_lookup(dev_id, dev_path, path_size);
}

/**
 * Query a device.
 * @param[in]   dev         Device adapter module.
 * @param[in]   dev_path    Device path.
 * @param[out]  lds         Device status.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_dev_query(const struct dev_adapter_module *dev,
                                const char *dev_path,
                                struct ldm_dev_state *lds)
{
    assert(dev != NULL);
    assert(dev->ops != NULL);
    assert(dev->ops->dev_query != NULL);
    return dev->ops->dev_query(dev_path, lds);
}

/**
 * Load a device with a medium on front of it.
 * @param[in]   dev         Device adapter module.
 * @param[in]   dev_path    Device path.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_dev_load(const struct dev_adapter_module *dev,
                               const char *dev_path)

{
    assert(dev != NULL);
    assert(dev->ops != NULL);
    if (dev->ops->dev_load == NULL)
        return 0;
    return dev->ops->dev_load(dev_path);
}

/**
 * Eject the medium currently loaded in the device.
 * @param[in]   dev         Device adapter module.
 * @param[in]   dev_path    Device path.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_dev_eject(const struct dev_adapter_module *dev,
                                const char *dev_path)

{
    assert(dev != NULL);
    assert(dev->ops != NULL);
    if (dev->ops->dev_eject == NULL)
        return 0;
    return dev->ops->dev_eject(dev_path);
}

/**
 * Free all resources associated with this lds.
 *
 * @param[in]   lds The lds of which to free resources
 */
void ldm_dev_state_fini(struct ldm_dev_state *lds);

/** @}*/

/**
 * \defgroup lib_adapter (Library Adapter API)
 * @{
 */

/**
 * Type of location in a library.
 */
enum med_location {
    MED_LOC_UNKNOWN = 0,
    MED_LOC_DRIVE   = 1,
    MED_LOC_SLOT    = 2,
    MED_LOC_ARM     = 3,
    MED_LOC_IMPEXP  = 4
};

/**
 * Location descriptor in a library.
 *
 * lia_addr examples:
 * SCSI library: 16 bits integer handled as 64 bits.
 * STK library: 4 integers (e.g. 0,1,10,5) encoded as 4x16 bits.
 */
struct lib_item_addr {
    enum med_location    lia_type; /**< type of location */
    uint64_t             lia_addr; /**< address of location */
};

/**
 * Device information in a library.
 */
struct lib_drv_info {
    struct lib_item_addr ldi_addr;      /**< location of the drive */
    uint64_t             ldi_first_addr;/**< address of the first drive */
    bool                 ldi_full;      /**< true if a medium is in the drive */
    struct pho_id        ldi_medium_id; /**< medium ID, if drive is full */
};

struct lib_handle;

/**
 * A library adapter is a vector of functions to control a tape library.
 * They should be invoked via their corresponding wrappers. Refer to
 * them for more precise explanation about each call.
 *
 * lib_drive_lookup and lib_media_lookup are mandatory.
 * lib_open, lib_close, lib_media_move and lib_scan do noop if they are NULL.
 */
struct pho_lib_adapter_module_ops {
    /* adapter functions */
    int (*lib_open)(struct lib_handle *lib, const char *dev, json_t *message);
    int (*lib_close)(struct lib_handle *lib);
    int (*lib_drive_lookup)(struct lib_handle *lib, const char *drive_serial,
                            struct lib_drv_info *drv_info, json_t *message);
    int (*lib_media_lookup)(struct lib_handle *lib, const char *media_label,
                            struct lib_item_addr *med_addr, json_t *message);
    int (*lib_media_move)(struct lib_handle *lib,
                          const struct lib_item_addr *src_addr,
                          const struct lib_item_addr *tgt_addr,
                          json_t *message);
    int (*lib_scan)(struct lib_handle *lib, json_t **lib_data, json_t *message);
};

struct lib_adapter_module {
    struct module_desc desc;       /**< Description of this lib adapter */
    const struct pho_lib_adapter_module_ops *ops;
                                   /**< Operations of this lib adapter */
};

/**
 * Library handle.
 */
struct lib_handle {
    void *lh_lib; /**< Opaque library handler */
    struct lib_adapter_module *ld_module; /**< Library adapter */
};

/**
 * Retrieve library adapter for the given library type.
 * @param[in]  lib_type Family of library.
 * @param[out] lib      Vector of functions to handle the library.
 *
 * @return 0 on success, negative error code on failure.
 */
int get_lib_adapter(enum lib_type lib_type, struct lib_adapter_module **lib);

/**
 * Open a library handler.
 *
 * Library access may rely on a caching of item addresses.
 * A library should be closed and reopened to refresh this cache in case a
 * change or inconsistency is detected.
 *
 * @param[in,out] lib_hdl Library handle.
 * @param[in]     dev     Device to open
 * @param[out]    message Json message to fill in case of error
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_lib_open(struct lib_handle *lib_hdl, const char *dev,
                               json_t *message)
{
    assert(lib_hdl->ld_module != NULL);
    assert(lib_hdl->ld_module->ops != NULL);
    if (lib_hdl->ld_module->ops->lib_open == NULL)
        return 0;
    return lib_hdl->ld_module->ops->lib_open(lib_hdl, dev, message);
}

/**
 * Close a library handler.
 *
 * Close access to the library and clean address cache.
 *
 * @param[in,out] lib_hdl      Lib handle holding an opened library adapter.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_lib_close(struct lib_handle *lib_hdl)
{
    assert(lib_hdl->ld_module != NULL);
    assert(lib_hdl->ld_module->ops != NULL);
    if (lib_hdl->ld_module->ops->lib_close == NULL)
        return 0;
    return lib_hdl->ld_module->ops->lib_close(lib_hdl);
}

/**
 * Get the location of a device in library from its serial number.
 *
 * @param[in,out] lib_hdl      Lib handle holding an opened library adapter.
 * @param[in]     drive_serial Serial number of the drive to lookup.
 * @param[out]    drv_info     Information about the drive.
 * @param[out]    message      Json message to fill in case of error
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_lib_drive_lookup(struct lib_handle *lib_hdl,
                                       const char *drive_serial,
                                       struct lib_drv_info *drv_info,
                                       json_t *message)
{
    assert(lib_hdl->ld_module != NULL);
    assert(lib_hdl->ld_module->ops != NULL);
    assert(lib_hdl->ld_module->ops->lib_drive_lookup != NULL);
    return lib_hdl->ld_module->ops->lib_drive_lookup(lib_hdl, drive_serial,
                                                     drv_info, message);
}

/**
 * Get the location of a media in library from its label.
 *
 * @param[in,out] Lib_hdl       Lib handle holding an opened library adapter.
 * @param[in]     media_label   Label of the media.
 * @param[out]    media_addr    Location of the media in library.
 * @param[out]    message       Json message to fill in case of error
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_lib_media_lookup(struct lib_handle *lib_hdl,
                                       const char *media_label,
                                       struct lib_item_addr *med_addr,
                                       json_t *message)
{
    assert(lib_hdl->ld_module != NULL);
    assert(lib_hdl->ld_module->ops != NULL);
    assert(lib_hdl->ld_module->ops->lib_media_lookup != NULL);
    return lib_hdl->ld_module->ops->lib_media_lookup(lib_hdl, media_label,
                                                     med_addr, message);
}

/**
 * Move a media in library from a source location to a target location.
 *
 * @param[in,out] lib_hdl   Lib handle holding an opened library adapter.
 * @param[in]     src_addr  Source address of the move.
 * @param[in]     tgt_addr  Target address of the move.
 * @param[out]    message   Json message to fill in case of error
 */
static inline int ldm_lib_media_move(struct lib_handle *lib_hdl,
                                     const struct lib_item_addr *src_addr,
                                     const struct lib_item_addr *tgt_addr,
                                     json_t *message)
{
    assert(lib_hdl->ld_module != NULL);
    assert(lib_hdl->ld_module->ops != NULL);
    if (lib_hdl->ld_module->ops->lib_media_move == NULL)
        return 0;
    return lib_hdl->ld_module->ops->lib_media_move(lib_hdl, src_addr, tgt_addr,
                                                   message);
}

/**
 * Scan a library and generate a json array with unstructured information.
 * Output information may vary, depending on the library.
 *
 * @param[in,out] lib_hdl   Lib handle holding an opened library adapter.
 * @param[in,out] lib_data  Json object allocated by ldm_lib_scan, json_decref
 *                          must be called later on to deallocate it properly
 * @param[out]    message   Json message to fill in case of error
 */
static inline int ldm_lib_scan(struct lib_handle *lib_hdl,
                               json_t **lib_data, json_t *message)
{
    assert(lib_hdl->ld_module != NULL);
    assert(lib_hdl->ld_module->ops != NULL);
    if (lib_hdl->ld_module->ops->lib_scan == NULL)
        return 0;
    return lib_hdl->ld_module->ops->lib_scan(lib_hdl, lib_data, message);
}

/** @}*/

/**
 * \defgroup fs_adapter_module (FileSystem Adapter API)
 * @{
 */

/**
 * A FS adapter is a vector of functions to manage a FileSystem.
 * Managing a filesystem requires a media is loaded into a device.
 *
 * Functions should be invoked via their corresponding wrappers. Refer to
 * them for more precise explanation about each call.
 *
 * fs_mounted, fs_df and fs_get_label are mandatory.
 * fs_mount, fs_umount and fs_format do noop if they are NULL.
 */
struct pho_fs_adapter_module_ops {
    int (*fs_mount)(const char *dev_path, const char *mnt_path,
                    const char *label);
    int (*fs_umount)(const char *dev_path, const char *mnt_path);
    int (*fs_format)(const char *dev_path, const char *label,
                     struct ldm_fs_space *fs_spc);
    int (*fs_mounted)(const char *dev_path, char *mnt_path,
                      size_t mnt_path_size);
    int (*fs_df)(const char *mnt_path, struct ldm_fs_space *fs_spc);
    int (*fs_get_label)(const char *mnt_path, char *fs_label, size_t llen);
};

struct fs_adapter_module {
    struct module_desc desc;       /**< Description of this fs_adapter_module */
    const struct pho_fs_adapter_module_ops *ops;
                                   /**< Operations of this fs_adapter_module */
};

/**
 * Retrieve adapter for the filesystem type.
 *
 * @return 0 on success, negative error code on failure.
 */
int get_fs_adapter(enum fs_type fs_type, struct fs_adapter_module **fsa);

/**
 * Mount a device as a given filesystem type.
 * @param[in] fsa       File system adapter module.
 * @param[in] dev_path  Path to the device.
 * @param[in] mnt_path  Mount point for the filesystem.
 * @param[in] fs_label  Validation label.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_fs_mount(const struct fs_adapter_module *fsa,
                               const char *dev_path, const char *mnt_point,
                               const char *fs_label)
{
    assert(fsa != NULL);
    assert(fsa->ops != NULL);
    if (fsa->ops->fs_mount == NULL)
        return 0;
    return fsa->ops->fs_mount(dev_path, mnt_point, fs_label);
}

/**
 * Unmount a filesystem.
 * @param[in] fsa       File system adapter module.
 * @param[in] dev_path  Path to the device.
 * @param[in] mnt_path  Mount point for the filesystem.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_fs_umount(const struct fs_adapter_module *fsa,
                                const char *dev_path, const char *mnt_point)
{
    assert(fsa != NULL);
    assert(fsa->ops != NULL);
    if (fsa->ops->fs_umount == NULL)
        return 0;
    return fsa->ops->fs_umount(dev_path, mnt_point);
}

/**
 * Format a media to the desired filesystem type.
 * @param[in]  fsa       File system adapter module.
 * @param[in]  dev_path  Path to the device.
 * @param[in]  label     Media label to apply.
 * @param[out] fs_spc    Filesystem space information.
 *                       Set to zero if fs provides no such operation.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_fs_format(const struct fs_adapter_module *fsa,
                                const char *dev_path, const char *label,
                                struct ldm_fs_space *fs_spc)
{
    assert(fsa != NULL);
    assert(fsa->ops != NULL);
    if (fsa->ops->fs_format == NULL) {
        memset(fs_spc, 0, sizeof(*fs_spc));
        return 0;
    }
    return fsa->ops->fs_format(dev_path, label, fs_spc);
}

/**
 * Indicate if a device is currently mounted as a filesystem.
 * @param[in]  fsa           File system adapter module.
 * @param[in]  dev_path      Path to the device.
 * @param[out] mnt_path      Mount point for the filesystem, if mounted.
 * @param[in]  mnt_path_size Size of mnt_path argument.
 *
 * @return 0 if the device is mounted, negative error code on failure.
 * @retval 0            the device is mounted.
 * @retval -ENOENT      the device is not mounted.
 * @retval -EMEDIUMTYPE the device is mounted with an unexpected FS type.
 */
static inline int ldm_fs_mounted(const struct fs_adapter_module *fsa,
                                 const char *dev_path, char *mnt_path,
                                 size_t mnt_path_size)
{
    assert(fsa != NULL);
    assert(fsa->ops != NULL);
    assert(fsa->ops->fs_mounted != NULL);
    return fsa->ops->fs_mounted(dev_path, mnt_path, mnt_path_size);
}

/**
 * Get used and available space in a filesystem.
 * @param[in] fsa        File system adapter module.
 * @param[in] mnt_path   Mount point of the filesystem.
 * @param[out] fs_spc    Filesystem space information.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_fs_df(const struct fs_adapter_module *fsa,
                            const char *mnt_path, struct ldm_fs_space *fs_spc)
{
    assert(fsa != NULL);
    assert(fsa->ops != NULL);
    assert(fsa->ops->fs_df != NULL);
    return fsa->ops->fs_df(mnt_path, fs_spc);
}

/**
 * Get filesystem label
 * @param[in] fsa       File system adapter module.
 * @param[in] mnt_path  Mount point of the filesystem.
 * @param[out] label    Buffer of at least PHO_LABEL_MAX_LEN + 1 bytes.
 * @param[in]  llen     Label buffer length in bytes.
 *
 * @return 0 on success, negative error code on failure.
 */
static inline int ldm_fs_get_label(const struct fs_adapter_module *fsa,
                                   const char *mnt_path, char *fs_label,
                                   size_t llen)
{
    assert(fsa != NULL);
    assert(fsa->ops != NULL);
    assert(fsa->ops->fs_get_label != NULL);
    return fsa->ops->fs_get_label(mnt_path, fs_label, llen);
}

/** @}*/
#endif
