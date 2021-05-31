// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2011-2012 CoinyeCoin Developers
// Copyright (c) 2013 Coinyecoin Developers
// Copyright (c) 2013-2014 Team CoinyeCoin
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp> // for 'map_list_of()'
#include <boost/foreach.hpp>

#include "checkpoints.h"

#include "main.h"
#include "uint256.h"

namespace Checkpoints
{
    typedef std::map<int, uint256> MapCheckpoints;

    //
    // What makes a good checkpoint block?
    // + Is surrounded by blocks with reasonable timestamps
    //   (no blocks before with a timestamp after, none after with
    //    timestamp before)
    // + Contains no strange transactions
    //

	// no checkpoint now, can be added in later releases
    static MapCheckpoints mapCheckpoints =
            boost::assign::map_list_of
            (  0, hashGenesisBlockOfficial )
	    ( 239, uint256("0xb92e3fb5135ab80019a65ddf3e9564bf42366a0cd527bc5757fcb758f5148835") )
            ( 240, uint256("0x98258db64bc0886124695e3a730ba177edbc3436033a0faac024116394b97b6c") )
            ( 145109, uint256("0xd59a852c944ff7e070c739875c841bfe919e8ad13ef45e9624e7c91bb07e8bff") )
            ( 145110, uint256("0x48c6d42e8520f5efedffe2214df2da44ff4ee2aaecdb012311c0f90492307f5e" ) )
            ( 145111, uint256("0x717871a4ff53af6465577a3fca00032d214344a9eb7584114a0f8691c5b488b2" ) )
            ( 145112, uint256("0xf401f6d89f9751ab4ab2f79119535f356cdeb282d6716496b745be865079d6d4" ) )
            ( 1385400, uint256("0x41b19b11a406bb9aad5776e7c5fd241686b25c5fe99858e2ef03ba9959bcf9c" ) ) // May 31 2021 17:37:50
            ( 1385401, uint256("0x1dc05ae80d2eeb8e8f357ae6c54ba26de1524c826a92f0c93dac6658ea69bf6f" ) ) // ...... 17:39:03
            ( 1385402, uint256("0x6542f4a54884f735f385ccc7efc7b0f85013257366799b0d87e0a95fffc3d36b" ) ) // ...... 17:39:13
			;


    bool CheckBlock(int nHeight, const uint256& hash)
    {
        if (fTestNet) return true; // Testnet has no checkpoints

        MapCheckpoints::const_iterator i = mapCheckpoints.find(nHeight);
        if (i == mapCheckpoints.end()) return true;
        return hash == i->second;
		// return true;
    }

    int GetTotalBlocksEstimate()
    {
        if (fTestNet) return 0;
	
        return mapCheckpoints.rbegin()->first;
		// return 0;
    }

    CBlockIndex* GetLastCheckpoint(const std::map<uint256, CBlockIndex*>& mapBlockIndex)
    {
        if (fTestNet) return NULL;

        BOOST_REVERSE_FOREACH(const MapCheckpoints::value_type& i, mapCheckpoints)
        {
            const uint256& hash = i.second;
            std::map<uint256, CBlockIndex*>::const_iterator t = mapBlockIndex.find(hash);
            if (t != mapBlockIndex.end())
                return t->second;
				// return NULL;
        }
        return NULL;
    }
}
