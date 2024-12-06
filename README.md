# HouseMotion
The house Motion sidekick service.

## Overview

This service interfaces between the HouseDvr web API and the Motion interfaces.

It fulfills two main roles:
- Report the list of camera handled by motion, and how to access them live.
- Transfer the Motion recording to the HouseDvr service.

This runs as a sidekick to Motion, on the same computer.

The HouseMotion service implements the CCTV web API (with additional Motion extensions), and is externally identified as the CCTV service. There can be other implementations of the CCTV service, potentially identifying themselves as CCTV. This brings a restriction, as there can be only one CCTV service running on a given computer. (In theory, two CCTV services could use different URL prefixes. However HouseMotion cannot use the /motion prefix, already used for the Motion daemon itself. This does not leave a lot of relevant prefixes.)

## Web API

```
/cctv/status
```
Reports the current Motion configuration and local storage space status.

```
/cctv/motion/event?id=STRING
```
Report the end of a Motion event to HouseMotion. The id parameter should be just enough to find all files associated with the event, but only files associated with the event.

```
/cctv/motion/file?name=STRING
```
Report an event file as available. This might be a video recording or a picture file (HouseMotion does not differentiate: it sends both to HouseDvr).

