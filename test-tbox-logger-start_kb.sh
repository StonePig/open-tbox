#!/bin/sh

VIN=TX866954012345675
ICCID=89860012111654035052
SERVER=47.94.20.34
PORT=10110

mkdir -p /home/yaozhong/open-tbox/conf
mkdir -p /home/yaozhong/open-tbox/log

cp ./tboxparse.xml /home/yaozhong/open-tbox/conf/

sudo ./enablevcan.sh



./src/tbox-logger -N $VIN -I $ICCID \
    --fallback-vehicle-server-host=$SERVER \
    --fallback-vehicle-server-port=$PORT \
    --use-vcan
