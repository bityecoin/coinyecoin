// Copyright (c) 2026 The Coinyecoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Fishsticks release: built-in stratum (v1) server so the wallet itself can
// hand out scrypt work, old-school solo style. Speaks the same dialect as
// node-stratum-pool / stratum-mining, i.e. what ccminer, cpuminer, cgminer
// and scrypt ASICs (Antminer L3 family) already expect.
//
// Design notes
//  - Non-consensus. Blocks found here go through ProcessNewBlock() and are
//    validated exactly like any block arriving from the network. No fork risk.
//  - One acceptor thread, one thread per client, one job manager thread.
//  - The stratum worker username may be a Coinyecoin address; if valid, the
//    coinbase pays that address. Otherwise -stratumaddress, otherwise a key
//    from the local wallet.

#include "stratum.h"

#include "base58.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/merkle.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "hash.h"
#include "miner.h"
#include "net.h"
#include "netbase.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/pureheader.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "streams.h"
#include "sync.h"
#include "timedata.h"
#include "txmempool.h"
#include "util.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "validation.h"
#include "validationinterface.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>

#ifndef WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/** A single unit of work handed to miners. */
struct StratumJob
{
    std::string id;
    std::shared_ptr<CBlockTemplate> tmpl; // keeps the tx set alive
    uint256 hashPrevBlock;
    int32_t nVersion;
    uint32_t nBits;
    uint32_t nTime;   // template time; miners may roll forward
    int nHeight;
    std::vector<uint256> vMerkleSteps; // stratum merkle branch (internal byte order)
    int64_t nCreated;
};

/** Per-connection state. */
struct StratumClient
{
    SOCKET sock;
    std::string strAddr;
    uint32_t nExtraNonce1;
    bool fSubscribed;
    bool fAuthorized;
    std::string strWorker;
    CScript payoutScript;      // where this client's blocks pay out
    bool fPayoutFromUser;      // true if payoutScript parsed from username

    double dDiff;              // current share difficulty (pool units)
    double dPrevDiff;          // previous difficulty (grace window)
    int64_t nDiffChangeTime;
    int64_t nVardiffStart;
    int nVardiffShares;

    uint64_t nSharesAccepted;
    uint64_t nSharesRejected;
    std::set<std::string> setDupes;

    CCriticalSection cs_send;

    StratumClient() : sock(INVALID_SOCKET), nExtraNonce1(0), fSubscribed(false),
        fAuthorized(false), fPayoutFromUser(false), dDiff(DEFAULT_STRATUM_DIFF),
        dPrevDiff(DEFAULT_STRATUM_DIFF), nDiffChangeTime(0), nVardiffStart(0),
        nVardiffShares(0), nSharesAccepted(0), nSharesRejected(0) {}
};

static CCriticalSection cs_stratum;
static std::atomic<bool> g_fStratumRunning(false);
static std::atomic<bool> g_fStratumStop(false);
static SOCKET g_listenSocket = INVALID_SOCKET;
static boost::thread_group* g_stratumThreads = NULL;
static std::vector<boost::shared_ptr<StratumClient> > g_vClients;
static std::map<std::string, boost::shared_ptr<StratumJob> > g_mapJobs;
static std::string g_strCurrentJob;
static uint32_t g_nJobCounter = 0;
static uint32_t g_nClientCounter = 0;
static CScript g_defaultPayoutScript;
static boost::shared_ptr<CReserveScript> g_walletScript; // KeepScript() on block found
static double g_dStartDiff = DEFAULT_STRATUM_DIFF;
static bool g_fVardiff = true;

// Lifetime counters for getstratuminfo
static std::atomic<uint64_t> g_nTotalAccepted(0);
static std::atomic<uint64_t> g_nTotalRejected(0);
static std::atomic<uint64_t> g_nBlocksFound(0);
static std::atomic<int64_t>  g_nLastShareTime(0);

// Magic marker used to locate the extranonce region inside the serialized
// coinbase. 8 bytes, replaced by extranonce1(4) + extranonce2(4).
static const unsigned char EXTRANONCE_MAGIC[8] = {0xf1, 0x5b, 0x57, 0x1c, 0xc5, 0x0d, 0xab, 0xe5};
static const char* COINBASE_TAG = "/Fishsticks/";

/* ------------------------------------------------------------------------- */
/* helpers                                                                    */
/* ------------------------------------------------------------------------- */

/** Stratum's odd prevhash encoding: internal (LE) bytes, taken in 4-byte
 *  chunks from the end, each chunk in forward order. Matches the encoding
 *  produced by node-stratum-pool and expected by cpuminer/ccminer/cgminer. */
