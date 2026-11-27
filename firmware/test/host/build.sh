#!/bin/sh
# Bouwt de host-test van de beeldverwerking (na `pio pkg install` voor de JPEGDEC-bron).
cd "$(dirname "$0")/../.."
J=.pio/libdeps/esp32s3/JPEGDEC/src
g++ -O2 -std=c++17 -D__LINUX__ -Isrc -I$J test/host/imaging_test.cpp src/imaging.cpp $J/JPEGDEC.cpp \
    -o test/host/imaging_test
