#!/usr/bin/env bash
set -euo pipefail

echo "Install build dependencies first (Ubuntu 24.04 example):"
echo "  sudo apt update"
echo "  sudo apt install -y \
    build-essential autoconf automake libtool pkg-config bsdmainutils curl git \
    libevent-dev libboost-system-dev libboost-filesystem-dev libboost-chrono-dev \
    libboost-test-dev libboost-thread-dev libboost-program-options-dev \
    libboost-dev libsqlite3-dev libminiupnpc-dev libzmq3-dev libqrencode-dev \
    qtbase5-dev qttools5-dev qttools5-dev-tools libqt5svg5-dev libprotobuf-dev \
    protobuf-compiler libssl-dev libdb++-dev libdb-dev"

chmod +x autogen.sh || true
./autogen.sh
./configure --with-gui=qt5 --with-incompatible-bdb --disable-tests --disable-bench \
  CXXFLAGS="-std=c++11 -DBOOST_BIND_GLOBAL_PLACEHOLDERS -Wno-deprecated-copy -Wno-maybe-uninitialized -Wno-cpp"
make -j"$(nproc)"
