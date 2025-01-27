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
 * \brief  LRS Device Thread handling
 */
#ifndef _PHO_LRS_DEVICE_H
#define _PHO_LRS_DEVICE_H

#include <glib.h>
#include <pthread.h>
#include <stdbool.h>

#include "lrs_thread.h"

#include "pho_dss.h"
#include "pho_ldm.h"
#include "pho_types.h"

struct lrs_sched;
struct lrs_dev;

/**
 * Structure handling thread devices used by the scheduler.
 */
struct lrs_dev_hdl {
    GPtrArray      *ldh_devices;   /**< List of active devices of
                                     *  type lrs_dev
                                     */
    struct timespec sync_time_ms;  /**< Time threshold for medium
                                     *  synchronization
                                     */
    unsigned int    sync_nb_req;   /**< Number of requests
                                     *  threshold for medium
                                     *  synchronization
                                     */
    unsigned long   sync_wsize_kb; /**< Written size threshold for
                                     *  medium synchronization
                                     */
};

/** Request pushed to a device */
struct sub_request {
    struct req_container *reqc;
    size_t medium_index; /**< index of the medium in reqc that this device
                           *  must handle
                           */
    bool failure_on_medium; /**< an error occurs on medium */
};

/**
 * sub_request_free can be used as glib callback
 *
 * @param[in]   sub_req     sub_request to free
 */
void sub_request_free(struct sub_request *sub_req);

/**
 * Cancel sub_request on error
 *
 * Must be called with a lock on sub_request->reqc
 * If a previous error is detected with a non null rc in reqc, the medium of
 * this sub_request is freed and set to NULL and its status is set as
 * SUB_REQUEST_CANCEL.
 *
 * @param[in]   sub_request     the rwalloc sub_request to check
 * @param[out]  ended           Will be set to true if the request is ended and
 *                              must be freed, false otherwise.
 *
 * @return  True if there was an error and the request was cancelled,
 *          false otherwise.
 */
bool locked_cancel_rwalloc_on_error(struct sub_request *sub_request,
                                    bool *ended);

/**
 * Load a medium into a drive or return -EBUSY to retry later
 *
 * The loaded medium is registered as dev->ld_dss_media_info. If
 * free_medium is true, medium is set to NULL.
 *
 * If an error occurs on a medium that is not registered to
 * dev->ld_dss_media_info, the medium is considered failed, marked as such in
 * the DSS, freed and set to NULL if free_medium is true. WARNING: if we
 * cannot set it to failed into the DSS, the medium DSS lock is not released.
 *
 * @param[in]   release_medium_on_dev_only_failure
 *                              If true, release the medium on a dev-only
 *                              failure. The medium is neither freed or set to
 *                              NULL.
 * @param[out]  failure_on_dev  Return false if no error or error not due to the
 *                              device, return true if there is an error due to
 *                              the device.
 * @param[out]  failure_on_medium
 *                              Return false if no error or error not due to the
 *                              medium, return true if there is an error due to
 *                              the medium.
 * @param[out]  can_retry       true if an error occured on the library and the
 *                              operation can be retried later.
 * @param[in]   free_medium     If false, medium is never freed and never set to
 *                              NULL.
 *
 * @return 0 on success, -error number on error. -EBUSY is returned when a
 * drive to drive medium movement was prevented by the library or if the device
 * is empty.
 */
int dev_load(struct lrs_dev *dev, struct media_info **medium,
             bool release_medium_on_dev_only_failure,
             bool *failure_on_dev, bool *failure_on_medium,
             bool *can_retry, bool free_medium);

/**
 * Unload medium from device
 *
 * - DSS unlock the medium
 * - set drive's ld_op_status to PHO_DEV_OP_ST_EMPTY
 *
 * @param[in]   dev   The device to unload a medium from
 *
 * @return 0 on success, -error number on error.
 */
int dev_unload(struct lrs_dev *dev);

/**
 * Parameters to check when a synchronization is required.
 */
struct sync_params {
    GPtrArray       *tosync_array;  /**< array of release requests with to_sync
                                      *  set
                                      */
    struct timespec  oldest_tosync; /**< oldest release request in
                                      *  \p tosync_array
                                      */
    size_t           tosync_size;   /**< total size of release requests in
                                      *  \p tosync_array
                                      */
};

