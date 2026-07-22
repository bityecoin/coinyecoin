#!/usr/bin/env bash
# =============================================================================
# build-macos.sh
# Cross-compile Coinyecoin Core 2.3.0-fishsticks for macOS from Linux:
#   Coinyecoin-Qt.app          (GUI wallet bundle)
#   Coinyecoin-Core.dmg        (drag-to-Applications disk image, via make deploy)
#   src/coinyecoind            (daemon)   src/coinyecoin-cli (CLI)
#
# WHY CROSS-COMPILE (vs building on a real Mac):
#   This is a 2016-era codebase whose depends pin Qt 5.7.1 / boost / openssl to
#   the exact versions macOS 10.11 shipped against. Cross-compiling with the
#   pinned SDK reproduces that world far more reliably than a modern Mac +
#   modern Xcode, and it reuses the same depends system that already builds your
#   Windows wallet. It also runs on the Linux box you already have.
#
# THE ONE PREREQUISITE YOU MUST PROVIDE — the macOS SDK:
#   Apple's license forbids redistributing their SDK, so it can't ship in the
#   repo or be downloaded here. You extract it once from Xcode 7.3.1:
#     1. Get "Xcode 7.3.1.dmg" from https://developer.apple.com/download/all/
#        (free Apple ID). On a Mac, or with the tools below on Linux.
#     2. ./contrib/macdeploy/extract-osx-sdk.sh    (produces MacOSX10.11.sdk.tar.gz)
#        - on Linux you need: p7zip-full  (to open the .dmg/.xip/.pkg)
#     3. mkdir -p depends/SDKs && tar -C depends/SDKs -xf MacOSX10.11.sdk.tar.gz
#   You end up with:  depends/SDKs/MacOSX10.11.sdk/   <- this script checks for it.
#
# HOW TO RUN (from the source root, once the SDK is in place):
#   docker run --rm -v "$PWD":/src -w /src ubuntu:20.04 bash /src/build-macos.sh
#
# HEADS-UP: like the Windows port, expect a fix-chain on the first go — a 2016
# tree meeting a newer toolchain surfaces a few errors. Paste them and iterate.
# =============================================================================
set -euo pipefail

[[ -f configure.ac ]] || { echo "ERROR: run from the coinyecoin source root." >&2; exit 1; }

HOST=x86_64-apple-darwin11
SDK_PATH="$PWD/depends/SDKs"
SDK_DIR="$SDK_PATH/MacOSX10.11.sdk"

# ---- job count: leave a core free + cap by RAM (same policy as Windows) ----
CORES="$(nproc)"
JOBS="$CORES"; [ "$JOBS" -gt 1 ] && JOBS=$((JOBS - 1))
MEM_GB="$(awk '/MemTotal/{printf "%d",$2/1024/1024}' /proc/meminfo 2>/dev/null || echo 4)"
MEM_JOBS=$(( MEM_GB / 2 )); [ "$MEM_JOBS" -lt 1 ] && MEM_JOBS=1
[ "$JOBS" -gt "$MEM_JOBS" ] && JOBS="$MEM_JOBS"
NICE="nice -n 15"; command -v ionice >/dev/null 2>&1 && NICE="ionice -c3 $NICE"
echo "==> building with -j$JOBS ($CORES cores, ~${MEM_GB}GB RAM), low priority"
export DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC

# ---- persistent ccache on the mounted volume (survives --rm) ----
export CCACHE_DIR="${CCACHE_DIR:-$PWD/.ccache-macos}"
export CCACHE_COMPILERCHECK=content
export CCACHE_MAXSIZE=8G
mkdir -p "$CCACHE_DIR"
echo "==> ccache dir: $CCACHE_DIR (persists across runs)"

echo "==> [1/6] Checking for the macOS SDK..."
if [ ! -d "$SDK_DIR" ]; then
    cat >&2 <<EOF

ERROR: macOS SDK not found at:
    $SDK_DIR

I can't include Apple's SDK (their license forbids redistribution). Extract it
once from Xcode 7.3.1, then place it there:

    ./contrib/macdeploy/extract-osx-sdk.sh          # -> MacOSX10.11.sdk.tar.gz
    mkdir -p depends/SDKs
    tar -C depends/SDKs -xf MacOSX10.11.sdk.tar.gz

(See the header of this script for the full walkthrough.) Then re-run.
EOF
    exit 1
fi
echo "    found: $SDK_DIR"

