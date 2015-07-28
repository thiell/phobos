/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos Local Device Manager.
 *
 * This modules implements low level device control on local host.
 */
#ifndef _PHO_LDM_H
#define _PHO_LDM_H

#include "pho_types.h"

/**
 * Retrieve device information from system.
 * @param(in) dev_type family of device to query.
 *                 Caller can pass PHO_DEV_INVAL if it doesn't know.
 *                 The function will then try to guess the type of device,
 *                 but the call is more expensive.
 * @param(in)  dev_path path to the device.
 * @param(out) dev_st   information about the device.
 *
 * @return 0 on success, -errno on failure.
 */
int ldm_device_query(enum dev_family dev_type, const char *dev_path,
                     struct dev_state *dev_st);

/**
 * Mount a device as a given filesystem type.
 * @param(in) fs        type of filesystem.
 * @param(in) dev_path  path to the device.
 * @param(in) mnt_point mount point of the filesystem.
 *
 * @return 0 on success, -errno on failure.
 */
int ldm_fs_mount(enum fs_type fs, const char *dev_path, const char *mnt_point);

#endif