/**
 * Data specific to the device thread.
 */
struct lrs_dev {
    pthread_mutex_t      ld_mutex;              /**< exclusive access */
    struct dev_info     *ld_dss_dev_info;       /**< device info from DSS */
    struct lib_drv_info  ld_lib_dev_info;       /**< device info from library
                                                  *  (for tape drives)
                                                  */
    struct ldm_dev_state ld_sys_dev_state;      /**< device info from system */

    enum dev_op_status   ld_op_status;          /**< operational status of the
                                                  * device
                                                  */
    char                 ld_dev_path[PATH_MAX]; /**< path to the device */
    struct media_info   *ld_dss_media_info;     /**< loaded media info
                                                  *  from DSS, if any
                                                  */
    char                 ld_mnt_path[PATH_MAX]; /**< mount path of the
                                                  * filesystem
                                                  */
    struct sub_request  *ld_sub_request;        /**< sub request to handle */
    bool                 ld_ongoing_scheduled;  /**< one I/O is going to be
                                                  *  scheduled
                                                  */
    bool                 ld_ongoing_io;         /**< one I/O is ongoing */
    bool                 ld_needs_sync;         /**< medium needs to be sync */
    struct thread_info   ld_device_thread;      /**< thread handling the actions
                                                  * executed on the device
                                                  */
    struct sync_params   ld_sync_params;        /**< pending synchronization
                                                  * requests
                                                  */
    struct tsqueue      *ld_response_queue;     /**< reference to the response
                                                  * queue
                                                  */
    struct format_media *ld_ongoing_format;     /**< reference to the ongoing
                                                  * format array
                                                  */
    /* TODO: move sched_req_queue use to sched_retry_queue */
    struct tsqueue      *sched_req_queue;       /**< reference to the sched
                                                  * request queue
                                                  */
    struct tsqueue      *sched_retry_queue;     /**< reference to the sched
                                                  * retry queue
                                                  */
    struct lrs_dev_hdl  *ld_handle;
    int                  ld_io_request_type;
        /**< OR-ed enum io_request_type indicating which schedulers currently
         * have access to this device. Modified by
         * io_sched_handle::dispatch_devices.
         */
    int                  ld_last_client_rc;     /**< last I/O error of a client
                                                  *  sent on release.
                                                  */
    const char *ld_technology; /** The technology of the device. For tapes, this
                                 * corresponds the tape generation (e.g. LTO5).
                                 * For dir, it is NULL. This information is only
                                 * used by the fair share dispatch_devices
                                 * algorithm for now.
                                 */
};

static inline bool dev_is_release_ready(struct lrs_dev *dev)
{
    return dev && !thread_is_stopped(&dev->ld_device_thread);
}

bool is_request_tosync_ended(struct req_container *req);

int queue_release_response(struct tsqueue *response_queue,
                           struct req_container *reqc);

static inline bool dev_is_sched_ready(struct lrs_dev *dev)
{
    return dev && thread_is_running(&dev->ld_device_thread) &&
           !dev->ld_ongoing_io && !dev->ld_needs_sync && !dev->ld_sub_request &&
           !dev->ld_ongoing_scheduled &&
           dev->ld_op_status != PHO_DEV_OP_ST_FAILED &&
           (dev->ld_dss_dev_info->rsc.adm_status == PHO_RSC_ADM_ST_UNLOCKED);
}

static inline bool dev_is_online(struct lrs_dev *dev)
{
    return dev && thread_is_running(&dev->ld_device_thread) &&
        (dev->ld_dss_dev_info->rsc.adm_status == PHO_RSC_ADM_ST_UNLOCKED);
}

static inline bool is_device_shared_between_schedulers(struct lrs_dev *dev)
{
    return __builtin_popcount(dev->ld_io_request_type & 0b111) != 0;
}

/**
 *  TODO: will become a device thread static function when all media operations
 *  will be moved to device thread
 */
int clean_tosync_array(struct lrs_dev *dev, int rc);

/**
 * Add a new sync request to a device
 *
 * \param[in,out]   dev     device to add the sync request
 * \param[in]       reqc    sync request to add
 * \param[in]       medium  index in reqc of the medium to sync
 *
 * \return                0 on success, -errno on failure
 */
int push_new_sync_to_device(struct lrs_dev *dev, struct req_container *reqc,
                            size_t medium_index);

