// Copyright (c) 2023 The Coinyecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_MININGPAGE_H
#define BITCOIN_QT_MININGPAGE_H

#include <QWidget>
#include <QString>

class ClientModel;
class WalletModel;
class PlatformStyle;

namespace Ui {
    class MiningPage;
}

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

/** Control page for the wallet's built-in CPU (scrypt) solo miner.
 *  Thin GUI over the node's GenerateCoinyecoins() / IsMiningActive() API in miner.h.
 */
class MiningPage : public QWidget
{
    Q_OBJECT

public:
    explicit MiningPage(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~MiningPage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);

private Q_SLOTS:
    void on_toggleMiningButton_clicked();
    void updateUI();

private:
    Ui::MiningPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    const PlatformStyle *platformStyle;
    QTimer *pollTimer;

    void startMining();
    void stopMining();
    void refreshExternalMinerInfo();
    QString firstReceiveAddress() const;

    QString m_userField;
};

#endif // BITCOIN_QT_MININGPAGE_H