static std::string PrevHashStratumHex(const uint256& hash)
{
    const unsigned char* p = hash.begin();
    std::string str;
    str.reserve(64);
    for (int i = 7; i >= 0; --i)
        str += HexStr(p + 4 * i, p + 4 * i + 4);
    return str;
}

/** Merkle "steps" for stratum: the miner folds the coinbase hash through
 *  these to obtain the merkle root. Classic pool algorithm with a coinbase
 *  placeholder at index 0 of each level. */
static std::vector<uint256> ComputeMerkleSteps(const CBlock& block)
{
    std::vector<uint256> vSteps;
    std::vector<uint256> vLevel;
    for (size_t i = 1; i < block.vtx.size(); i++)
        vLevel.push_back(block.vtx[i]->GetHash());

    while (!vLevel.empty()) {
        vSteps.push_back(vLevel[0]);
        // Pair up everything after the step element; duplicate last if odd.
        if (((vLevel.size() - 1) % 2) == 1)
            vLevel.push_back(vLevel.back());
        std::vector<uint256> vNext;
        for (size_t i = 1; i + 1 < vLevel.size(); i += 2) {
            vNext.push_back(Hash(vLevel[i].begin(), vLevel[i].end(),
                                 vLevel[i + 1].begin(), vLevel[i + 1].end()));
        }
        vLevel = vNext;
    }
    return vSteps;
}

/** Fold a coinbase txid through the merkle steps to get the root. */
static uint256 MerkleRootFromSteps(const uint256& cbHash, const std::vector<uint256>& vSteps)
{
    uint256 hash = cbHash;
    for (size_t i = 0; i < vSteps.size(); i++)
        hash = Hash(hash.begin(), hash.end(), vSteps[i].begin(), vSteps[i].end());
    return hash;
}

/** Share target for a given pool difficulty, scrypt convention (miners divide
 *  the advertised difficulty by 65536, so we multiply the target back up). */
static arith_uint256 ShareTargetForDiff(double dDiff)
{
    dDiff /= 65536.0;
    if (dDiff < 1e-12)
        dDiff = 1e-12;
    arith_uint256 bnDiff1;
    bnDiff1.SetCompact(0x1d00ffff); // difficulty-1 target
    uint64_t nDivisor = (uint64_t)(dDiff * 4294967296.0 + 0.5);
    if (nDivisor == 0)
        nDivisor = 1;
    arith_uint256 bnDivisor(nDivisor);
    arith_uint256 bnQuot = bnDiff1 / bnDivisor;
    arith_uint256 bnRem = bnDiff1 - bnQuot * bnDivisor;
    return (bnQuot << 32) + ((bnRem << 32) / bnDivisor);
}

/** Approximate difficulty (pool units) of a given pow hash, for logs/vardiff. */
static double ShareDiffOfHash(const uint256& powHash)
{
    double dHash = UintToArith256(powHash).getdouble();
    if (dHash <= 0.0)
        return 0.0;
    arith_uint256 bnDiff1;
    bnDiff1.SetCompact(0x1d00ffff);
    return bnDiff1.getdouble() * 65536.0 / dHash;
}

/** Build this client's coinbase for a job and split it around the extranonce
 *  region. Deterministic in (job, payout script), so submit-time rebuilds
 *  reproduce the exact bytes that were announced. */
static bool BuildCoinbaseParts(const StratumJob& job, const CScript& payoutScript,
                               std::vector<unsigned char>& vchCoinb1,
                               std::vector<unsigned char>& vchCoinb2)
{
    const CTransaction& tmplCb = *job.tmpl->block.vtx[0];

    CMutableTransaction cbTx;
    cbTx.nVersion = tmplCb.nVersion;
    cbTx.nLockTime = tmplCb.nLockTime;
    cbTx.vin.resize(1);
    cbTx.vin[0].prevout.SetNull();
    cbTx.vin[0].nSequence = tmplCb.vin[0].nSequence;

    std::vector<unsigned char> vchExtra(EXTRANONCE_MAGIC, EXTRANONCE_MAGIC + 8);
    const std::string strTag(COINBASE_TAG);
    vchExtra.insert(vchExtra.end(), strTag.begin(), strTag.end());
    cbTx.vin[0].scriptSig = CScript() << job.nHeight << vchExtra;
    if (cbTx.vin[0].scriptSig.size() > 100)
        return false;

    cbTx.vout = tmplCb.vout;
    if (cbTx.vout.empty())
        return false;
    cbTx.vout[0].scriptPubKey = payoutScript;

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << cbTx;
    std::vector<unsigned char> vchFull(ss.begin(), ss.end());

    // Locate the magic
    size_t nPos = std::string::npos;
    for (size_t i = 0; i + 8 <= vchFull.size(); i++) {
        if (memcmp(&vchFull[i], EXTRANONCE_MAGIC, 8) == 0) {
            nPos = i;
            break;
        }
    }
    if (nPos == std::string::npos)
        return false;

    vchCoinb1.assign(vchFull.begin(), vchFull.begin() + nPos);
    vchCoinb2.assign(vchFull.begin() + nPos + 8, vchFull.end());
    return true;
}

