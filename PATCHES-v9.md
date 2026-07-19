Patched for Ubuntu 24 / Qt5 compile progress (v9)

Changes in this pass:
- src/qt/transactiondesc.cpp
  - replaced Q_FOREACH over PAIRTYPE(std::string, std::string) with range-for loops
- src/qt/peertablemodel.cpp
  - fixed flags() invalid-index return to Qt::ItemFlags()
- src/qt/receivecoinsdialog.cpp
  - replaced deprecated QModelIndex::child() usage with model()->index(...)
- src/qt/paymentserver.cpp
  - removed deprecated QSslSocket::setDefaultCaCertificates() call

This pass is intended to move beyond the transactiondesc.cpp Qt macro failure and reduce nearby Qt deprecation warnings, without touching consensus or block serialization.
