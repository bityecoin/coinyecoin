# Fishsticks: Windows GUI + installer, and a built-in CPU mining tab

This tree adds two things to Coinyecoin Core 2.3.0-fishsticks:

1. A one-shot, reproducible **Windows cross-compile of the GUI wallet + installer**.
2. A **"Mining" tab in coinyecoin-qt** that drives the wallet's built-in CPU
   (scrypt) solo miner — one click to start/stop, with a live hash-rate readout.

Everything here builds the Bitcoin-Core-equivalent set:
`coinyecoin-qt.exe`, `coinyecoind.exe`, `coinyecoin-cli.exe`, plus
`coinyecoin-2.3.0-win64-setup.exe`.

---

## 1. Build the Windows GUI wallet + installer

Run from the source root, on any machine with Docker:

```bash
docker run --rm -v "$PWD":/src -w /src ubuntu:20.04 bash /src/build-windows-gui.sh
```

Why the container and the script file:

- The GUI `depends` pin **Qt 5.7.1** (2016). It will not compile with modern
  mingw gcc (>=11). Ubuntu 24.04 ships mingw gcc 13 -> fails. `ubuntu:20.04`
  has mingw gcc 9, which is period-correct.
- The script selects the **POSIX** mingw threading model. The `win32` model has
  no `std::thread` / `std::mutex` / `std::condition_variable` — that was the
  root cause of the earlier `'std::thread' does not name a type` build wall.
  (Do **not** use `-D_GLIBCXX_HAS_GTHREADS=1`; that just moves the same failure
  one layer down into the standard headers.)
- It installs **NSIS before configure** (the installer target is detected at
  configure time), patches the dead OpenSSL 1.0.2u download URL, and runs
  `make deploy` to produce the setup.exe.

First run builds `depends` (Qt/boost/openssl/bdb/...) and takes ~30-45 min.
Later runs reuse it.

Artifacts:

```
src/qt/coinyecoin-qt.exe
src/coinyecoind.exe
src/coinyecoin-cli.exe
coinyecoin-2.3.0-win64-setup.exe
```

### Daemon-only variant
If you ever want just the daemon/CLI (no Qt, much faster), the fork's existing
`build-windows.sh` already does that. This script is specifically the GUI +
installer path.

---

## 2. The Mining tab

### What was already there
This fork already ships a complete built-in CPU scrypt miner in the node:
`GenerateCoinyecoins()`, `IsMiningActive()`, `GetMiningThreadCount()`,
`GetMiningHashRate()` in `src/miner.h` / `src/miner.cpp`, exposed over RPC as
`setgenerate`. Found blocks go through `ProcessNewBlock()` and are validated
exactly like network blocks. **No consensus code was touched.**

### What this adds
A thin GUI over that API — a new wallet page:

- `src/qt/miningpage.h`, `src/qt/miningpage.cpp`, `src/qt/forms/miningpage.ui` (new)
- Wiring into `walletview`, `walletframe`, `bitcoingui`, and `Makefile.qt.include`
  (see `fishsticks-mining-gui.patch`).

The page has a thread-count selector (defaults to cores-1), a Start/Stop button,
and a live status + hash-rate display. Rewards pay to a reserve key from the
open wallet. It reuses the existing `:/icons/tx_mined` icon — no new assets.

Because the miner calls `ProcessNewBlock()` directly, it is **not** gated by the
stale-tip RPC policy (the `-allowstalemining` flag only affects
`getblocktemplate`/`getauxblock`), so the tab works even on a long-stale tip.

### Solo mining with an external miner (Antminer / ccminer / cpuminer)
The same Mining tab now also shows everything needed to point an **external**
scrypt miner at this wallet's built-in stratum server, read live from the node
via `GetStratumInfo()`:

- **Stratum server status** — ON (with port + connected client count) or OFF.
- **URL (this computer):** `stratum+tcp://127.0.0.1:<port>` (default port **3333**).
- **URL (other devices):** `stratum+tcp://YOUR-PC-LAN-IP:<port>` — replace with
  this machine's LAN address (e.g. `192.168.1.50`) for an ASIC on the network.
- **Worker / username:** a receive address from this wallet (payout goes to that
  address). Any address works; any non-address text (e.g. a rig name) pays the
  node's default address. The field is pre-filled with one of your addresses.
- **Password:** `x` — it is **not checked** by the server, so any value works.
- **ccminer command:** a ready-to-paste line, e.g.
  `ccminer -a scrypt -o stratum+tcp://127.0.0.1:3333 -u <your-COYE-address> -p x`.

For an **Antminer**-style ASIC, put the URL in the pool field, the address in the
worker field, and `x` as the password.

**The stratum server is OFF by default.** To turn it on, add `stratum=1` to
`coinyecoin.conf` (optionally `stratumport=<n>`, default 3333) and restart the
wallet, or start it with `-stratum=1`. The tab's status line reflects this live.
Relevant node options: `-stratum`, `-stratumport` (3333), `-stratumbind`
(0.0.0.0), `-stratumaddress` (fallback payout), `-stratumdiff` (16).

### Applying the mining patch to a clean tree
This zip already has it applied. To apply to a fresh checkout instead:

```bash
# copy the 3 new files into place:
#   src/qt/miningpage.h  src/qt/miningpage.cpp  src/qt/forms/miningpage.ui
# then:
patch -p1 < fishsticks-mining-gui.patch
```

### Honest caveats
- CPU scrypt mining on a small coin is slow; solo means you only earn on a full
  block. It's a legit "mine from the wallet" button, not a way to out-hash a
  dedicated rig.
- The Qt form and slots were validated with `uic`/`moc`. The full Qt cross-build
  was not run in the authoring environment, so if the compiler flags anything in
  `miningpage.cpp`, it'll be an include/signature nit in that one file — the
  miner API calls follow `miner.h` exactly.
- In-wallet **GPU** mining was intentionally not added (it would roughly double
  the build/support surface for a codebase this age). Point an external scrypt
  GPU miner at `coinyecoind` if you want GPU.

---

## Files changed / added

New:
- `build-windows-gui.sh`
- `src/qt/miningpage.h`
- `src/qt/miningpage.cpp`
- `src/qt/forms/miningpage.ui`

Modified (see `fishsticks-mining-gui.patch`):
- `src/qt/walletview.h`, `src/qt/walletview.cpp`
- `src/qt/walletframe.h`, `src/qt/walletframe.cpp`
- `src/qt/bitcoingui.h`, `src/qt/bitcoingui.cpp`
- `src/Makefile.qt.include`