static void SendJson(boost::shared_ptr<StratumClient> client, const UniValue& val)
{
    std::string str = val.write() + "\n";
    LOCK(client->cs_send);
    size_t nSent = 0;
    while (nSent < str.size() && !g_fStratumStop) {
        int nRet = send(client->sock, str.data() + nSent, str.size() - nSent, MSG_NOSIGNAL);
        if (nRet <= 0)
            return;
        nSent += nRet;
    }
}

static void SendResult(boost::shared_ptr<StratumClient> client, const UniValue& id,
                       const UniValue& result, const UniValue& error = NullUniValue)
{
    UniValue reply(UniValue::VOBJ);
    reply.pushKV("id", id);
    reply.pushKV("result", result);
    reply.pushKV("error", error);
    SendJson(client, reply);
}

static void SendError(boost::shared_ptr<StratumClient> client, const UniValue& id,
                      int nCode, const std::string& strMsg)
{
    UniValue err(UniValue::VARR);
    err.push_back(nCode);
    err.push_back(strMsg);
    err.push_back(NullUniValue);
    SendResult(client, id, NullUniValue, err);
}

static void SendSetDifficulty(boost::shared_ptr<StratumClient> client, double dDiff)
{
    UniValue msg(UniValue::VOBJ);
    msg.pushKV("id", NullUniValue);
    msg.pushKV("method", "mining.set_difficulty");
    UniValue params(UniValue::VARR);
    params.push_back(dDiff);
    msg.pushKV("params", params);
    SendJson(client, msg);
}

/** Send mining.notify for a job to one client (coinbase is per-client). */
static void SendNotify(boost::shared_ptr<StratumClient> client,
                       boost::shared_ptr<StratumJob> job, bool fClean)
{
    std::vector<unsigned char> vchCoinb1, vchCoinb2;
    if (!BuildCoinbaseParts(*job, client->payoutScript, vchCoinb1, vchCoinb2)) {
        LogPrintf("stratum: failed to build coinbase for client %s\n", client->strAddr);
        return;
    }

    UniValue params(UniValue::VARR);
    params.push_back(job->id);
    params.push_back(PrevHashStratumHex(job->hashPrevBlock));
    params.push_back(HexStr(vchCoinb1.begin(), vchCoinb1.end()));
    params.push_back(HexStr(vchCoinb2.begin(), vchCoinb2.end()));
    UniValue branches(UniValue::VARR);
    for (size_t i = 0; i < job->vMerkleSteps.size(); i++)
        branches.push_back(HexStr(job->vMerkleSteps[i].begin(), job->vMerkleSteps[i].end()));
    params.push_back(branches);
    params.push_back(strprintf("%08x", (uint32_t)job->nVersion));
    params.push_back(strprintf("%08x", job->nBits));
    params.push_back(strprintf("%08x", job->nTime));
    params.push_back(fClean);

    UniValue msg(UniValue::VOBJ);
    msg.pushKV("id", NullUniValue);
    msg.pushKV("method", "mining.notify");
    msg.pushKV("params", params);
    SendJson(client, msg);
}

/** Create a fresh job from the current tip and register it. Returns null on failure. */
static boost::shared_ptr<StratumJob> CreateJob()
{
    std::unique_ptr<CBlockTemplate> ptmpl(
        BlockAssembler(Params()).CreateNewBlock(g_defaultPayoutScript, false));
    if (!ptmpl)
        return boost::shared_ptr<StratumJob>();

    boost::shared_ptr<StratumJob> job(new StratumJob());
    job->tmpl = std::shared_ptr<CBlockTemplate>(ptmpl.release());
    const CBlock& block = job->tmpl->block;
    job->hashPrevBlock = block.hashPrevBlock;
    job->nVersion = block.nVersion;
    job->nBits = block.nBits;
    job->nTime = block.nTime;
    job->vMerkleSteps = ComputeMerkleSteps(block);
    job->nCreated = GetTime();
    {
        LOCK(cs_main);
        job->nHeight = chainActive.Height() + 1;
    }
    {
        LOCK(cs_stratum);
        job->id = strprintf("%x", ++g_nJobCounter);
        g_mapJobs[job->id] = job;
        g_strCurrentJob = job->id;
        // Keep only the most recent jobs around
        while (g_mapJobs.size() > 8) {
            std::map<std::string, boost::shared_ptr<StratumJob> >::iterator itOldest = g_mapJobs.begin();
            for (std::map<std::string, boost::shared_ptr<StratumJob> >::iterator it = g_mapJobs.begin();
                 it != g_mapJobs.end(); ++it) {
                if (it->second->nCreated < itOldest->second->nCreated)
                    itOldest = it;
            }
            if (itOldest->first == job->id)
                break;
            g_mapJobs.erase(itOldest);
        }
    }
    return job;
}