echo "==> [2/6] Installing the darwin cross toolchain + dmg tooling..."
apt-get update -qq
apt-get install -y -qq \
    build-essential autoconf automake libtool pkg-config bsdmainutils \
    curl git perl python3 python3-setuptools zip ca-certificates cmake \
    clang llvm-dev libz-dev libbz2-dev libssl-dev uuid-dev \
    librsvg2-bin libtiff-tools imagemagick fonts-tuffy \
    genisoimage p7zip-full tzdata

echo "==> [3/6] Patching depends (dead OpenSSL + LLVM mirrors; BDB atomic clash)..."
python3 - <<'PY'
import os, re

# native_cctools pulls a prebuilt clang from releases.llvm.org, which LLVM
# retired — downloads now live on GitHub. Repoint it (same tarball => same hash,
# so no hash change needed). Without this the darwin toolchain build dies early.
p = "depends/packages/native_cctools.mk"
if os.path.exists(p):
    s = open(p).read()
    if "releases.llvm.org" in s:
        s = s.replace(
            "$(package)_clang_download_path=https://releases.llvm.org/$($(package)_clang_version)",
            "$(package)_clang_download_path=https://github.com/llvm/llvm-project/releases/download/llvmorg-$($(package)_clang_version)")
        open(p, "w").write(s)
        print("    native_cctools.mk: clang download repointed to GitHub (llvmorg-*)")
    else:
        print("    native_cctools.mk: no releases.llvm.org URL, leaving as-is")

p = "depends/packages/openssl.mk"
if os.path.exists(p):
    s = open(p).read()
    if "www.openssl.org/source/old" in s:
        s = re.sub(r'^\$\(package\)_download_path=.*$',
                   '$(package)_download_path=https://distfiles.macports.org/openssl10',
                   s, flags=re.M)
        open(p, "w").write(s); print("    openssl.mk: repointed to macports mirror")
    else:
        print("    openssl.mk: already patched / different mirror")
p = "depends/packages/bdb.mk"
if os.path.exists(p):
    s = open(p).read()
    if "__atomic_compare_exchange_db" not in s:
        needle = "define $(package)_preprocess_cmds"
        i = s.find(needle)
        if i != -1:
            eol = s.find("\n", i) + 1
            fix = ("  sed -i.old 's/__atomic_compare_exchange\\b/"
                   "__atomic_compare_exchange_db/' src/dbinc/atomic.h && \\\n")
            s = s[:eol] + fix + s[eol:]
            open(p, "w").write(s); print("    bdb.mk: atomic symbol rename inserted")
    else:
        print("    bdb.mk: already patched")
PY

echo "==> [4/6] Building depends WITH Qt for darwin (first run is the slow part)..."
$NICE make -C depends HOST="$HOST" SDK_PATH="$SDK_PATH" -j"$JOBS"

echo "==> [5/6] autogen + configure (GUI enabled) + build..."
make distclean >/dev/null 2>&1 || true
./autogen.sh
CONFIG_SITE="$PWD/depends/$HOST/share/config.site" \
    ./configure \
        --prefix=/ \
        --with-gui=qt5 \
        --with-incompatible-bdb \
        --disable-tests \
        --disable-bench

$NICE make -j"$JOBS"

echo "==> [6/6] Assembling the .app / .dmg (make deploy)..."
# make deploy runs macdeployqtplus + builds the disk image. The .dmg step is the
# most fragile part of a Linux cross-build; if it fails we still zip the .app.
if $NICE make deploy; then
    echo "    make deploy OK"
else
    echo "    WARNING: 'make deploy' failed (often the .dmg packaging on Linux)."
    echo "    The .app may still have been built; zipping it as a fallback."
fi

echo
echo "==> BUILD COMPLETE"
ls -la src/coinyecoind src/coinyecoin-cli 2>/dev/null || true
if [ -d "Coinyecoin-Qt.app" ]; then
    ls -ld Coinyecoin-Qt.app
    zip -qry Coinyecoin-2.3.0-fishsticks-macos-app.zip Coinyecoin-Qt.app && \
        echo "    wrote Coinyecoin-2.3.0-fishsticks-macos-app.zip"
fi
ls -la ./*.dmg 2>/dev/null || true
echo
CCACHE_BIN="depends/$HOST/native/bin/ccache"
[[ -x "$CCACHE_BIN" ]] && { echo "==> ccache stats (next run reuses this):"; "$CCACHE_BIN" -s 2>/dev/null | grep -iE "hit|miss|cache size" || true; }
