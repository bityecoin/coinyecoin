#!/usr/bin/env bash
# Cross-compile Coinyecoin Core 2.3.0-fishsticks for 64-bit Windows.
#
# Default:              coinyecoind.exe + coinyecoin-cli.exe
#                       (daemon: wallet + stratum server + RPC)
#
# ./build-windows.sh --gui   also builds coinyecoin-qt.exe
#
# This version automatically fixes the OpenSSL 1.0.2u download problem
# (official site now returns 404, Dogecoin mirror has expired cert).

set -euo pipefail

WITH_GUI=0
for a in "$@"; do [[ "$a" == "--gui" ]] && WITH_GUI=1; done

[[ -f configure.ac ]] || { echo "ERROR: Run this from the coinyecoin source root." >&2; exit 1; }

SUDO="sudo"
[[ $EUID -eq 0 ]] && SUDO=""

export DEBIAN_FRONTEND=noninteractive

echo "==> Installing build dependencies..."
$SUDO apt-get update
$SUDO apt-get install -y \
    build-essential autoconf automake libtool pkg-config \
    bsdmainutils curl git perl python3 g++-mingw-w64-x86-64 zip ca-certificates

# depends needs the POSIX threading model
$SUDO update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix || true
$SUDO update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix || true

# ---------------------------------------------------------------------------
# Ensure depends/ exists
# ---------------------------------------------------------------------------
if [[ ! -f depends/Makefile ]]; then
    echo "==> depends/ build system missing — vendoring from Dogecoin v1.14.9..."
    curl -fL --retry 3 https://codeload.github.com/dogecoin/dogecoin/tar.gz/refs/tags/v1.14.9 \
        | tar xz --strip-components=1 dogecoin-1.14.9/depends

    if [[ ! -f depends/Makefile ]]; then
        echo "ERROR: Failed to vendor depends/ from Dogecoin." >&2
        exit 1
    fi
fi

# ---------------------------------------------------------------------------
# Apply fixes
# ---------------------------------------------------------------------------
echo "==> Applying compatibility patches..."

# BDB atomic fix for modern GCC
if [[ -f depends/packages/bdb.mk ]]; then
    if ! grep -q '__atomic_compare_exchange_db' depends/packages/bdb.mk; then
        sed -i '/define $(package)_preprocess_cmds/a\  sed -i.old '"'"'s/__atomic_compare_exchange\\b/__atomic_compare_exchange_db/'"'"' src/dbinc/atomic.h \&\& \\' \
            depends/packages/bdb.mk || true
    fi
fi

# Fix OpenSSL download path (the main problem you're hitting)
if [[ -f depends/packages/openssl.mk ]]; then
    if grep -q 'www.openssl.org/source/old' depends/packages/openssl.mk; then
        echo "    Patching openssl.mk to use working download mirror..."
        sed -i 's|^$(package)_download_path=.*|$(package)_download_path=https://distfiles.macports.org/openssl10|' \
            depends/packages/openssl.mk
    fi
fi

# ---------------------------------------------------------------------------
# Build depends
# ---------------------------------------------------------------------------
MAKEOPTS="-j$(nproc)"

if [[ $WITH_GUI -eq 1 ]]; then
    GCCV=$(gcc -dumpversion 2>/dev/null | cut -d. -f1 || echo 0)
    if (( GCCV >= 11 )); then
        echo ""
        echo "WARNING: Your host gcc is version $GCCV."
        echo "         Qt 5.7.1 does not build well with GCC >= 11."
        echo "         Recommended: use the Docker ubuntu:20.04 command shown in the header."
        echo ""
        read -r -p "Continue anyway? [y/N] " ans
        [[ "${ans,,}" != "y" ]] && exit 1
    fi
    echo "==> Building depends (with Qt)..."
    make -C depends HOST=x86_64-w64-mingw32 $MAKEOPTS
    GUI_FLAG="--with-gui=qt5"
else
    echo "==> Building depends (daemon only)..."
    make -C depends HOST=x86_64-w64-mingw32 NO_QT=1 $MAKEOPTS
    GUI_FLAG="--without-gui"
fi

# ---------------------------------------------------------------------------
# Build Coinyecoin
# ---------------------------------------------------------------------------
echo "==> Running autogen + configure..."
./autogen.sh

CONFIG_SITE="$PWD/depends/x86_64-w64-mingw32/share/config.site" \
    ./configure \
        --prefix=/ \
        $GUI_FLAG \
        --with-incompatible-bdb \
        --disable-tests \
        --disable-bench

echo "==> Building Coinyecoin..."
make $MAKEOPTS

echo
echo "==> Build complete!"
ls -la src/coinyecoind.exe src/coinyecoin-cli.exe 2>/dev/null || true
ls -la src/qt/coinyecoin-qt.exe 2>/dev/null || true
