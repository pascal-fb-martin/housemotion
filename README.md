# HouseMotion
The house Motion sidekick service.

## Overview

This service interfaces between the HouseDvr web API and the Motion interfaces.

It fulfills two main roles:
- Report the list of camera handled by motion, and how to access them live.
- Provides the Motion recording files to the HouseDvr services.

This runs as a sidekick to Motion, on the same computer. The goal is to provide an interface to DVR services that is independent of the Motion interfaces: a CCTV service interfacing with a different security camera software should offer the same interface to DVR services.

The HouseMotion service implements the CCTV web API (with additional Motion extensions), and is externally identified as the CCTV service. There can be other implementations of the CCTV service, potentially identifying themselves as CCTV. This brings a restriction, as there can be only one CCTV service running on a given computer. (In theory, two CCTV services could use different URL prefixes. However HouseMotion cannot use the /motion prefix, already used for the Motion daemon itself. This does not leave a lot of relevant prefixes.)

This service also provides an housekeeping function: if the local storage gets too full, the oldest recording files will be deleted. This approach provides enough time for multiple DVR services to upload the recordings before they disappear.

## Command line options

The housemotion service accepts all standard echttp and HousePortal options, plus the following:
* --motion-conf=FILE: the full path to the Motion configuration file.
* --motion-clean=INTEGER: the storage usage limit (percentage) that triggers a cleanup (removal of oldest recording files).

## Motion configuration

This service recovers the following items from the Motion configuration:
* target_dir: root path for the recording files. All files found in this directory and its subdirectories will be offered for download to HouseDvr.
* stream_port: used to build URLs for access to Motion web interface.
* webcontrol_port: used to build URLs for access to Motion web interface.
* camera: used to read each camera configuration file.
* camera_id: used to build the list of cameras handled by this service.

Otherwise, for compatibility with HouseDvr, the `movie_filename` and `picture_filename` items must be set so that recording files are organized in a tree of directories: year / month / day and that all relative file paths are globally unique. One particular issue is when running Motion on multiple servers, feeding the same HouseDvr service: in that case the name of the Motion host should be part of the file name to avoid naming conflicts between servers. For example:

```
text_event %Y/%m/%d/%H:%M:%S
movie_filename %C-%{host}:%t:%v
picture_filename %C-%{host}:%t:%v
```
It is recommended to configure the `on_event_end`, `on_picture_save` and `on_movie_end` items so to notify HouseMotion of new recordings: this will limit the lag between the recording creation and the download by HouseDvr. For example:

```
on_event_end /usr/bin/wget -nd -q -O /dev/null http://localhost/cctv/motion/event\?event=%C
on_picture_save /usr/bin/wget -nd -q -O /dev/null http://localhost/cctv/motion/event\?file=%f
on_movie_end /usr/bin/wget -nd -q -O /dev/null http://localhost/cctv/motion/event\?file=%f
```

The on_event_end item is the one really important, as it also sets the reference time used to determine when a recording file has become _stable_. The others are optional.

## Web API

```
GET /cctv/check
```

This endpoint is a low overhead method for polling for changes. The returned content is a JSON object defined as follows:
* host: the name of the server running this service.
* proxy: the name of the server used for redirection (typically the same as host).
* timestamp: the time of the request/response.
* updated: a 64 bit number that changes when the status has changed.

```
GET /cctv/status
```
This endpoint returns a complete status of the service, as a JSON object defined as follows:
* host: the name of the server running this service.
* proxy: the name of the server used for redirection (typically the same as host).
* timestamp: the time of the request/response.
* updated: a 64 bit number that changes when the status has changed.
* cctv.console: the URL to access the web UI of the motion detection software.
* cctv.feeds: a JSON object where each item is the ID of a camera and the item's value is the URL to access the live video from that camera.
* cctv.available: a string representing the space currently available in the local volume that hosts recordings.
* cctv.total:  string representing the size of the local volume that hosts recordings.
* cctv.used: a string representing the percentage of space used in the local volume that hosts recordings.
* cctv.recordings: an array that lists all recording files currently available. Each file is described using an array: timestamp, relative path, size.
* cctv.metrics: an array that represents a short term history of the available space in RAM and in memory. This is typically used to troubleshoot local storage issues.

This status information is visible in the Status web page.

```
GET /cctv/recording/<path>
```
This endpoint provides access to all current recording files.

```
GET /cctv/motion/event
GET /cctv/motion/event?event=STRING
GET /cctv/motion/event?file=STRING
```
This endpoint is specific to HouseMotion and can be used to notify HouseMotion that new recording files are available. The last two forms are recorded as events. See the next section for more information.

