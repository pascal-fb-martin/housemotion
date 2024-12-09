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

## Web API

```
/cctv/check
```
Return a millisecond timestamp representing when the last change to any reported data has occurred. This method allows for a low overhead periodic polling of the service. The client should not use the returned timestamp for anything else than comparing it with a previous returned value. If the timestamp value has changed, regardless how, the client should query the repository content again.

```
/cctv/status
```
Reports the current Motion configuration and local storage space status, in JSON. This status lists both all cameras locally handled by Motion and all recording files currently stored on that feed server.

```
/cctv/recording/<file>
```
This returns the content of the specified recording file.

```
/cctv/motion/event
```
Report the end of a Motion event to HouseMotion. This may cause the service to scan the recording folders to search for new files.

