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
 * \brief  Handling of layout and extent structures.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <jansson.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pho_common.h"
#include "pho_type_utils.h"

bool pho_id_equal(const struct pho_id *id1, const struct pho_id *id2)
{
    if (id1->family != id2->family)
        return false;

    if (strcmp(id1->name, id2->name))
        return false;

    return true;
}

int build_extent_key(const char *uuid, int version, const char *extent_tag,
                     char **key)
{
    int rc;

    rc = asprintf(key, "%d.%s.%s", version, extent_tag, uuid);
    if (rc < 0) {
        *key = NULL;
        return -ENOMEM;
    }

    return 0;
}

void init_pho_lock(struct pho_lock *lock, char *hostname, int owner,
                   struct timeval *timestamp)
{
    lock->hostname = xstrdup_safe(hostname);
    lock->owner = owner;
    lock->timestamp = *timestamp;
}

void pho_lock_cpy(struct pho_lock *lock_dst, const struct pho_lock *lock_src)
{
    lock_dst->hostname = xstrdup_safe(lock_src->hostname);
    lock_dst->owner = lock_src->owner;
    lock_dst->timestamp = lock_src->timestamp;
}

void pho_lock_clean(struct pho_lock *lock)
{
    if (lock == NULL)
        return;

    free(lock->hostname);
    lock->hostname = NULL;
    lock->owner = 0;
}

void dev_info_cpy(struct dev_info *dev_dst, const struct dev_info *dev_src)
{
    assert(dev_src);
    dev_dst->rsc.id = dev_src->rsc.id;
    dev_dst->rsc.model = xstrdup_safe(dev_src->rsc.model);
    dev_dst->rsc.adm_status = dev_src->rsc.adm_status;
    dev_dst->path = xstrdup_safe(dev_src->path);
    dev_dst->host = xstrdup_safe(dev_src->host);
    pho_lock_cpy(&dev_dst->lock, &dev_src->lock);
}

struct dev_info *dev_info_dup(const struct dev_info *dev)
{
    struct dev_info *dev_out;

    dev_out = xmalloc(sizeof(*dev_out));
    dev_info_cpy(dev_out, dev);

    return dev_out;
}

void dev_info_free(struct dev_info *dev, bool free_top_struct)
{
    if (!dev)
        return;
    pho_lock_clean(&dev->lock);
    free(dev->rsc.model);
    free(dev->path);
    free(dev->host);
    if (free_top_struct)
        free(dev);
}

struct media_info *media_info_dup(const struct media_info *mda)
{
    struct media_info *media_out;

    media_out = xmalloc(sizeof(*media_out));

    memcpy(media_out, mda, sizeof(*media_out));
    media_out->rsc.model = xstrdup_safe(mda->rsc.model);

    tags_dup(&media_out->tags, &mda->tags);

    pho_lock_cpy(&media_out->lock, &mda->lock);

    return media_out;
}

void media_info_cleanup(struct media_info *medium)
{
    if (!medium)
        return;

    pho_lock_clean(&medium->lock);
    free(medium->rsc.model);
    tags_free(&medium->tags);
}

void media_info_free(struct media_info *mda)
{
    if (!mda)
        return;

    pho_lock_clean(&mda->lock);
    free(mda->rsc.model);
    tags_free(&mda->tags);
    free(mda);
}

struct object_info *object_info_dup(const struct object_info *obj)
{
    struct object_info *obj_out = NULL;

    /* use xcalloc to set memory to 0 */
    obj_out = xcalloc(sizeof(*obj_out), 1);

    /* dup oid */
    obj_out->oid = xstrdup_safe(obj->oid);

    /* dup uuid */
    obj_out->uuid = xstrdup_safe(obj->uuid);

    /* version */
    obj_out->version = obj->version;

    /* dup user_md */
    obj_out->user_md = xstrdup_safe(obj->user_md);

    /* timeval deprec_time */
    obj_out->deprec_time = obj->deprec_time;

    /* success */
    return obj_out;
}

void object_info_free(struct object_info *obj)
{
    if (!obj)
        return;

    free(obj->oid);
    free(obj->uuid);
    free(obj->user_md);
    free(obj);
}

void tags_dup(struct tags *tags_dst, const struct tags *tags_src)
{
    if (!tags_dst)
        return;

    if (!tags_src) {
        *tags_dst = NO_TAGS;
        return;
    }

    tags_init(tags_dst, tags_src->tags, tags_src->n_tags);
}

void tags_init(struct tags *tags, char **tag_values, size_t n_tags)
{
    ssize_t i;

    tags->n_tags = n_tags;
    if (tags->n_tags == 0) {
        tags->tags = NULL;
        return;
    }

    tags->tags = xcalloc(n_tags, sizeof(*tags->tags));

    for (i = 0; i < n_tags; i++)
        tags->tags[i] = xstrdup_safe(tag_values[i]);
}

void tags_free(struct tags *tags)
{
    size_t i;

    if (!tags)
        return;

    for (i = 0; i < tags->n_tags; i++)
        free(tags->tags[i]);
    free(tags->tags);

    tags->tags = NULL;
    tags->n_tags = 0;
}

bool tags_eq(const struct tags *tags1, const struct tags *tags2)
{
    size_t i;

    /* Same size? */
    if (tags1->n_tags != tags2->n_tags)
        return false;

    /* Same content? (order matters) */
    for (i = 0; i < tags1->n_tags; i++)
        if (strcmp(tags1->tags[i], tags2->tags[i]))
            return false;

    return true;
}

