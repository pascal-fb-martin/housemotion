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
 * void housemotion_store_location (const char *directory);
 *
 *    Set the location of the Motion recording files, i.e. the directory
 *    where Motion stores them. This may be called multiple times if the
 *    Motion configuration is changed..
 *
 * void housemotion_store_background (time_t now);
 *
 *    The periodic function that manages the video storage.
 *
 * long long housemotion_store_check (void);
 *
 *    Return a timestamp value. Any increase to that value indicates that
 *    some data reported in status may have changed.
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
#include <errno.h>

#include <echttp.h>
#include <echttp_static.h>
#include <echttp_json.h>

#include "houselog.h"
#include "housemotion_store.h"

#define DEBUG if (echttp_isdebug()) printf

static int HouseMotionMaxSpace = 0; // Default is no automatic cleanup.

static char *HouseMotionStorage = 0;
static time_t HouseMotionChanged = 0;

struct HouseMotionEvent {
    time_t timestamp;
    char   id[32];
};

#define MOTION_EVENT_DEPTH 8
static struct HouseMotionEvent HouseMotionRecentEvents[MOTION_EVENT_DEPTH];
static int HouseMotionEventCursor = 0;


static const char *housemotion_store_record (const char *stage,
                                             const char *data, int length) {

    const char *event = echttp_parameter_get ("event");
    const char *cam = echttp_parameter_get ("camera");
    const char *cat = "CAMERA";
    if (!cam) {
        cat = "DETECTION";
        cam = "cctv";
    }
    if (event) {
        houselog_event (cat, cam, stage, "EVENT %s", event);
        return event;
    }
    const char *file = echttp_parameter_get ("file");
    if (file) {
        houselog_event (cat, cam, "FILE", "%s", file);
    }
    return 0;
}

static void housemotion_store_complete (const char *event) {

    time_t now = time(0);
    HouseMotionRecentEvents[HouseMotionEventCursor].timestamp = now;
    snprintf (HouseMotionRecentEvents[HouseMotionEventCursor].id,
              sizeof(HouseMotionRecentEvents[0].id),
              "%s", event);
    if (++HouseMotionEventCursor >= MOTION_EVENT_DEPTH)
        HouseMotionEventCursor = 0;
    HouseMotionChanged = now;
}

static const char *housemotion_store_start (const char *method, const char *uri,
                                            const char *data, int length) {
    housemotion_store_record ("START", data, length);
    return 0;
}

static const char *housemotion_store_end (const char *method, const char *uri,
                                            const char *data, int length) {
    const char *event = housemotion_store_record ("END", data, length);
    if (event) housemotion_store_complete (event);
    return 0;
}

static const char *housemotion_store_event (const char *method, const char *uri,
                                            const char *data, int length) {
    const char *event = housemotion_store_record ("EVENT", data, length);
    if (event) housemotion_store_complete (event);
    return 0;
}

static time_t housemotion_store_matchevent (const char *name) {
    int i;
    for (i = MOTION_EVENT_DEPTH-1; i >= 0; --i) {
        if (strstr (name, HouseMotionRecentEvents[i].id))
            return HouseMotionRecentEvents[i].timestamp;
    }
    return 0;
}

void housemotion_store_initialize (int argc, const char **argv) {

    int i;
    const char *max = 0;

    for (i = 1; i < argc; ++i) {
        echttp_option_match ("-motion-clean=", argv[i], &max);
    }
    if (max) {
        HouseMotionMaxSpace = atoi(max);
    }
    echttp_route_uri ("/cctv/motion/event", housemotion_store_event);
    echttp_route_uri ("/cctv/motion/event/end", housemotion_store_end);
    echttp_route_uri ("/cctv/motion/event/start", housemotion_store_start);
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

    if (value >= 1024*1024*1024) {
        snprintf (buffer, size, "%d.%01dGB",
                  (int)(value / (1024*1024*1024)),
                  (int)((value % 1024*1024*1024) / 1024*1024*100));
    } else if (value >= 1024*1024) {
        snprintf (buffer, size, "%d.%01dMB",
                  (int)(value / (1024*1024)),
                  (int)((value % 1024*1024) / (1024*100)));
    } else {
        snprintf (buffer, size, "%dKB", (int)(value / 1024));
    }
}

long long housemotion_store_check (void) {
    // We report a changed timestamp whever a new event has been reported
    // or when a Motion configuration has impacted recordings.
    if (!HouseMotionChanged) HouseMotionChanged = time(0);
    return (long long)HouseMotionChanged * 1000;
}

