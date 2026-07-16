# Coinyecoin Core 2.3.0 "Fishsticks"

*Do you like fishsticks? Do you like putting fishsticks in your mouth?
Then this is the release for you.* Dedicated to the one pool that kept
99.9% of this chain alive for two months straight. You know what you are.

No hard fork. No consensus changes. Old and new wallets stay on one network.

## Fixed

- **Wallets no longer stuck "synchronizing" on a stale chain.** A node that
  has validated every block its peers know about now reports synced, even if
  the last block is months old. Wallet, RPC, GUI and mining all come back to
  life. (IsInitialBlockDownload rewritten; sync latch removed; GUI overlay
  clears once tip == best header; verification progress no longer
  extrapolates phantom transactions.)
- **getblocktemplate chicken-and-egg on idle chains.** Pools and solo miners
  get work immediately on a caught-up node. `-allowstalemining=1` kept as an
  explicit override.

## Added

- **Built-in stratum server** (`-stratum=1`, port 3333): point ccminer,
  cpuminer, cgminer or an Antminer L3 straight at your wallet and solo mine.
  Speaks standard stratum v1 (node-stratum-pool dialect), per-miner vardiff,
  worker-name-as-payout-address, multi-miner capable. `getstratuminfo` RPC.
- **Old-school CPU miner is back**: `setgenerate true <threads>`,
  `getgenerate`, `-gen`, `-genproclimit`, `-miningaddress`.
- Docs: `doc/solo-mining.md`, `doc/vps-pool-runbook.md`, systemd unit and
  config profiles in `contrib/fishsticks/`, `build-windows.sh` cross-compile
  script and a GitLab CI job for Windows wallet builds.

## Version

v2.3.0.0-fishsticks. Blocks found through the new paths are submitted via
ProcessNewBlock() and validated identically to network blocks. The coinbase
carries a /Fishsticks/ tag so you can spot them in the explorer.
