#!/usr/bin/env bash
# =============================================================================
# build-windows-gui.sh
# Cross-compile Coinyecoin Core 2.3.0-fishsticks for 64-bit Windows:
#   src/qt/coinyecoin-qt.exe   (GUI wallet, Bitcoin-Core equivalent)
#   src/coinyecoind.exe        (daemon)
#   src/coinyecoin-cli.exe     (CLI)
#   coinyecoin-2.3.0-win64-setup.exe   (NSIS installer)
#
# WHY A SCRIPT FILE (not an inline `docker ... bash -c "..."`):
#   The GUI depends pins Qt 5.7.1 (2016), which will not compile with modern
#   mingw gcc (>=11). Ubuntu 24.04's mingw is gcc 13 -> fails. So build inside
#   a period-correct ubuntu:20.04 container (mingw gcc 9). Running a *file*
#   avoids the nested-quote breakage that bites inline bash -c blobs.
#
# HOW TO RUN (from the source root, on any machine with Docker):
#   docker run --rm -v "$PWD":/src -w /src ubuntu:20.04 bash /src/build-windows-gui.sh
#
# First run builds depends (Qt/boost/openssl/bdb/...) and takes ~30-45 min.
# Re-runs reuse depends and are much faster.
# =============================================================================
set -euo pipefail

[[ -f configure.ac ]] || { echo "ERROR: run from the coinyecoin source root." >&2; exit 1; }

HOST=x86_64-w64-mingw32
# Leave one core free so sshd / your shell stay responsive during the build.
# (On a 4-core box, -j$(nproc) pegs all 4 and the machine feels frozen even
#  though it is healthy.) Also cap by RAM (~2GB per heavy C++ unit) for
#  low-memory hosts; on a big-RAM box the core rule dominates.
CORES="$(nproc)"
JOBS="$CORES"; [ "$JOBS" -gt 1 ] && JOBS=$((JOBS - 1))
MEM_GB="$(awk '/MemTotal/{printf "%d",$2/1024/1024}' /proc/meminfo 2>/dev/null || echo 4)"
MEM_JOBS=$(( MEM_GB / 2 )); [ "$MEM_JOBS" -lt 1 ] && MEM_JOBS=1
[ "$JOBS" -gt "$MEM_JOBS" ] && JOBS="$MEM_JOBS"
# Run the compilers at low priority so interactive work always wins the CPU.
NICE="nice -n 15"
command -v ionice >/dev/null 2>&1 && NICE="ionice -c3 $NICE"
echo "==> building with -j$JOBS ($CORES cores, ~${MEM_GB}GB RAM), low priority"
export DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC

# Persist the compiler cache on the mounted source volume ($PWD == /src, which is
# your host directory) so re-runs reuse it. Without this the cache sits in the
# --rm container's /root/.ccache and is destroyed on exit, forcing a full cold
# recompile of the whole tree EVERY run. With it, the first run is slow and
# every run after only recompiles what actually changed (minutes, not tens).
export CCACHE_DIR="${CCACHE_DIR:-$PWD/.ccache}"
export CCACHE_COMPILERCHECK=content
export CCACHE_MAXSIZE=8G
mkdir -p "$CCACHE_DIR"
echo "==> ccache dir: $CCACHE_DIR (persists across runs)"

echo "==> [1/6] Installing toolchain + NSIS (installer must be present BEFORE configure)..."
apt-get update -qq
apt-get install -y -qq \
    build-essential autoconf automake libtool pkg-config bsdmainutils \
    curl git perl python3 zip ca-certificates \
    g++-mingw-w64-x86-64 nsis tzdata

echo "==> [2/6] Selecting the POSIX mingw threading model..."
# win32 mingw has NO std::thread / std::mutex / std::condition_variable, which is
# the root cause of the 'std::thread does not name a type' build wall. Use posix.
update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix
update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
echo -n "    g++ thread model: "
x86_64-w64-mingw32-g++ -v 2>&1 | sed -n 's/^Thread model: //p'

echo "==> [3/6] Patching depends (dead OpenSSL mirror; BDB atomic clash)..."
python3 - <<'PY'
import io, os, re

# OpenSSL 1.0.2u: openssl.org/source/old now 404s. Point at a live mirror.
p = "depends/packages/openssl.mk"
if os.path.exists(p):
    s = open(p).read()
    if "www.openssl.org/source/old" in s:
        s = re.sub(r'^\$\(package\)_download_path=.*$',
                   '$(package)_download_path=https://distfiles.macports.org/openssl10',
                   s, flags=re.M)
        open(p, "w").write(s)
        print("    openssl.mk: download path repointed to macports mirror")
    else:
        print("    openssl.mk: already patched / different mirror, leaving as-is")

# BDB + modern gcc: __atomic_compare_exchange redefinition. Rename in the header
# during preprocess. (Harmless on gcc 9; kept for robustness across hosts.)
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
            open(p, "w").write(s)
            print("    bdb.mk: atomic symbol rename inserted")
    else:
        print("    bdb.mk: already patched")
PY

echo "==> [4/6] Building depends WITH Qt (first run is the slow part)..."
$NICE make -C depends HOST="$HOST" -j"$JOBS"

echo "==> [5/6] autogen + configure (GUI enabled) + build..."
# scrub any stale win32 configure output from a previous attempt on the mounted tree
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

echo "==> [6/6] Building the Windows installer (make deploy -> makensis)..."
$NICE make deploy

echo
echo "==> BUILD COMPLETE"
ls -la src/qt/coinyecoin-qt.exe src/coinyecoind.exe src/coinyecoin-cli.exe 2>/dev/null || true
ls -la ./*-win64-setup.exe 2>/dev/null || true
echo
CCACHE_BIN="depends/$HOST/native/bin/ccache"
[[ -x "$CCACHE_BIN" ]] && { echo "==> ccache stats (next run reuses this):"; "$CCACHE_BIN" -s 2>/dev/null | grep -iE "hit|miss|cache size" || true; }
echo
echo "The GUI wallet has a new 'Mining' tab (built-in CPU scrypt solo miner)."