/**
 * Initialize an lrs_dev_hdl to manipulate devices from the scheduler
 *
 * \param[out]   handle   pointer to an uninitialized handle
 * \param[in]    family   family of the devices handled
 *
 * \return                0 on success, -errno on failure
 */
int lrs_dev_hdl_init(struct lrs_dev_hdl *handle, enum rsc_family family);

/**
 * Undo the work done by lrs_dev_hdl_init
 *
 * \param[in]    handle   pointer to an initialized handle
 */
void lrs_dev_hdl_fini(struct lrs_dev_hdl *handle);

/**
 * Creates a new device thread and add it to the list of registered devices
 *
 * \param[in]    sched    scheduler managing the device
 * \param[in]    handle   initialized device handle
 * \param[in]    name     serial number of the device
 *
 * \return                0 on success, -errno no failure
 */
int lrs_dev_hdl_add(struct lrs_sched *sched,
                    struct lrs_dev_hdl *handle,
                    const char *name);

/**
 * Undo the work done by lrs_dev_hdl_add
 *
 * This function is blocking as it waits for the end of the device thread.
 *
 * \param[in]    handle   initialized device handle
 * \param[in]    index    index of the device to remove from the list
 * \param[in]    rc       error which caused the thread to stop
 *
 * \return                0 on success, -errno no failure
 */
int lrs_dev_hdl_del(struct lrs_dev_hdl *handle, int index, int rc);

/**
 * Will try to remove a device thread context.
 *
 * If the device thread is still busy after 100ms, we delay the removal.
 *
 * \param[in]   handle      Device handle.
 * \param[in]   index       Index of the device to remove from the list.
 *
 * \return                  0 on success,
 *                         -EAGAIN if the thread is still busy,
 *                         -errno on failure.
 */
int lrs_dev_hdl_trydel(struct lrs_dev_hdl *handle, int index);

/**
 * Retry to remove a device thread context.
 *
 * If the device thread is still busy, we delay the removal.
 *
 * \param[in]   handle      Device handle.
 * \param[in]   dev         Device to remove from the list.
 *
 * \return                  0 on success,
 *                         -EAGAIN if the thread is still busy,
 *                         -errno on failure.
 */
int lrs_dev_hdl_retrydel(struct lrs_dev_hdl *handle, struct lrs_dev *dev);

/**
 * Load all the devices that are attributed to this LRS from the DSS
 *
 * \param[in]      sched    scheduler managing the device
 * \param[in/out]  handle   initialized device handle
 *
 * \return                0 on success, -errno no failure
 */
int lrs_dev_hdl_load(struct lrs_sched *sched,
                     struct lrs_dev_hdl *handle);

/**
 * Remove all the devices from the handle
 *
 * This function is blocking as it waits for the termination of all threads.
 * Each thread is signaled first and then joined so that they are stopped
 * concurrently.
 *
 * \param[in]  handle  pointer to an initialized handle to clear
 */
void lrs_dev_hdl_clear(struct lrs_dev_hdl *handle);

/**
 * Wrapper arround GLib's getter to retrive devices' structures
 *
 * \param[in]  handle  initialized device handle
 * \param[in]  index   index of the device, must be smaller than the number of
 *                     devices in handle->ldh_devices
 *
 * \return             a pointer to the requested device is returned
 */
struct lrs_dev *lrs_dev_hdl_get(struct lrs_dev_hdl *handle, int index);

/**
 * Wrap library open operations
 *
 * @param[in]   dev_type    Device type
 * @param[in]   log         Log to fill in case of an SCSI error
 * @param[out]  lib_hdl     Library handle
 *
 * @return          0 on success, -1 * posix error code on failure.
 */
int wrap_lib_open(enum rsc_family dev_type, struct lib_handle *lib_hdl,
                  struct pho_log *log);

/**
 * Returns the technology of a drive from its model using the configuration for
 * the association.
 *
 * \param[in]  dev     device whose technology name to get
 * \param[out] techno  allocated technology of the device (must be passed to
 *                     free)
 *
 * \return             0 on success, negative POSIX error code on failure
 *            -ENODATA the model of the device was not found in the
 *                     configuration
 */
int lrs_dev_technology(const struct lrs_dev *dev, const char **techno);

#endif
