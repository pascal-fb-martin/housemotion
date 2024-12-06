/* HouseMotion - a web server to handle videos files created by Motion.
 *
 * Copyright 2020, Pascal Martin
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
 * housemotion_feed.c - Handle the list of cameras managed by the local motion
 *
 * SYNOPSYS:
 *
 * This module detects which cameras as managed by the local motion service
 * and reports them to HouseDvr (on request).
 *
 * This module is not configured by the user: it learns about motion's cameras
 * on its own.
 *
 * void housemotion_feed_initialize (int argc, const char **argv);
 *
 *    Initialize this module.
 *
 * int housemotion_feed_status (char *buffer, int size);
 *
 *    Return a JSON string that represents the status of the known feeds.
 *
 * void housemotion_feed_background (time_t now);
 *
 *    The periodic function that detect any possible Motion configuration
 *    changes.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/time.h>
#include <unistd.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"

#include "housemotion_feed.h"

#define DEBUG if (echttp_isdebug()) printf

typedef struct {
    char  *id;
    char  *name;
    char  *url;
} FeedRegistration;

static FeedRegistration *Feeds = 0;
static int               FeedsCount = 0;
static int               FeedsSize = 0;

static const char *HouseMotionConf = "/etc/motion/motion.conf";
static char *HouseMotionControlPort = 0;
static char *HouseMotionStreamPort = 0;

static char HouseMotionHost[256];
static time_t LastConfigLoad = 0;

static void housemotion_feed_replace (char **var, const char *value) {
    if (*var) free(*var);
    if (value) *var = strdup(value);
    else       *var = 0;
}

static void housemotion_feed_add_camera (char *id, char *name) {

    if (FeedsCount >= FeedsSize) {
        FeedsSize += 16;
        Feeds = realloc (Feeds, FeedsSize * sizeof(FeedRegistration));
    }
    Feeds[FeedsCount].id = id;
    Feeds[FeedsCount].name = name;

    char url[1024];
    snprintf (url, sizeof(url), "http://%s:%s/%s/stream",
              HouseMotionHost, HouseMotionStreamPort, id);
    Feeds[FeedsCount].url = strdup(url);

    FeedsCount += 1;
}

static void housemotion_feed_clear_camera (void) {

    int i;
    for (i = 0; i < FeedsCount; ++i) {
        housemotion_feed_replace (&(Feeds[i].id), 0);
        housemotion_feed_replace (&(Feeds[i].name), 0);
        housemotion_feed_replace (&(Feeds[i].url), 0);
    }
    FeedsCount = 0;
}

int housemotion_feed_status (char *buffer, int size) {

    int i;
    int cursor = 0;
    const char *prefix = "";

    cursor += snprintf (buffer+cursor, size-cursor, "\"console\":\"%s:%s\"",
                        HouseMotionHost, HouseMotionControlPort);
    if (cursor >= size) goto overflow;

    cursor += snprintf (buffer+cursor, size-cursor, ",\"feeds\":{");
    if (cursor >= size) goto overflow;

    for (i = 0; i < FeedsCount; ++i) {

        cursor += snprintf (buffer+cursor, size-cursor, "%s\"%s\":\"%s\"",
                            prefix, Feeds[i].id, Feeds[i].url);
        if (cursor >= size) goto overflow;
        prefix = ",";
    }

    cursor += snprintf (buffer+cursor, size-cursor, "}");
    if (cursor >= size) goto overflow;

    return cursor;

overflow:
    houselog_trace (HOUSE_FAILURE, "BUFFER", "overflow");
    echttp_error (413, "Payload too large");
    buffer[0] = 0;
    return 0;
}

static char *housemotion_feed_skipempty (char *data) {
    while (isblank(*data)) data += 1; // Skip space and tab.
    // Ignore empty lines and comments.
    if (*data < ' ') return 0; // Ignore end of line, empty string.
    if (*data == '#') return 0; // Ignore comments.
    if (*data == ';') return 0; // Ignore comments.
    return data;
}

static char *housemotion_feed_get_value (const char *name, char *data) {

    int length = strlen(name);
    if (strncmp (data, name, length)) return 0;
    data += length;
    if (!isblank(*data)) return 0;

    while (isblank(*(++data))) ; // Skip space and tab separators.
    char *eol = data;
    while (*eol >= ' ') eol += 1; // Skip to the end of the line.
    *eol = 0;
    return data;
}

static void housemotion_feed_read_camera (const char *filename) {

    char buffer[1024];
    FILE *fd = fopen (filename, "r");
    if (!fd) return;

    char *camid = 0;
    char *camname = 0;

    while (!feof(fd)) {
        char *data = fgets (buffer, sizeof(buffer), fd);
        if (!data) break;
        data = housemotion_feed_skipempty (data);
        if (!data) continue;

        char *value = housemotion_feed_get_value ("camera_id", data);
        if (value) {
            housemotion_feed_replace (&camid, value);
            continue;
        }

        value = housemotion_feed_get_value ("camera_name", data);
        if (value) {
            housemotion_feed_replace (&camname, value);
            continue;
        }
    }

    if (camname && camid) {
        housemotion_feed_add_camera (camid, camname);
    } else {
        // Ignore any incomplete configuration.
        if (camname) free(camname);
        if (camid) free(camid);
    }
    fclose(fd);
}

static void housemotion_feed_read_configuration (void) {

    char buffer[1024];
    FILE *fd = fopen (HouseMotionConf, "r");
    if (!fd) return;

    while (!feof(fd)) {
        char *data = fgets (buffer, sizeof(buffer), fd);
        if (!data) break;
        data = housemotion_feed_skipempty (data);
        if (!data) continue;

        char *value = housemotion_feed_get_value ("camera", data);
        if (value) {
            housemotion_feed_read_camera (value);
            continue;
        }
        value = housemotion_feed_get_value ("webcontrol_port", data);
        if (value) {
            housemotion_feed_replace (&HouseMotionControlPort, value);
            continue;
        }
        value = housemotion_feed_get_value ("stream_port", data);
        if (value) {
            housemotion_feed_replace (&HouseMotionStreamPort, value);
            continue;
        }
    }
    if (!HouseMotionControlPort) HouseMotionControlPort = strdup("8080");
    if (!HouseMotionStreamPort) HouseMotionStreamPort = strdup("8081");

    fclose(fd);
    LastConfigLoad = time(0);
}

void housemotion_feed_initialize (int argc, const char **argv) {

    int i;
    for (i = 1; i < argc; ++i) {
        echttp_option_match ("-motion-conf=", argv[i], &HouseMotionConf);
    }
    gethostname (HouseMotionHost, sizeof(HouseMotionHost));
    housemotion_feed_read_configuration ();
}

static void housemotion_feed_scan_configuration (time_t now) {

    if (now < LastConfigLoad + 300) return;

    // For now, re-read the configuration files.
    // TBD: use the Motion web API to get the live configuration.
    housemotion_feed_clear_camera();
    housemotion_feed_read_configuration();
}

void housemotion_feed_background (time_t now) {

    housemotion_feed_scan_configuration (now);
}