static void BroadcastJob(boost::shared_ptr<StratumJob> job, bool fClean)
{
    std::vector<boost::shared_ptr<StratumClient> > vClients;
    {
        LOCK(cs_stratum);
        vClients = g_vClients;
    }
    for (size_t i = 0; i < vClients.size(); i++) {
        if (vClients[i]->fSubscribed)
            SendNotify(vClients[i], job, fClean);
    }
}

/* ------------------------------------------------------------------------- */
/* share handling                                                             */
/* ------------------------------------------------------------------------- */

static bool HandleSubmit(boost::shared_ptr<StratumClient> client, const UniValue& id,
                         const UniValue& params)
{
    if (params.size() < 5) {
        SendError(client, id, 20, "Invalid params");
        return false;
    }
    std::string strJob = params[1].get_str();
    std::string strEn2 = params[2].get_str();
    std::string strTime = params[3].get_str();
    std::string strNonce = params[4].get_str();

    boost::shared_ptr<StratumJob> job;
    {
        LOCK(cs_stratum);
        std::map<std::string, boost::shared_ptr<StratumJob> >::iterator it = g_mapJobs.find(strJob);
        if (it != g_mapJobs.end())
            job = it->second;
    }
    if (!job) {
        client->nSharesRejected++;
        g_nTotalRejected++;
        SendError(client, id, 21, "Job not found (stale)");
        return false;
    }
    if (strEn2.size() != 8 || strTime.size() != 8 || strNonce.size() != 8) {
        client->nSharesRejected++;
        g_nTotalRejected++;
        SendError(client, id, 20, "Malformed submission");
        return false;
    }

    // Duplicate check
    std::string strKey = strJob + ":" + strEn2 + ":" + strTime + ":" + strNonce;
    if (!client->setDupes.insert(strKey).second) {
        client->nSharesRejected++;
        g_nTotalRejected++;
        SendError(client, id, 22, "Duplicate share");
        return false;
    }
    if (client->setDupes.size() > 5000)
        client->setDupes.clear();

    uint32_t nTime = (uint32_t)strtoul(strTime.c_str(), NULL, 16);
    uint32_t nNonce = (uint32_t)strtoul(strNonce.c_str(), NULL, 16);

    // Time sanity: no rolling backwards before the template, max 2h future
    if (nTime < job->nTime || (int64_t)nTime > GetAdjustedTime() + 2 * 60 * 60) {
        client->nSharesRejected++;
        g_nTotalRejected++;
        SendError(client, id, 20, "ntime out of range");
        return false;
    }

    // Rebuild the exact coinbase we announced, with the miner's extranonce
    std::vector<unsigned char> vchCoinb1, vchCoinb2;
    if (!BuildCoinbaseParts(*job, client->payoutScript, vchCoinb1, vchCoinb2)) {
        SendError(client, id, 20, "Internal error");
        return false;
    }
    std::vector<unsigned char> vchCoinbase(vchCoinb1);
    unsigned char en1[4];
    en1[0] = (client->nExtraNonce1 >> 24) & 0xff;
    en1[1] = (client->nExtraNonce1 >> 16) & 0xff;
    en1[2] = (client->nExtraNonce1 >> 8) & 0xff;
    en1[3] = client->nExtraNonce1 & 0xff;
    vchCoinbase.insert(vchCoinbase.end(), en1, en1 + 4);
    std::vector<unsigned char> vchEn2 = ParseHex(strEn2);
    vchCoinbase.insert(vchCoinbase.end(), vchEn2.begin(), vchEn2.end());
    vchCoinbase.insert(vchCoinbase.end(), vchCoinb2.begin(), vchCoinb2.end());

    CMutableTransaction cbTx;
    try {
        CDataStream ss(vchCoinbase, SER_NETWORK, PROTOCOL_VERSION);
        ss >> cbTx;
    } catch (const std::exception&) {
        client->nSharesRejected++;
        g_nTotalRejected++;
        SendError(client, id, 20, "Bad coinbase");
        return false;
    }

    // Reconstruct the header
    const uint256 cbHash = CTransaction(cbTx).GetHash();
    CPureBlockHeader header;
    header.SetNull();
    header.nVersion = job->nVersion;
    header.hashPrevBlock = job->hashPrevBlock;
    header.hashMerkleRoot = MerkleRootFromSteps(cbHash, job->vMerkleSteps);
    header.nTime = nTime;
    header.nBits = job->nBits;
    header.nNonce = nNonce;

    const uint256 powHash = header.GetPoWHash();
    const arith_uint256 bnPow = UintToArith256(powHash);

    // Share difficulty check (with a short grace window after retargets)
    arith_uint256 bnShareTarget = ShareTargetForDiff(client->dDiff);
    bool fMeets = bnPow <= bnShareTarget;
    if (!fMeets && GetTime() - client->nDiffChangeTime < 20)
        fMeets = bnPow <= ShareTargetForDiff(client->dPrevDiff);
    if (!fMeets) {
        client->nSharesRejected++;
        g_nTotalRejected++;
        SendError(client, id, 23, "Share above target");
        return false;
    }

    client->nSharesAccepted++;
    client->nVardiffShares++;
    g_nTotalAccepted++;
    g_nLastShareTime = GetTime();
    LogPrint("stratum", "stratum: share accepted from %s (%s) diff %.3f\n",
             client->strWorker, client->strAddr, ShareDiffOfHash(powHash));

    // Network-level block?
    if (CheckProofOfWork(powHash, job->nBits, Params().GetConsensus(job->nHeight))) {
        CBlock block(job->tmpl->block);
        block.vtx[0] = MakeTransactionRef(std::move(cbTx));
        block.nVersion = job->nVersion;
        block.nTime = nTime;
        block.nNonce = nNonce;
        block.hashMerkleRoot = BlockMerkleRoot(block);
        if (block.hashMerkleRoot != header.hashMerkleRoot) {
            LogPrintf("stratum: WARNING merkle mismatch on block candidate, using recomputed root\n");
        }
        std::shared_ptr<const CBlock> spBlock = std::make_shared<const CBlock>(block);
        bool fNewBlock = false;
        if (ProcessNewBlock(Params(), spBlock, true, &fNewBlock)) {
            g_nBlocksFound++;
            LogPrintf("stratum: BLOCK FOUND by '%s'! height=%d hash=%s\n",
                      client->strWorker, job->nHeight, block.GetHash().GetHex());
            if (!client->fPayoutFromUser && g_walletScript)
                g_walletScript->KeepScript();
        } else {
            LogPrintf("stratum: block candidate at height %d REJECTED by validation\n", job->nHeight);
        }
    }

    SendResult(client, id, true);
    return true;
}

