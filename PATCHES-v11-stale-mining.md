Patch set v11: stale-tip solo mining gate

Changes
- Added a new runtime option: -allowstalemining
- Relaxed only the RPC mining gates in src/rpc/mining.cpp for getblocktemplate and getauxblock
- Left consensus, block validation, PoW, serialization, block versioning, timestamps, and rewards unchanged

Why
- On a long-stale network tip, external solo miners that use getblocktemplate/getauxblock can be blocked by the local IBD/stale-tip policy gate even when the node is otherwise synced to the chain
- This patch allows an operator to explicitly opt in to serving mining work anyway

Usage
- Start the daemon with: -allowstalemining=1
- Then use coinyecoin-cli getblocktemplate or your external miner as normal

Safety
- This does not alter consensus rules or block acceptance rules
- It only changes whether the RPC layer refuses mining work while the node thinks it is still in initial block download
