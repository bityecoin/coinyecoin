# PATCHES v12 — 2.3.0 "Fishsticks"

Scope: stale-chain sync fix, built-in stratum server, built-in CPU miner,
version rebrand. Zero consensus changes; every new block path submits via
ProcessNewBlock() and validates like a network block.

## 1. Stale-chain sync fix (replaces the v11 -allowstalemining band-aid)

**src/validation.cpp — IsInitialBlockDownload() rewritten.** A node now only
reports IBD while `pindexBestHeader->nChainWork > tip->nChainWork` (peers
advertise a chain it hasn't validated); only then do the classic
minimum-chain-work and max-tip-age rules apply. The one-way latch is removed
(it re-latched true on every restart against a stale tip, which is why wallets
sat in "synchronizing" forever). `-allowstalemining` remains as an RPC-gate
override but is no longer needed for normal operation.

**src/chainparams.cpp — mainnet chainTxData set to dTxRate=0.** Estimation
only. GuessVerificationProgress previously extrapolated 0.3 tx/s forever, so
progress could never reach 100% on an idle chain (GUI stuck at 99.x%).

**src/qt/bitcoingui.cpp — sync gate.** `setNumBlocks` now treats
`count >= headerTipHeight` (with no header in flight) as in-sync, clearing the
"catching up" spinner/overlay on an old-but-caught-up tip. 3 lines, uses the
existing getHeaderTipHeight() API.

## 2. Built-in stratum server (NEW: src/stratum.h, src/stratum.cpp, ~960 lines)

Options: `-stratum -stratumbind -stratumport(3333) -stratumaddress`
`-stratumdiff(16) -stratumvardiff(1) -stratummaxconn(64)`.

- Standard stratum v1, node-stratum-pool wire dialect: 4-byte-chunk-swapped
  prevhash, coinb1/coinb2 split (8-byte extranonce region inside the scriptSig
  push, /Fishsticks/ tag), classic merkle-steps branch, scrypt diff/65536
  target convention. Verified against an independent miner-side
  implementation (contrib/fishsticks/stratum_miner_test.py).
- extranonce1 = 4 bytes/client, extranonce2size = 4. Handles subscribe,
  authorize, submit, extranonce.subscribe, suggest_difficulty, configure,
  get_transactions.
- Worker username parsed as payout address (fallback: -stratumaddress, then
  wallet key). Per-miner vardiff targeting ~1 share/4 s with a 20 s grace
  window after retargets. Duplicate-share and ntime-bounds rejection.
- Job manager rebuilds work on new tip (clean=1), on mempool change (2 s
  rate-limit), and every 45 s. Threads: acceptor + per-client + job manager.

## 3. Old-school CPU miner (src/miner.cpp/.h append)

`GenerateCoinyecoins()`, `-gen`, `-genproclimit`, `-miningaddress`;
`setgenerate` / `getgenerate` RPCs restored (src/rpc/mining.cpp) with CLI
param conversions (src/rpc/client.cpp). `getstratuminfo` RPC added.

## 4. Wiring and version

- src/init.cpp: option help, startup after "Done loading", Interrupt/Shutdown
  teardown ordering (stratum + miner stop before validation teardown).
- src/Makefile.am: stratum.cpp/h added to libbitcoin_server.
- configure.ac 2.2.x -> 2.3.0; clientversion suffix "-fishsticks".
  Reports: `Coinyecoin Core Daemon version v2.3.0.0-...-fishsticks`.

## 5. Test evidence (regtest, this build)

- Stratum e2e: independent python scrypt miner subscribed, received work,
  mined height-1 block, share accepted, block ACCEPTED on chain, wallet
  credited 666666 COYE immature. Duplicate share rejected.
- Multi-tx job: mempool tx picked up within 2 s, job carried 1 merkle branch,
  mined block ACCEPTED (height 292, txs=2), zero "merkle mismatch" log lines.
- CPU miner: `setgenerate true 1` mined heights 293..2731 in ~8 s;
  getgenerate true/false; `setgenerate false` stopped cleanly.
- getstratuminfo reports clients/job/shares/blocksfound correctly.
- Build: gcc 13.3 / boost 1.83 / Ubuntu 24.04, zero warnings.

Files changed: configure.ac, src/Makefile.am, src/chainparams.cpp,
src/clientversion.cpp, src/init.cpp, src/miner.cpp, src/miner.h,
src/qt/bitcoingui.cpp, src/rpc/client.cpp, src/rpc/mining.cpp,
src/validation.cpp. New: src/stratum.h, src/stratum.cpp, doc/solo-mining.md,
doc/vps-pool-runbook.md, contrib/fishsticks/*, build-windows.sh,
.gitlab-ci.yml, RELEASE-NOTES-2.3.0-fishsticks.md.
