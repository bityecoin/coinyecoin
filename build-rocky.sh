#!/usr/bin/env bash
# Build Coinyecoin Core 2.3.0-fishsticks on Rocky Linux / RHEL / AlmaLinux 8, 9, or 10.
# Default output: src/coinyecoind + src/coinyecoin-cli (daemon build, wallet enabled).
# Add --gui to attempt the Qt5 wallet as well (see notes at bottom).
#
# EL quirks handled here:
#   - EPEL + CRB/PowerTools enablement for -devel packages
#   - RHEL 9 dropped libdb-cxx and RHEL 10 dropped libdb entirely, so this
#     builds Berkeley DB 5.3.28 from source when system packages are missing.
#     5.3.28 matches Ubuntu's system libdb, so wallet.dat files stay
#     interchangeable with your build-ubuntu.sh binaries (both configure
#     with --with-incompatible-bdb).
#   - gcc 14 (EL10) implicit-declaration hard errors in the old BDB code
set -euo pipefail

WITH_GUI=0
[[ "${1:-}" == "--gui" ]] && WITH_GUI=1

if [[ ! -f configure.ac ]]; then
    echo "Run this from the coinyecoin source root." >&2
    exit 1
fi

EL_VER=$(rpm -E %rhel)
echo "==> EL major version: $EL_VER"

# --- repos -----------------------------------------------------------------
sudo dnf -y install dnf-plugins-core epel-release || true
if [[ "$EL_VER" -ge 9 ]]; then
    sudo dnf config-manager --set-enabled crb || true
else
    sudo dnf config-manager --set-enabled powertools || true
fi

# --- toolchain + libraries ---------------------------------------------------
sudo dnf -y groupinstall "Development Tools" || sudo dnf -y install gcc gcc-c++ make
sudo dnf -y install autoconf automake libtool pkgconf-pkg-config \
    libevent-devel boost-devel openssl-devel util-linux which curl zip

# --- Berkeley DB (wallet database) ------------------------------------------
BDB_PREFIX=""
if sudo dnf -y install libdb-devel libdb-cxx-devel 2>/dev/null; then
    echo "==> Using system Berkeley DB"
else
    echo "==> System BDB C++ bindings unavailable (normal on EL9/EL10); building 5.3.28 from source"
    BDB_PREFIX="$PWD/deps/bdb53"
    mkdir -p deps && pushd deps > /dev/null
    if [[ ! -f db-5.3.28.tar.gz ]]; then
        curl -fLO https://download.oracle.com/berkeley-db/db-5.3.28.tar.gz
    fi
    rm -rf db-5.3.28 && tar xzf db-5.3.28.tar.gz
    pushd db-5.3.28 > /dev/null
    # gcc >= 4.7: BDB defines __atomic_compare_exchange, which clashes with
    # the compiler built-in. Standard rename fix.
    sed -i 's/__atomic_compare_exchange\b/__atomic_compare_exchange_db/g' src/dbinc/atomic.h
    pushd build_unix > /dev/null
    ../dist/configure --enable-cxx --disable-shared --with-pic --prefix="$BDB_PREFIX" \
        CFLAGS="-O2 -fPIC -Wno-implicit-function-declaration -Wno-implicit-int -Wno-int-conversion -Wno-incompatible-pointer-types"
    make -j"$(nproc)"
    make install
    popd > /dev/null; popd > /dev/null; popd > /dev/null
fi

# --- optional Qt GUI deps -----------------------------------------------------
GUI_FLAG="--without-gui"
if [[ $WITH_GUI -eq 1 ]]; then
    if sudo dnf -y install qt5-qtbase-devel qt5-qttools-devel protobuf-devel qrencode-devel; then
        GUI_FLAG="--with-gui=qt5"
        PB_VER=$(pkg-config --modversion protobuf 2>/dev/null || echo 0)
        case "$PB_VER" in
            3.2[2-9]*|3.[3-9]*|4.*|2[2-9].*)
                echo "WARNING: protobuf $PB_VER requires C++17 and will not link against this"
                echo "         C++11 codebase. Build the GUI on Ubuntu (build-ubuntu.sh) or via"
                echo "         the depends system instead. Continuing with daemon-only."
                GUI_FLAG="--without-gui"
                ;;
        esac
    else
        echo "WARNING: Qt5 devel packages not available on this EL release; building daemon only."
    fi
fi

# --- configure + make: exact flag set proven on the 2.3.0-fishsticks build ---
./autogen.sh

EXTRA_CPPFLAGS=""
EXTRA_LDFLAGS=""
if [[ -n "$BDB_PREFIX" ]]; then
    EXTRA_CPPFLAGS="-I$BDB_PREFIX/include"
    EXTRA_LDFLAGS="-L$BDB_PREFIX/lib"
fi

./configure $GUI_FLAG \
    --with-incompatible-bdb \
    --disable-tests --disable-bench \
    --without-miniupnpc --disable-zmq --disable-hardening \
    CPPFLAGS="$EXTRA_CPPFLAGS" \
    LDFLAGS="$EXTRA_LDFLAGS" \
    CXXFLAGS="-O2 -std=c++11 -DBOOST_BIND_GLOBAL_PLACEHOLDERS -Wno-deprecated-declarations -Wno-deprecated-copy -Wno-maybe-uninitialized -Wno-cpp"

make -j"$(nproc)"

echo
echo "==> Build complete:"
ls -la src/coinyecoind src/coinyecoin-cli
[[ -f src/qt/coinyecoin-qt ]] && ls -la src/qt/coinyecoin-qt

cat << 'DONE'

Install:
    sudo install -m 0755 src/coinyecoind src/coinyecoin-cli /usr/local/bin/

Open the stratum port to your miners (firewalld):
    sudo firewall-cmd --permanent --add-port=3333/tcp && sudo firewall-cmd --reload

Systemd unit and config profiles: contrib/fishsticks/
DONE