bool tag_exists(const struct tags *tags, const char *tag_str)
{
    int i;

    for (i = 0; i < tags->n_tags; i++)
        if (strcmp(tag_str, tags->tags[i]) == 0)
            return true;

    return false;
}

bool tags_in(const struct tags *haystack, const struct tags *needle)
{
    size_t ndl_i, hay_i;

    /* The needle cannot be larger than the haystack */
    if (needle->n_tags > haystack->n_tags)
        return false;

    /* Naive n^2 set inclusion check */
    for (ndl_i = 0; ndl_i < needle->n_tags; ndl_i++) {
        for (hay_i = 0; hay_i < haystack->n_tags; hay_i++)
            if (!strcmp(needle->tags[ndl_i], haystack->tags[hay_i]))
                break;

        /* Needle tag not found in haystack tags */
        if (hay_i == haystack->n_tags)
            return false;
    }

    return true;
}

void str2tags(const char *tag_str, struct tags *tags)
{
    size_t n_alias_tags = 0;
    char *parse_tag_str;
    char *single_tag;
    char *saveptr;
    size_t i;

    if (tag_str == NULL || tags == NULL)
        return;

    i = tags->n_tags;

    if (strcmp(tag_str, "") == 0)
        return;

    /* copy the tags list to tokenize it */
    parse_tag_str = xstrdup(tag_str);

    /* count number of tags in alias */
    single_tag = strtok_r(parse_tag_str, ",", &saveptr);
    while (single_tag != NULL) {
        n_alias_tags++;
        single_tag = strtok_r(NULL, ",", &saveptr);
    }
    free(parse_tag_str);

    if (n_alias_tags == 0)
        return;

    /* allocate space for new tags */
    if (tags->n_tags > 0)
        tags->tags = xrealloc(tags->tags,
                              (tags->n_tags + n_alias_tags) * sizeof(char *));
    else
        tags->tags = xcalloc(n_alias_tags, sizeof(char *));

    /* fill tags */
    parse_tag_str = xstrdup(tag_str);

    for (single_tag = strtok_r(parse_tag_str, ",", &saveptr);
         single_tag != NULL;
         single_tag = strtok_r(NULL, ",", &saveptr), i++) {
        if (tag_exists(tags, single_tag))
            continue;

        tags->tags[i] = xstrdup(single_tag);
        tags->n_tags++;
    }

    free(parse_tag_str);
}

int str2timeval(const char *tv_str, struct timeval *tv)
{
    struct tm tmp_tm = {0};
    char *usec_ptr;

    usec_ptr = strptime(tv_str, "%Y-%m-%d %T", &tmp_tm);
    if (!usec_ptr)
        LOG_RETURN(-EINVAL, "Object timestamp '%s' is not well formatted",
                   tv_str);
    tv->tv_sec = mktime(&tmp_tm);
    tv->tv_usec = 0;
    if (*usec_ptr == '.') {
        tv->tv_usec = atoi(usec_ptr + 1);
        /* in case usec part is less than 6 characters */
        tv->tv_usec *= pow(10, 6 - strlen(usec_ptr + 1));
    }

    return 0;
}

void timeval2str(const struct timeval *tv, char *tv_str)
{
    char buf[PHO_TIMEVAL_MAX_LEN - 7];

    if (tv->tv_sec == 0 && tv->tv_usec == 0) {
        strcpy(tv_str, "0");
        return;
    }

    strftime(buf, sizeof(buf), "%Y-%m-%d %T", localtime(&tv->tv_sec));
    snprintf(tv_str, PHO_TIMEVAL_MAX_LEN, "%s.%06ld", buf, tv->tv_usec);
}

void layout_info_free_extents(struct layout_info *layout)
{
    int i;

    for (i = 0; i < layout->ext_count; i++) {
        free(layout->extents[i].address.buff);
        free(layout->extents[i].uuid);
    }
    layout->ext_count = 0;
    free(layout->extents);
    layout->extents = NULL;
}

int tsqueue_init(struct tsqueue *tsqueue)
{
    int rc;

    rc = pthread_mutex_init(&tsqueue->mutex, NULL);
    if (rc)
        LOG_RETURN(-rc, "Unable to init threadsafe queue mutex");

    tsqueue->queue = g_queue_new();

    return 0;
}

void tsqueue_destroy(struct tsqueue *tsq, GDestroyNotify free_func)
{
    int rc;

    if (free_func == NULL)
        g_queue_free(tsq->queue);
    else
        g_queue_free_full(tsq->queue, free_func);

    rc = pthread_mutex_destroy(&tsq->mutex);
    if (rc)
        pho_error(-rc, "Unable to destroy threadsafe queue mutex");
}

void *tsqueue_pop(struct tsqueue *tsq)
{
    void *data;

    MUTEX_LOCK(&tsq->mutex);
    data = g_queue_pop_tail(tsq->queue);
    MUTEX_UNLOCK(&tsq->mutex);

    return data;
}

void tsqueue_push(struct tsqueue *tsq, void *data)
{
    MUTEX_LOCK(&tsq->mutex);
    g_queue_push_head(tsq->queue, data);
    MUTEX_UNLOCK(&tsq->mutex);
}

unsigned int tsqueue_get_length(struct tsqueue *tsq)
{
    unsigned int length;

    MUTEX_LOCK(&tsq->mutex);
    length = g_queue_get_length(tsq->queue);
    MUTEX_UNLOCK(&tsq->mutex);

    return length;
}

struct pho_id *pho_id_dup(const struct pho_id *src)
{
    struct pho_id *dup;

    dup = xmalloc(sizeof(*dup));
    pho_id_copy(dup, src);
    return dup;
}
