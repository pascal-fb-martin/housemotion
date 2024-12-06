/* HouseMotion - a web server to handle videos files from Motion.
 *
 * Copyright 2024, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * housemotion_store.c - Access the videos and image files stored by Motion.
 *
 * SYNOPSYS:
 *
 * This module handles access to existing Motion recordings. It schedules
 * and execute the transfer of these recording to the HouseDvr service.
 *
 * void housemotion_store_initialize (int argc, const char **argv);
 *
 *    Initialize this module.
 *
 * void housemotion_store_background (time_t now);
 *
 *    The periodic function that manages the video storage.
 *
 * int housemotion_store_status (char *buffer, int size);
 *
 *    A function that populates a status overview of the storage in JSON.
 *
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <unistd.h>

#include <echttp.h>
#include <echttp_static.h>
#include <echttp_json.h>

#include "houselog.h"
#include "housemotion_store.h"

#define DEBUG if (echttp_isdebug()) printf

static int HouseMotionMaxSpace = 0; // Default is no automatic cleanup.

static const char *HouseMotionStorage = "/videos";


static void housemotion_store_schedule (const char *path) {
}

static const char *housemotion_store_event (const char *method, const char *uri,
                                            const char *data, int length) {
    const char *name = echttp_parameter_get("name");
    // Loop to find each file matching this event name.
    // TBD
    return 0;
}

static const char *housemotion_store_file (const char *method, const char *uri,
                                           const char *data, int length) {

    const char *path = echttp_parameter_get("path");
    if (path) housemotion_store_schedule (path);
    return 0;
}

void housemotion_store_initialize (int argc, const char **argv) {

    int i;
    const char *max = 0;

    for (i = 1; i < argc; ++i) {
        echttp_option_match ("-motion-store=", argv[i], &HouseMotionStorage);
        echttp_option_match ("-motion-clean=", argv[i], &max);
    }
    if (max) {
        HouseMotionMaxSpace = atoi(max);
    }
    echttp_route_uri ("/cctv/motion/event", housemotion_store_event);
    echttp_route_uri ("/cctv/motion/file", housemotion_store_file);
}

// Calculate storage space information (total, free, %used).
// Using the statvfs data is tricky because there are two different units:
// fragments and blocks, which can have different sizes. This code strictly
// follows the documentation in "man statvfs".
// The problem is compounded by these sizes being the same value for ext4,
// making it difficult to notice mistakes..
//
static long long housemotion_store_free (const struct statvfs *fs) {
    return (long long)(fs->f_bavail) * fs->f_bsize;
}

static long long housemotion_store_total (const struct statvfs *fs) {
    return (long long)(fs->f_blocks) * fs->f_frsize;
}

static int housemotion_store_used (const struct statvfs *fs) {

    long long total = housemotion_store_total (fs);
    return (int)(((total - housemotion_store_free(fs)) * 100) / total);
}

void housemotion_store_friendly (char *buffer, int size, long long value) {

    if (value > 1024*1024*1024) {
        snprintf (buffer, size, "%d.%01dGB",
                  (int)(value / 1024*1024*1024),
                  (int)((value % 1024*1024*1024) / 1024*1024*100));
    } else if (value > 1024*1024) {
        snprintf (buffer, size, "%d.%01dMB",
                  (int)(value / 1024*1024),
                  (int)((value % 1024*1024) / 1024*100));
    } else {
        snprintf (buffer, size, "%dKB", (int)(value / 1024));
    }
}

int housemotion_store_status (char *buffer, int size) {

    int i;
    int cursor = 0;
    const char *prefix = "";

    struct statvfs storage;

    if (statvfs (HouseMotionStorage, &storage)) return 0;

    char ascii[64];
    housemotion_store_friendly (ascii, sizeof(ascii),
                                housemotion_store_free (&storage));
    cursor += snprintf (buffer, size, "\"available\":\"%s\"", ascii);
    if (cursor >= size) goto overflow;
    housemotion_store_friendly (ascii, sizeof(ascii),
                                housemotion_store_total (&storage));
    cursor += snprintf (buffer+cursor, size-cursor, ",\"total\":\"%s\"", ascii);
    if (cursor >= size) goto overflow;
    cursor += snprintf (buffer+cursor, size-cursor, ",\"used\":\"%d%%\"",
                        housemotion_store_used (&storage));
    if (cursor >= size) goto overflow;

    return cursor;

overflow:
    houselog_trace (HOUSE_FAILURE, "BUFFER", "overflow");
    buffer[0] = 0;
    return 0;
}

static int housemotion_store_oldest (const char *parent) {

    int oldest = 9999;

    DIR *dir = opendir (parent);
    if (!dir) return 0;

    struct dirent *p = readdir(dir);
    while (p) {
        if ((p->d_type == DT_DIR) && (isdigit(p->d_name[0]))) {
            const char *name = p->d_name;
            int i = atoi ((name[0] == '0')?name+1:name);
            if (i < oldest) oldest = i;
        }
        p = readdir(dir);
    }
    closedir (dir);
    if (oldest == 9999) return 0; // Found nothing.
    return oldest;
}

static void housemotion_store_delete (const char *parent) {

    char path[1024];
    int oldest = 9999;

    DEBUG ("delete %s\n", parent);
    DIR *dir = opendir (parent);
    if (dir) {
        for (;;) {
            struct dirent *p = readdir(dir);
            if (!p) break;
            if (!strcmp(p->d_name, ".")) continue;
            if (!strcmp(p->d_name, "..")) continue;
            snprintf (path, sizeof(path), "%s/%s", parent, p->d_name);
            if (p->d_type == DT_DIR) {
                housemotion_store_delete (path);
            } else {
                unlink (path);
            }
        }
        closedir (dir);
    }
    rmdir (parent);
}

void housemotion_store_background (time_t now) {

    static time_t lastcheck = 0;

    if (now < lastcheck + 60) return;
    lastcheck = now;
}

