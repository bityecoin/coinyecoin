Coinyecoin Ubuntu 24 compile-fixes v7

Built from v5 (the last better build) with the v6 bantable regression removed.

Changes:
- src/qt/bantablemodel.cpp: BanTablePriv::index(int) returns nullptr for invalid indices
- src/init.cpp: uses stored boost::signals2::connection objects instead of old disconnect(functor) pattern
- src/net_processing.cpp: includes <array>
- src/support/lockedpool.cpp: includes <stdexcept>
- build-ubuntu.sh: configures with --with-incompatible-bdb --disable-tests --disable-bench
