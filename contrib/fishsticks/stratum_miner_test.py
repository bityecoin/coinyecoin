#!/usr/bin/env python3
"""End-to-end test: act as a real stratum scrypt miner against the built-in
stratum server on regtest, and mine an actual block through it.
Implements the standard miner-side interpretation (cpuminer/ccminer/node-stratum
conventions) so a pass here means real miners interoperate."""
import socket, json, hashlib, struct, sys, time, subprocess

HOST, PORT = "127.0.0.1", 3333
CLI = ["./src/coinyecoin-cli", "-regtest", "-datadir=/tmp/cyreg"]

def dsha(b): return hashlib.sha256(hashlib.sha256(b).digest()).digest()
def scrypt_pow(header80):
    return hashlib.scrypt(header80, salt=header80, n=1024, r=1, p=1, maxmem=64*1024*1024, dklen=32)

def share_target(diff):
    # mirror server ShareTargetForDiff exactly (scrypt /65536 convention)
    diff /= 65536.0
    diff = max(diff, 1e-12)
    diff1 = 0x00000000ffff0000000000000000000000000000000000000000000000000000
    div = max(1, int(diff * 4294967296.0 + 0.5))
    q, r = divmod(diff1, div)
    return (q << 32) + ((r << 32) // div)

def prevhash_to_internal(ph_hex):
    b = bytes.fromhex(ph_hex)
    chunks = [b[i*4:(i+1)*4] for i in range(8)]
    return b"".join(chunks[::-1])   # inverse of server chunk transform

def cli(*args):
    return subprocess.check_output(CLI + list(args)).decode().strip()

sock = socket.create_connection((HOST, PORT), timeout=30)
f = sock.makefile("r")
def send(obj): sock.sendall((json.dumps(obj) + "\n").encode())
def recv():
    line = f.readline()
    if not line: raise RuntimeError("server closed connection")
    return json.loads(line)

send({"id": 1, "method": "mining.subscribe", "params": ["fishsticks-test/1.0"]})
msgs = [recv()]
send({"id": 2, "method": "mining.authorize", "params": ["testworker", "x"]})

sub = None; diff = None; job = None; authorized = False
deadline = time.time() + 30
while time.time() < deadline and not (sub and diff and job and authorized):
    m = msgs.pop(0) if msgs else recv()
    if m.get("id") == 1: sub = m["result"]
    elif m.get("id") == 2: authorized = m["result"] is True
    elif m.get("method") == "mining.set_difficulty": diff = m["params"][0]
    elif m.get("method") == "mining.notify": job = m["params"]

assert sub and job and authorized, f"handshake incomplete sub={bool(sub)} job={bool(job)} auth={authorized}"
en1_hex, en2_size = sub[1], sub[2]
print(f"subscribed: extranonce1={en1_hex} en2size={en2_size} diff={diff}")

job_id, prevhash, coinb1, coinb2, branches, ver_hex, nbits_hex, ntime_hex, clean = job
print(f"job {job_id}: height target, ver={ver_hex} nbits={nbits_hex} ntime={ntime_hex} branches={len(branches)}")

target = share_target(diff)
prev_internal = prevhash_to_internal(prevhash)
en2 = "00000001"
coinbase = bytes.fromhex(coinb1) + bytes.fromhex(en1_hex) + bytes.fromhex(en2) + bytes.fromhex(coinb2)
cbhash = dsha(coinbase)
root = cbhash
for b in branches:
    root = dsha(root + bytes.fromhex(b))

version = int(ver_hex, 16); ntime = int(ntime_hex, 16); nbits = int(nbits_hex, 16)
print("grinding nonces in python-scrypt...")
found = None
for nonce in range(0, 2_000_000):
    hdr = struct.pack("<I", version) + prev_internal + root + struct.pack("<III", ntime, nbits, nonce)
    assert len(hdr) == 80
    pow_le = int.from_bytes(scrypt_pow(hdr), "little")
    if pow_le <= target:
        found = nonce
        break
assert found is not None, "no share found in range (diff too high for test?)"
print(f"share found at nonce {found}")

send({"id": 4, "method": "mining.submit",
      "params": ["testworker", job_id, en2, ntime_hex, "%08x" % found]})
resp = None
deadline = time.time() + 15
while time.time() < deadline:
    m = recv()
    if m.get("id") == 4:
        resp = m
        break
print("submit response:", resp)
assert resp and resp.get("result") is True and not resp.get("error"), f"share rejected: {resp}"

time.sleep(2)
height = int(cli("getblockcount"))
print(f"regtest height after submit: {height}")
assert height >= 1, "block was not accepted into the chain"
besthash = cli("getbestblockhash")
blk = json.loads(cli("getblock", besthash))
print(f"BLOCK ACCEPTED height={blk['height']} hash={besthash[:24]}... txs={len(blk['tx'])}")

# dupe rejection check
send({"id": 5, "method": "mining.submit",
      "params": ["testworker", job_id, en2, ntime_hex, "%08x" % found]})
deadline = time.time() + 10
while time.time() < deadline:
    m = recv()
    if m.get("id") == 5:
        assert m.get("error"), "duplicate share was not rejected"
        print("duplicate share correctly rejected:", m["error"][1])
        break
sock.close()
print("STRATUM_E2E_PASS")