int housemotion_store_status_recurse (char *buffer, int size,
                                      char *path, int psize, const char *sep) {

    int cursor = 0;
    time_t now = time(0);

    DIR *dir = opendir (path);
    if (dir) {
        int offset = strlen(path);
        char *base = path + offset;
        *(base++) = '/';
        int basesize = psize - offset - 1;
        char *relative = path + strlen(HouseMotionStorage) + 1;

        struct dirent *p;
        for (p = readdir(dir); p; p = readdir(dir)) {
            int saved = cursor;
            if (p->d_name[0] == '.') continue;
            snprintf (base, basesize, "%s", p->d_name);
            if (p->d_type == DT_REG) {
                struct stat filestat;
                if (stat (path, &filestat)) continue; // Cannot access, skip.

                // A file is considered stable if last update was a minute ago,
                // or else if it matches a detected event (and has not changed
                // since then--just to be safe).
                //
                int stable = (filestat.st_mtime < (now - 60));
                if (! stable) {
                    time_t eventtime = housemotion_store_matchevent (relative);
                    stable = (filestat.st_mtime <= eventtime);
                }

                cursor += snprintf (buffer+cursor, size-cursor,
                                    "%s[%lld,\"%s\",%lld,%s]",
                                    sep,
                                    (long long)(filestat.st_mtime),
                                    relative,
                                    (long long)(filestat.st_size),
                                    stable?"true":"false");
                if (cursor >= size) {
                    closedir (dir);
                    return saved;
                }
            } else if (p->d_type == DT_DIR) {
                cursor += housemotion_store_status_recurse
                             (buffer+cursor, size-cursor, path, psize, sep);
            }
            if (cursor > 0) sep = ",";
        }
        closedir (dir);
    }
    return cursor;
}

int housemotion_store_status (char *buffer, int size) {

    int cursor = 0;

    struct statvfs storage;

    if (!HouseMotionStorage) return 0;

    if (statvfs (HouseMotionStorage, &storage)) return 0;

    cursor += snprintf (buffer, size, "\"path\":\"%s\"", HouseMotionStorage);
    if (cursor >= size) goto overflow;

    char ascii[64];
    housemotion_store_friendly (ascii, sizeof(ascii),
                                housemotion_store_free (&storage));
    cursor += snprintf (buffer+cursor, size-cursor, ",\"available\":\"%s\"", ascii);
    if (cursor >= size) goto overflow;
    housemotion_store_friendly (ascii, sizeof(ascii),
                                housemotion_store_total (&storage));
    cursor += snprintf (buffer+cursor, size-cursor, ",\"total\":\"%s\"", ascii);
    if (cursor >= size) goto overflow;
    cursor += snprintf (buffer+cursor, size-cursor, ",\"used\":\"%d%%\"",
                        housemotion_store_used (&storage));
    if (cursor >= size) goto overflow;

    cursor += snprintf (buffer+cursor, size-cursor, ",\"recordings\":[");
    if (cursor >= size) goto overflow;
    char path[1024];
    snprintf (path, sizeof(path), "%s", HouseMotionStorage);
    cursor += housemotion_store_status_recurse
                  (buffer+cursor, size-cursor, path, sizeof(path), "");
    cursor += snprintf (buffer+cursor, size-cursor, "]");

    return cursor;

overflow:
    houselog_trace (HOUSE_FAILURE, "BUFFER", "overflow");
    buffer[0] = 0;
    return 0;
}

struct filetrack {
    time_t modified;
    char path[1024];
};

void housemotion_store_oldest (struct filetrack *oldest, const char *parent) {

    char path[1024];
    struct dirent *p;
    DIR *dir = opendir (parent);
    if (!dir) return;

    for (p = readdir(dir); p; p = readdir(dir)) {
        if (p->d_name[0] == '.') continue;
        snprintf (path, sizeof(path), "%s/%s", parent, p->d_name);
        if (p->d_type == DT_REG) {
            struct stat filestat;
            if (stat (path, &filestat)) {
                houselog_trace (HOUSE_FAILURE, "stat(2)",
                                "%s: %s", path, strerror(errno));
                continue; // Cannot access, skip.
            }
            if (filestat.st_mtime < oldest->modified) {
                snprintf (oldest->path, sizeof(oldest->path), "%s", path);
                oldest->modified = filestat.st_mtime;
            }
        } else if (p->d_type == DT_DIR) {
            housemotion_store_oldest (oldest, path);
        }
    }
    closedir (dir);
}

static void housemotion_store_cleanup (time_t now) {

    // Delete the oldest file.
    //
    struct filetrack oldest;
    oldest.modified = now + 60;
    oldest.path[0] = 0;
    housemotion_store_oldest (&oldest, HouseMotionStorage);
    if (oldest.modified < now) {
        houselog_event ("SERVICE", "cctv", "DELETE", "%s", oldest.path);
        if (unlink (oldest.path)) {
            houselog_trace (HOUSE_FAILURE, "unlink(2)",
                            "%s: %s", oldest.path, strerror(errno));
        }
        char *s = strrchr (oldest.path, '/');
        if (s) {
            *s = 0;
            rmdir (oldest.path); // Nothing done if the directory is not empty.
        }
    }
}

static void housemotion_store_monitor (time_t now) {

    if (!HouseMotionStorage) return;

    struct statvfs storage;
    if (statvfs (HouseMotionStorage, &storage)) return ;

    if ((HouseMotionMaxSpace > 0) &&
        (housemotion_store_used (&storage) >= HouseMotionMaxSpace)) {
        housemotion_store_cleanup (now);
    }
}

void housemotion_store_location (const char *directory) {

    char *existing = HouseMotionStorage;
    if (existing && (!strcmp(existing, directory))) return; // No change.

    HouseMotionStorage = strdup (directory);
    echttp_static_route ("/cctv/recording", HouseMotionStorage);
    if (existing) free (existing);

    HouseMotionChanged = time(0);
}

void housemotion_store_background (time_t now) {

    static time_t Nextcheck = 0;

    if (now <= Nextcheck) return;
    Nextcheck = now + 10;

    housemotion_store_monitor (now);
}

