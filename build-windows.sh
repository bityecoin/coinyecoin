#!/usr/bin/env bash
# Cross-compile Coinyecoin Core 2.3.0-fishsticks for 64-bit Windows on Ubuntu 22.04/24.04.
# Produces: src/qt/coinyecoin-qt.exe (GUI wallet), src/coinyecoind.exe, src/coinyecoin-cli.exe
# First run takes 45-90 min (depends/ builds Qt, BDB, Boost, OpenSSL from source).
set -euo pipefail

sudo apt-get update
sudo apt-get install -y build-essential autoconf automake libtool pkg-config \
    bsdmainutils curl git g++-mingw-w64-x86-64 zip

# depends needs the POSIX threading model
sudo update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
sudo update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix

make -C depends HOST=x86_64-w64-mingw32 -j"$(nproc)"

./autogen.sh
CONFIG_SITE="$PWD/depends/x86_64-w64-mingw32/share/config.site" \
    ./configure --prefix=/ --disable-tests --disable-bench
make -j"$(nproc)"

echo
echo "Windows binaries:"
ls -la src/coinyecoind.exe src/coinyecoin-cli.exe src/qt/coinyecoin-qt.exe
