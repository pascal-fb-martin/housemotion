#!/bin/bash
cd `dirname $0`
current=`pwd`
root=`dirname $current`/donotcommit
exec ../housemotion --motion-conf=$root/motion/motion.conf