/* ------------------------------------------------------------------------- */
/* protocol dispatch                                                          */
/* ------------------------------------------------------------------------- */

static void HandleLine(boost::shared_ptr<StratumClient> client, const std::string& strLine)
{
    UniValue val;
    if (!val.read(strLine) || !val.isObject())
        return;
    const UniValue& id = find_value(val, "id");
    const UniValue& methodVal = find_value(val, "method");
    if (!methodVal.isStr())
        return;
    std::string strMethod = methodVal.get_str();
    UniValue params = find_value(val, "params");
    if (!params.isArray())
        params = UniValue(UniValue::VARR);

    if (strMethod == "mining.subscribe") {
        client->fSubscribed = true;
        UniValue result(UniValue::VARR);
        UniValue subs(UniValue::VARR);
        UniValue sub1(UniValue::VARR);
        sub1.push_back("mining.set_difficulty");
        sub1.push_back(strprintf("%08x", client->nExtraNonce1));
        UniValue sub2(UniValue::VARR);
        sub2.push_back("mining.notify");
        sub2.push_back(strprintf("%08x", client->nExtraNonce1));
        subs.push_back(sub1);
        subs.push_back(sub2);
        result.push_back(subs);
        result.push_back(strprintf("%08x", client->nExtraNonce1));
        result.push_back(4); // extranonce2 size
        SendResult(client, id, result);

        SendSetDifficulty(client, client->dDiff);
        boost::shared_ptr<StratumJob> job;
        {
            LOCK(cs_stratum);
            if (!g_strCurrentJob.empty())
                job = g_mapJobs[g_strCurrentJob];
        }
        if (job)
            SendNotify(client, job, true);
    }
    else if (strMethod == "mining.authorize") {
        std::string strUser = params.size() > 0 && params[0].isStr() ? params[0].get_str() : "";
        client->strWorker = strUser;
        // Worker name may be "<address>" or "<address>.<rigname>"
        std::string strAddrPart = strUser;
        size_t nDot = strAddrPart.find('.');
        if (nDot != std::string::npos)
            strAddrPart = strAddrPart.substr(0, nDot);
        CBitcoinAddress addr(strAddrPart);
        if (addr.IsValid()) {
            client->payoutScript = GetScriptForDestination(addr.Get());
            client->fPayoutFromUser = true;
            LogPrintf("stratum: %s authorized, paying out to %s\n", client->strAddr, strAddrPart);
        } else {
            LogPrintf("stratum: %s authorized as '%s', paying out to node default address\n",
                      client->strAddr, strUser);
        }
        client->fAuthorized = true;
        SendResult(client, id, true);
    }
    else if (strMethod == "mining.submit") {
        HandleSubmit(client, id, params);
    }
    else if (strMethod == "mining.extranonce.subscribe") {
        SendResult(client, id, true);
    }
    else if (strMethod == "mining.suggest_difficulty") {
        if (params.size() > 0 && params[0].isNum()) {
            double dSuggested = params[0].get_real();
            if (dSuggested >= 0.001 && dSuggested <= 1048576.0) {
                client->dPrevDiff = client->dDiff;
                client->dDiff = dSuggested;
                client->nDiffChangeTime = GetTime();
                SendSetDifficulty(client, client->dDiff);
            }
        }
        SendResult(client, id, true);
    }
    else if (strMethod == "mining.configure") {
        // No extensions supported (no version rolling on scrypt)
        SendResult(client, id, UniValue(UniValue::VOBJ));
    }
    else if (strMethod == "mining.get_transactions") {
        SendResult(client, id, UniValue(UniValue::VARR));
    }
    else {
        // Be permissive with unknown methods; some firmwares ping oddly
        if (!id.isNull())
            SendResult(client, id, NullUniValue);
    }
}

