# Ubuntu 24 / Qt5 modernization patch set v5

This patch set keeps consensus-critical behavior unchanged while updating build and UI code for modern Ubuntu/Qt/Boost/OpenSSL toolchains.

## Build / core fixes
- Stored Boost signals2 connections in `src/init.cpp` instead of disconnecting bound functors directly
- Added `<array>` include in `src/net_processing.cpp`
- Added `CFeeRate::operator=` in `src/amount.h`
- Added `<stdexcept>` include in `src/support/lockedpool.cpp`
- Disabled tests/bench in `build-ubuntu.sh` by default
- Added `-Wno-cpp` to suppress Boost.Math minimum-language preprocessor warning

## Qt / wallet modernization
- Replaced `Q_FOREACH` on `PAIRTYPE(...)` with range-for loops in payment request code
- Updated deprecated Boost.Filesystem APIs in `src/wallet/wallet.cpp`
- Updated deprecated Qt string split, sort, font metrics, printer, screen, and pixmap APIs in several `src/qt/*` files
- Updated protobuf payment ACK size call to `ByteSizeLong()`

## Consensus safety
No chain params, serialization, PoW, block validity, subsidy, or difficulty rules were changed.
