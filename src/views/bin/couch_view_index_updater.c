/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */

/**
 * @copyright 2013 Couchbase, Inc.
 *
 * @author Sarath Lakshman  <sarath@couchbase.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 **/

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../view_group.h"
#include "../util.h"
#include "../file_sorter.h"

#define BUF_SIZE 8192

static void die_on_exit_msg(void *args)
{
    char buf[4];
    (void) args;

    if (fread(buf, 1, 4, stdin) == 4 && !strncmp(buf, "exit", 4)) {
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    view_group_info_t *group_info = NULL;
    char buf[BUF_SIZE];
    char **source_files = NULL;
    char *tmp_dir = NULL;
    int i;
    size_t len;
    int batch_size;
    int ret = 2;
    view_group_update_stats_t stats;
    sized_buf header_buf = {NULL, 0};
    sized_buf header_outbuf = {NULL, 0};
    view_error_t error_info;
    cb_thread_t exit_thread;

    (void) argc;
    (void) argv;

    /* Set all stats counters to zero */
    memset((char *) &stats, 0, sizeof(view_group_update_stats_t));

    if (couchstore_read_line(stdin, buf, BUF_SIZE) != buf) {
        fprintf(stderr, "Error reading temporary directory path\n");
        goto out;
    }

    len = strlen(buf);
    tmp_dir = (char *) malloc(len + 1);
    if (tmp_dir == NULL) {
        fprintf(stderr, "Memory allocation failure\n");
        goto out;
    }

    memcpy(tmp_dir, buf, len);
    tmp_dir[len] = '\0';

    group_info = couchstore_read_view_group_info(stdin, stderr);
    if (group_info == NULL) {
        ret = COUCHSTORE_ERROR_ALLOC_FAIL;
        goto out;
    }

    source_files = (char **) calloc(group_info->num_btrees + 1, sizeof(char *));
    if (source_files == NULL) {
        fprintf(stderr, "Memory allocation failure\n");
        ret = COUCHSTORE_ERROR_ALLOC_FAIL;
        goto out;
    }

    for (i = 0; i <= group_info->num_btrees; ++i) {
        if (couchstore_read_line(stdin, buf, BUF_SIZE) != buf) {
            if (i == 0) {
                fprintf(stderr, "Error reading source file for id btree\n");
            } else {
                fprintf(stderr,
                        "Error reading source file for btree %d\n", i - 1);
            }
            ret = COUCHSTORE_ERROR_INVALID_ARGUMENTS;
            goto out;
        }

        len = strlen(buf);
        source_files[i] = (char *) malloc(len + 1);
        if (source_files[i] == NULL) {
            fprintf(stderr, "Memory allocation failure\n");
            ret = COUCHSTORE_ERROR_ALLOC_FAIL;
            goto out;
        }
        memcpy(source_files[i], buf, len);
        source_files[i][len] = '\0';
    }

    if (fscanf(stdin, "%d\n", &batch_size) != 1) {
        fprintf(stderr, "Error reading batch size\n");
        ret = COUCHSTORE_ERROR_INVALID_ARGUMENTS;
        goto out;
    }

    if (fscanf(stdin, "%lu\n", &header_buf.size) != 1) {
        fprintf(stderr, "Error reading viewgroup header size\n");
        goto out;
    }

    header_buf.buf = malloc(header_buf.size);
    if (header_buf.buf == NULL) {
            fprintf(stderr, "Memory allocation failure\n");
            ret = COUCHSTORE_ERROR_ALLOC_FAIL;
            goto out;
    }

    if (fread(header_buf.buf, header_buf.size, 1, stdin) != 1) {
        fprintf(stderr,
                "Error reading viewgroup header from stdin\n");
        goto out;
    }

    /* Start a watcher thread to gracefully die on exit message */
    ret = cb_create_thread(&exit_thread, die_on_exit_msg, NULL, 1);
    if (ret < 0) {
        fprintf(stderr, "Error starting stdin exit listener thread\n");
        /* For differentiating from couchstore_error_t */
        ret = -ret;
        goto out;
    }

    /* Sort all operation list files for id and view btrees */
    ret = sort_view_ids_ops_file(source_files[0], tmp_dir);
    if (ret != FILE_SORTER_SUCCESS) {
        fprintf(stderr, "Error sorting id file: %d\n", ret);
        goto out;
    }

    for (i = 1; i <= group_info->num_btrees; ++i) {
        ret = sort_view_kvs_ops_file(source_files[i], tmp_dir);
        if (ret != FILE_SORTER_SUCCESS) {
            fprintf(stderr, "Error sorting view %d file: %d\n",i-1, ret);
            goto out;
        }
    }

    ret = couchstore_update_view_group(group_info,
                                      source_files[0],
                                      (const char **) &source_files[1],
                                      batch_size,
                                      &header_buf,
                                      &stats,
                                      &header_outbuf,
                                      &error_info);


    if (ret != COUCHSTORE_SUCCESS) {
        if (error_info.error_msg != NULL && error_info.view_name != NULL) {
            fprintf(stderr,
                    "Error updating index for view `%s`, reason: %s\n",
                    error_info.view_name,
                    error_info.error_msg);
        }
        goto out;
    }

    fprintf(stdout, "Header Len : %lu\n", header_outbuf.size);
    fwrite(header_outbuf.buf, header_outbuf.size, 1, stdout);
    fprintf(stdout, "\n");

    fprintf(stdout,"Results ="
                   " id_inserts : %"PRIu64
                   ", id_deletes : %"PRIu64
                   ", kv_inserts : %"PRIu64
                   ", kv_deletes : %"PRIu64
                   ", cleanups : %"PRIu64"\n",
                   stats.ids_inserted,
                   stats.ids_removed,
                   stats.kvs_inserted,
                   stats.kvs_removed,
                   stats.purged);

out:
    if (source_files != NULL) {
        for (i = 0; i <= group_info->num_btrees; ++i) {
            free(source_files[i]);
        }
        free(source_files);
    }

    couchstore_free_view_group_info(group_info);
    free((void *) error_info.error_msg);
    free((void *) error_info.view_name);
    free((void *) header_buf.buf);
    free((void *) header_outbuf.buf);
    free(tmp_dir);

    return (ret < 0) ? (100 + ret) : ret;
}