/* ------------------------------------------------------------------------- */
/* threads                                                                    */
/* ------------------------------------------------------------------------- */

static void StratumClientThread(boost::shared_ptr<StratumClient> client)
{
    RenameThread("coinye-stratum-client");
    std::string strBuffer;
    char pchBuf[4096];
    client->nVardiffStart = GetTime();

    while (!g_fStratumStop) {
        fd_set fdRead;
        FD_ZERO(&fdRead);
        FD_SET(client->sock, &fdRead);
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int nSel = select(client->sock + 1, &fdRead, NULL, NULL, &tv);
        if (nSel < 0)
            break;

        // Simple vardiff: aim for ~1 share every 4 seconds
        if (g_fVardiff && client->fSubscribed) {
            int64_t nNow = GetTime();
            int64_t nElapsed = nNow - client->nVardiffStart;
            if (nElapsed >= 45) {
                double dRate = (double)client->nVardiffShares / (double)nElapsed;
                double dFactor = dRate > 0.0 ? dRate / 0.25 : 0.5;
                if (dFactor < 0.5) dFactor = 0.5;
                if (dFactor > 4.0) dFactor = 4.0;
                double dNew = client->dDiff * dFactor;
                if (dNew < 0.001) dNew = 0.001;
                if (dNew > 1048576.0) dNew = 1048576.0;
                if (dNew / client->dDiff > 1.2 || dNew / client->dDiff < 0.8) {
                    client->dPrevDiff = client->dDiff;
                    client->dDiff = dNew;
                    client->nDiffChangeTime = nNow;
                    SendSetDifficulty(client, dNew);
                    LogPrint("stratum", "stratum: vardiff %s -> %.3f\n", client->strAddr, dNew);
                }
                client->nVardiffStart = nNow;
                client->nVardiffShares = 0;
            }
        }

        if (nSel == 0 || !FD_ISSET(client->sock, &fdRead))
            continue;

        int nBytes = recv(client->sock, pchBuf, sizeof(pchBuf), 0);
        if (nBytes <= 0)
            break;
        strBuffer.append(pchBuf, nBytes);
        if (strBuffer.size() > 128 * 1024)
            break; // protocol abuse

        size_t nPos;
        while ((nPos = strBuffer.find('\n')) != std::string::npos) {
            std::string strLine = strBuffer.substr(0, nPos);
            strBuffer.erase(0, nPos + 1);
            if (!strLine.empty() && strLine[strLine.size() - 1] == '\r')
                strLine.erase(strLine.size() - 1);
            if (!strLine.empty()) {
                try {
                    HandleLine(client, strLine);
                } catch (const std::exception& e) {
                    LogPrintf("stratum: error handling line from %s: %s\n", client->strAddr, e.what());
                }
            }
        }
    }

    CloseSocket(client->sock);
    {
        LOCK(cs_stratum);
        for (std::vector<boost::shared_ptr<StratumClient> >::iterator it = g_vClients.begin();
             it != g_vClients.end(); ++it) {
            if (it->get() == client.get()) {
                g_vClients.erase(it);
                break;
            }
        }
    }
    LogPrintf("stratum: client %s disconnected\n", client->strAddr);
}

