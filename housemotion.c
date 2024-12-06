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
 * housemotion.c - Main loop of the HouseMotion program.
 *
 * SYNOPSYS:
 *
 * This program fulfill two purposes:
 *
 * - It analyzes the current Motion's configuration and reports the cameras
 *   to HouseDvr (used for the HouseDvr live view).
 *
 * - It retrieves video and image files stored by Motion after an event
 *   has been triggered. It schedules and performs the transfer of these files
 *   to HouseDvr, the video storage service.
 *
 * Event notifications are received as HTTP requests, through shell scripts
 * activated by Motion.
 *
 * This service also implements a web UI, which is more intended for
 * troubleshooting and monitoring.
 *
 * This service implements the "cctv" web API. This name reflects that it
 * reports on CCTV cameras and their associated production.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "echttp.h"
#include "echttp_cors.h"
// #include "echttp_json.h"
#include "echttp_static.h"

#include "houseportalclient.h"
#include "housediscover.h"
#include "houselog.h"

#include "housemotion_feed.h"
#include "housemotion_store.h"

static int use_houseportal = 0;
static char HostName[256];


static const char *housemotion_status (const char *method, const char *uri,
                                       const char *data, int length) {
    static char buffer[65537];
    int cursor = 0;

    cursor += snprintf (buffer, sizeof(buffer),
                        "{\"host\":\"%s\",\"proxy\":\"%s\",\"timestamp\":%d,"
                            "\"cctv\":{",
                        HostName, houseportal_server(), (long)time(0));

    cursor += housemotion_feed_status (buffer+cursor, sizeof(buffer)-cursor);
    cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor, ",");
    cursor += housemotion_store_status (buffer+cursor, sizeof(buffer)-cursor);
    cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor, "}}");
    echttp_content_type_json ();
    return buffer;
}

static void housemotion_background (int fd, int mode) {

    static time_t LastRenewal = 0;
    time_t now = time(0);

    if (use_houseportal) {
        static const char *path[] = {"cctv:/cctv"};
        if (now >= LastRenewal + 60) {
            if (LastRenewal > 0)
                houseportal_renew();
            else
                houseportal_register (echttp_port(4), path, 1);
            LastRenewal = now;
        }
    }
    housemotion_store_background(now);
    housemotion_feed_background(now);

    housediscover (now);
    houselog_background (now);
}

static void housemotion_protect (const char *method, const char *uri) {
    echttp_cors_protect(method, uri);
}

int main (int argc, const char **argv) {

    const char *error;

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    signal(SIGPIPE, SIG_IGN);

    gethostname (HostName, sizeof(HostName));

    echttp_default ("-http-service=dynamic");

    argc = echttp_open (argc, argv);
    if (echttp_dynamic_port()) {
        houseportal_initialize (argc, argv);
        use_houseportal = 1;
    }
    housediscover_initialize (argc, argv);
    houselog_initialize ("cctv", argc, argv);

    echttp_cors_allow_method("GET");
    echttp_protect (0, housemotion_protect);

    housemotion_feed_initialize (argc, argv);
    housemotion_store_initialize (argc, argv);

    echttp_route_uri ("/cctv/status", housemotion_status);
    echttp_static_route ("/", "/usr/local/share/house/public");

    echttp_background (&housemotion_background);

    houselog_event ("SERVICE", "motion", "START", "ON %s", HostName);
    echttp_loop();
}

