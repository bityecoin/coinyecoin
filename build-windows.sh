#!/usr/bin/env bash
# Cross-compile Coinyecoin Core 2.3.0-fishsticks for 64-bit Windows.
#
# Default:              coinyecoind.exe + coinyecoin-cli.exe
#                       (daemon: wallet + stratum server + RPC, everything
#                        solo mining needs, no GUI)
# ./build-windows.sh --gui   also builds coinyecoin-qt.exe
#
# The fork's source zip shipped without the depends/ build system. This script
# vendors it from upstream Dogecoin v1.14.9 automatically if it is missing.
#
# TOOLCHAIN REALITY: depends pins Qt 5.7.1 (2016). Ubuntu 24.04's mingw
# gcc 13 will not compile Qt 5.7.1, so run the GUI build inside an
# ubuntu:20.04 container (mingw gcc 9, period-correct):
#
#     docker run --rm -v "$PWD":/src -w /src ubuntu:20.04 \
#         bash -c "./build-windows.sh --gui"
#
# The daemon-only default cross-compiles fine on 22.04/24.04.
# First run: 20-40 min daemon-only, 60-90 min with Qt.
set -euo pipefail

WITH_GUI=0
for a in "$@"; do [[ "$a" == "--gui" ]] && WITH_GUI=1; done

[[ -f configure.ac ]] || { echo "Run this from the coinyecoin source root." >&2; exit 1; }

SUDO="sudo"; [[ $EUID -eq 0 ]] && SUDO=""

export DEBIAN_FRONTEND=noninteractive
$SUDO apt-get update
$SUDO apt-get install -y build-essential autoconf automake libtool pkg-config \
    bsdmainutils curl git perl python3 g++-mingw-w64-x86-64 zip ca-certificates

# depends needs the POSIX threading model
$SUDO update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
$SUDO update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix

# --- vendor depends/ from Dogecoin v1.14.9 if this tree lacks it ------------
if [[ ! -f depends/Makefile ]]; then
    echo "==> depends/ build system missing; vendoring from Dogecoin v1.14.9"
    curl -fL https://codeload.github.com/dogecoin/dogecoin/tar.gz/refs/tags/v1.14.9 \
        | tar xz --strip-components=1 dogecoin-1.14.9/depends
    # gcc >= 4.7: BDB's __atomic_compare_exchange clashes with the built-in
    grep -q '__atomic_compare_exchange_db' depends/packages/bdb.mk || \
    sed -i '/define $(package)_preprocess_cmds/a\  sed -i.old '"'"'s/__atomic_compare_exchange\\b/__atomic_compare_exchange_db/'"'"' src/dbinc/atomic.h \&\& \\' depends/packages/bdb.mk 2>/dev/null || true
fi

MAKEOPTS="-j$(nproc)"
if [[ $WITH_GUI -eq 1 ]]; then
    GCCV=$(gcc -dumpversion | cut -d. -f1)
    if (( GCCV >= 11 )); then
        echo "WARNING: host gcc $GCCV will almost certainly fail on Qt 5.7.1."
        echo "         Use the ubuntu:20.04 container (see this script's header)."
    fi
    make -C depends HOST=x86_64-w64-mingw32 $MAKEOPTS
    GUI_FLAG="--with-gui=qt5"
else
    make -C depends HOST=x86_64-w64-mingw32 NO_QT=1 $MAKEOPTS
    GUI_FLAG="--without-gui"
fi

./autogen.sh
CONFIG_SITE="$PWD/depends/x86_64-w64-mingw32/share/config.site" \
    ./configure --prefix=/ $GUI_FLAG --with-incompatible-bdb \
    --disable-tests --disable-bench
make $MAKEOPTS

echo
echo "==> Windows binaries:"
ls -la src/coinyecoind.exe src/coinyecoin-cli.exe 2>/dev/null || true
ls -la src/qt/coinyecoin-qt.exe 2>/dev/null || true
