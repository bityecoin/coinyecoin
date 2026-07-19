// Copyright (c) 2026 The Coinyecoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Fishsticks release: built-in stratum server for old-school solo mining.
// Point ccminer/cpuminer/cgminer or a scrypt ASIC (Antminer L3 etc.)
// straight at the wallet:  stratum+tcp://<wallet-ip>:3333
// Worker username may be a Coinyecoin address to direct the payout.

#ifndef BITCOIN_STRATUM_H
#define BITCOIN_STRATUM_H

#include <string>

#include <univalue.h>

/** Default settings */
static const bool DEFAULT_STRATUM_ENABLE = false;
static const int DEFAULT_STRATUM_PORT = 3333;
static const double DEFAULT_STRATUM_DIFF = 16.0;   // scrypt pool-difficulty units
static const int DEFAULT_STRATUM_MAX_CONN = 64;

/** Start the stratum server threads. Returns false on fatal error (bad bind, no payout script). */
bool StartStratumServer();
/** Signal all stratum threads to stop (non-blocking). */
void InterruptStratumServer();
/** Join and clean up all stratum threads. */
void StopStratumServer();

/** True if the server is currently running. */
bool StratumServerRunning();
/** Snapshot of counters for RPC (getstratuminfo). */
UniValue GetStratumInfo();

#endif // BITCOIN_STRATUM_H
