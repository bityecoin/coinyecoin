# Pool VPS runbook: crashes and refusing to sync

Symptoms this covers: coinyecoind dies on the Ubuntu VPS, restarts into a
reindex, dies again; pool stuck showing "node not synced"; getblocktemplate
returning "in initial download".

## 1. Confirm the killer (it is almost always the OOM killer)

    dmesg -T | grep -i -E "oom|killed process"
    journalctl -u coinyecoind --since "-2 days" | tail -50

A 1-2 GB VPS cannot run default settings while reindexing. Coinyecoind's
default dbcache plus mempool plus reindex buffers exceeds available RAM,
the kernel kills it mid-write, the DB is left dirty, next start reindexes,
and the loop repeats.

## 2. Fix the memory footprint

Add swap (once):

    fallocate -l 2G /swapfile && chmod 600 /swapfile
    mkswap /swapfile && swapon /swapfile
    echo '/swapfile none swap sw 0 0' >> /etc/fstab

Use the low-RAM config in `contrib/fishsticks/coinyecoin.conf.pool-vps`
(key lines: `dbcache=100`, `maxmempool=100`, `par=1`, `maxconnections=24`).

## 3. Stop the reindex loop: bootstrap from a healthy node

Do not let the VPS grind through a reindex it will lose. Copy state from a
node that is already at the tip (your home wallet):

    # on both machines
    coinyecoin-cli stop
    # from the healthy node
    rsync -a --info=progress2 ~/.coinyecoin/blocks/ ~/.coinyecoin/chainstate/ \
        root@VPS:/home/coinye/.coinyecoin/
    chown -R coinye:coinye /home/coinye/.coinyecoin

Start the VPS node; it verifies the copied tip and comes up synced in minutes.

## 4. Keep it up

Install `contrib/fishsticks/coinyecoind.service` (Restart=on-failure,
OOMScoreAdjust=-500), then:

    systemctl daemon-reload && systemctl enable --now coinyecoind

## 5. Why the pool works again on a stale chain

Pre-2.3.0, a tip older than ~24h made IsInitialBlockDownload() latch true,
so getblocktemplate refused to hand out work: nobody could mine because
nobody was mining. 2.3.0 only reports IBD while peers advertise a better
chain than the node has validated, so a caught-up node on a quiet chain
serves templates immediately. Keep `allowstalemining=1` in the conf as a
belt-and-braces override. This is node policy only; no consensus change.

## 6. Quick health checklist

    coinyecoin-cli getblockchaininfo   # blocks == headers, ibd false
    coinyecoin-cli getnetworkinfo      # connections > 0
    coinyecoin-cli getblocktemplate '{"rules":[]}' | head -5   # returns work
    free -m; dmesg -T | tail -5        # no fresh OOM lines