static void StratumAcceptThread()
{
    RenameThread("coinye-stratum-accept");
    while (!g_fStratumStop) {
        fd_set fdRead;
        FD_ZERO(&fdRead);
        FD_SET(g_listenSocket, &fdRead);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500 * 1000;
        int nSel = select(g_listenSocket + 1, &fdRead, NULL, NULL, &tv);
        if (nSel <= 0 || g_fStratumStop)
            continue;

        struct sockaddr_storage saClient;
        socklen_t nLen = sizeof(saClient);
        SOCKET sockClient = accept(g_listenSocket, (struct sockaddr*)&saClient, &nLen);
        if (sockClient == INVALID_SOCKET)
            continue;

        int nMaxConn = GetArg("-stratummaxconn", DEFAULT_STRATUM_MAX_CONN);
        bool fFull = false;
        {
            LOCK(cs_stratum);
            fFull = (int)g_vClients.size() >= nMaxConn;
        }
        if (fFull) {
            CloseSocket(sockClient);
            continue;
        }

        int nOne = 1;
        setsockopt(sockClient, IPPROTO_TCP, TCP_NODELAY, (const char*)&nOne, sizeof(nOne));

        char pchIp[64];
        memset(pchIp, 0, sizeof(pchIp));
        strcpy(pchIp, "unknown");
        {
            // Portable across POSIX and MinGW (avoids Vista-gated inet_ntop).
            CService servClient;
            if (servClient.SetSockAddr((const struct sockaddr*)&saClient)) {
                std::string strClientIp = servClient.ToStringIP();
                strncpy(pchIp, strClientIp.c_str(), sizeof(pchIp) - 1);
            }
        }

        boost::shared_ptr<StratumClient> client(new StratumClient());
        client->sock = sockClient;
        client->strAddr = pchIp;
        client->payoutScript = g_defaultPayoutScript;
        client->dDiff = g_dStartDiff;
        client->dPrevDiff = g_dStartDiff;
        {
            LOCK(cs_stratum);
            client->nExtraNonce1 = ++g_nClientCounter;
            g_vClients.push_back(client);
        }
        LogPrintf("stratum: new client %s (extranonce1=%08x)\n", client->strAddr, client->nExtraNonce1);
        g_stratumThreads->create_thread(boost::bind(&StratumClientThread, client));
    }
}

static void StratumJobThread()
{
    RenameThread("coinye-stratum-jobs");
    uint256 hashLastTip;
    int64_t nLastJobTime = 0;
    size_t nLastMempool = 0;

    // Wait until the chain is loaded
    while (!g_fStratumStop) {
        {
            LOCK(cs_main);
            if (chainActive.Tip() != NULL)
                break;
        }
        MilliSleep(500);
    }

    while (!g_fStratumStop) {
        uint256 hashTip;
        {
            LOCK(cs_main);
            if (chainActive.Tip())
                hashTip = chainActive.Tip()->GetBlockHash();
        }
        size_t nMempool = mempool.size();
        int64_t nNow = GetTime();

        bool fNewTip = (hashTip != hashLastTip);
        bool fMempoolChanged = (nMempool != nLastMempool) && (nNow - nLastJobTime >= 2);
        bool fRefresh = (nNow - nLastJobTime >= 45);

        if (fNewTip || fMempoolChanged || fRefresh) {
            boost::shared_ptr<StratumJob> job = CreateJob();
            if (job) {
                hashLastTip = hashTip;
                nLastJobTime = nNow;
                nLastMempool = nMempool;
                BroadcastJob(job, fNewTip);
                LogPrint("stratum", "stratum: new job %s height=%d clean=%d txs=%u\n",
                         job->id, job->nHeight, fNewTip ? 1 : 0, (unsigned)job->tmpl->block.vtx.size());
            } else {
                MilliSleep(1000);
            }
        }
        MilliSleep(250);
    }
}

/* ------------------------------------------------------------------------- */
/* lifecycle                                                                  */
/* ------------------------------------------------------------------------- */

bool StratumServerRunning()
{
    return g_fStratumRunning;
}

