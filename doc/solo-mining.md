# Solo mining Coinyecoin (2.3.0-fishsticks)

The wallet now ships with a built-in stratum server and an old-school CPU miner.
No pool software, no proxy, no middleman. Point a miner at your own wallet and
every block you find pays you directly.

## 1. Turn on the stratum server

Add to `coinyecoin.conf` (see `contrib/fishsticks/coinyecoin.conf.solo-home`):

    server=1
    stratum=1
    stratumbind=0.0.0.0
    stratumport=3333
    stratumaddress=YOUR_COYE_ADDRESS   # optional fallback payout

Restart the wallet. `debug.log` shows:

    stratum: server listening on 0.0.0.0:3333 (start diff 16.000, vardiff on)

Check status any time:

    coinyecoin-cli getstratuminfo

## 2. Point miners at it

The worker username IS the payout address. If it parses as a valid COYE
address, blocks that miner finds pay that address. Otherwise blocks pay
`-stratumaddress`, and if that is not set, the local wallet.

**ccminer (NVIDIA):**

    ccminer -a scrypt -o stratum+tcp://WALLET_IP:3333 -u YOUR_COYE_ADDRESS -p x

**cpuminer / minerd:**

    minerd -a scrypt -o stratum+tcp://WALLET_IP:3333 -u YOUR_COYE_ADDRESS -p x

**Antminer L3+/L3++ (or any scrypt ASIC):**

    Pool 1 URL:  stratum+tcp://WALLET_IP:3333
    Worker:      YOUR_COYE_ADDRESS        (append .rig1 etc. if you want names)
    Password:    x

Multiple miners can connect at once (default cap 64, `-stratummaxconn`).
Vardiff automatically retargets each miner toward ~1 share every 4 seconds;
pin a fixed difficulty with `-stratumvardiff=0 -stratumdiff=<d>`.

## 3. Or just use the wallet itself (old school)

    coinyecoin-cli setgenerate true 2     # mine on 2 CPU threads
    coinyecoin-cli getgenerate            # -> true
    coinyecoin-cli setgenerate false      # stop

Or at startup: `-gen -genproclimit=2 -miningaddress=YOUR_COYE_ADDRESS`.
CPU scrypt is not going to outrace an L3, but on a chain this quiet it works.

## 4. Stale chain notes

2.3.0 fixes the wallet so a months-old tip no longer locks you out: once your
node has validated every block its peers know about, it reports synced, the
GUI drops the "catching up" overlay, and mining starts immediately. When
someone extends the chain your node picks it up like normal. Nothing about
consensus changed; old and new wallets stay on the same network.

Firewall reminder: open TCP 3333 (or your `-stratumport`) to your miners only.
Do not expose stratum to the open internet unless you want strangers soloing
into your node (they would still only mine valid blocks, but be tidy).
