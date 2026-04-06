# PATCHES v8

Based on v7.

Changes:
- src/qt/peertablemodel.cpp: return nullptr from PeerTablePriv::index(int) on invalid row
- src/qt/addresstablemodel.cpp: return nullptr from AddressTablePriv::index(int) on invalid row
- src/qt/transactiontablemodel.cpp: return nullptr from TransactionTablePriv::index(int) on invalid row
- src/qt/bitcoin.cpp: use Qt::WindowFlags() for SplashScreen constructor to avoid deprecated zero-flags warning