bool StartStratumServer()
{
    if (g_fStratumRunning)
        return true;
    g_fStratumStop = false;

    // Resolve the default payout script: -stratumaddress first, else wallet key
    std::string strAddr = GetArg("-stratumaddress", "");
    if (!strAddr.empty()) {
        CBitcoinAddress addr(strAddr);
        if (!addr.IsValid()) {
            LogPrintf("stratum: ERROR invalid -stratumaddress '%s'\n", strAddr);
            return false;
        }
        g_defaultPayoutScript = GetScriptForDestination(addr.Get());
    } else {
        GetMainSignals().ScriptForMining(g_walletScript);
        if (!g_walletScript || g_walletScript->reserveScript.empty()) {
            LogPrintf("stratum: ERROR no -stratumaddress given and no wallet available for payouts\n");
            return false;
        }
        g_defaultPayoutScript = g_walletScript->reserveScript;
    }

    g_dStartDiff = atof(GetArg("-stratumdiff", strprintf("%g", DEFAULT_STRATUM_DIFF)).c_str());
    if (g_dStartDiff <= 0.0)
        g_dStartDiff = DEFAULT_STRATUM_DIFF;
    g_fVardiff = GetBoolArg("-stratumvardiff", true);

    int nPort = GetArg("-stratumport", DEFAULT_STRATUM_PORT);
    std::string strBind = GetArg("-stratumbind", "0.0.0.0");

    g_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocket == INVALID_SOCKET) {
        LogPrintf("stratum: ERROR socket() failed\n");
        return false;
    }
    int nOne = 1;
    setsockopt(g_listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&nOne, sizeof(nOne));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)nPort);
    {
        // Portable across POSIX and MinGW (avoids Vista-gated inet_pton).
        CNetAddr bindAddr;
        if (!(LookupHost(strBind.c_str(), bindAddr, false) &&
              bindAddr.IsIPv4() && bindAddr.GetInAddr(&sa.sin_addr)))
            sa.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(g_listenSocket, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
        LogPrintf("stratum: ERROR could not bind %s:%d\n", strBind, nPort);
        CloseSocket(g_listenSocket);
        return false;
    }
    if (listen(g_listenSocket, 16) != 0) {
        LogPrintf("stratum: ERROR listen() failed\n");
        CloseSocket(g_listenSocket);
        return false;
    }

    g_stratumThreads = new boost::thread_group();
    g_stratumThreads->create_thread(&StratumAcceptThread);
    g_stratumThreads->create_thread(&StratumJobThread);
    g_fStratumRunning = true;
    LogPrintf("stratum: server listening on %s:%d (start diff %.3f, vardiff %s)\n",
              strBind, nPort, g_dStartDiff, g_fVardiff ? "on" : "off");
    return true;
}

void InterruptStratumServer()
{
    g_fStratumStop = true;
}

void StopStratumServer()
{
    if (!g_fStratumRunning)
        return;
    g_fStratumStop = true;
    if (g_listenSocket != INVALID_SOCKET)
        CloseSocket(g_listenSocket);
    {
        LOCK(cs_stratum);
        for (size_t i = 0; i < g_vClients.size(); i++)
            CloseSocket(g_vClients[i]->sock);
    }
    if (g_stratumThreads) {
        g_stratumThreads->join_all();
        delete g_stratumThreads;
        g_stratumThreads = NULL;
    }
    {
        LOCK(cs_stratum);
        g_vClients.clear();
        g_mapJobs.clear();
        g_strCurrentJob.clear();
    }
    g_fStratumRunning = false;
    LogPrintf("stratum: server stopped\n");
}

UniValue GetStratumInfo()
{
    UniValue info(UniValue::VOBJ);
    info.pushKV("enabled", (bool)g_fStratumRunning);
    info.pushKV("port", GetArg("-stratumport", DEFAULT_STRATUM_PORT));
    {
        LOCK(cs_stratum);
        info.pushKV("clients", (int64_t)g_vClients.size());
        info.pushKV("currentjob", g_strCurrentJob);
        std::map<std::string, boost::shared_ptr<StratumJob> >::iterator it = g_mapJobs.find(g_strCurrentJob);
        if (it != g_mapJobs.end())
            info.pushKV("jobheight", it->second->nHeight);
    }
    info.pushKV("startdiff", g_dStartDiff);
    info.pushKV("vardiff", g_fVardiff);
    info.pushKV("sharesaccepted", (int64_t)g_nTotalAccepted.load());
    info.pushKV("sharesrejected", (int64_t)g_nTotalRejected.load());
    info.pushKV("blocksfound", (int64_t)g_nBlocksFound.load());
    info.pushKV("lastsharetime", (int64_t)g_nLastShareTime.load());
    return info;
}
