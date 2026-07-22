// Copyright (c) 2023 The Coinyecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miningpage.h"
#include "ui_miningpage.h"

#include "clientmodel.h"
#include "walletmodel.h"
#include "addresstablemodel.h"
#include "platformstyle.h"

#include "chainparams.h"
#include "miner.h"
#include "stratum.h"

#include <boost/thread.hpp>

#include <QModelIndex>
#include <QMessageBox>
#include <QTimer>

MiningPage::MiningPage(const PlatformStyle *_platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MiningPage),
    clientModel(0),
    walletModel(0),
    platformStyle(_platformStyle),
    pollTimer(0)
{
    ui->setupUi(this);

    int nCores = (int)boost::thread::hardware_concurrency();
    if (nCores < 1)
        nCores = 1;

    ui->threadsSpinBox->setMinimum(1);
    ui->threadsSpinBox->setMaximum(nCores);
    // Default to leaving one core free so the UI stays responsive.
    ui->threadsSpinBox->setValue(nCores > 1 ? nCores - 1 : 1);
    ui->coreCountLabel->setText(tr("of %n core(s)", "", nCores));

    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(updateUI()));
    pollTimer->start(1000);

    updateUI();
}

MiningPage::~MiningPage()
{
    delete ui;
}

void MiningPage::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
}

void MiningPage::setWalletModel(WalletModel *_walletModel)
{
    this->walletModel = _walletModel;
    refreshExternalMinerInfo();
}

void MiningPage::startMining()
{
    int nThreads = ui->threadsSpinBox->value();
    // Non-consensus: found blocks are validated via ProcessNewBlock() exactly
    // like blocks from the network. Rewards go to a reserve key from this wallet.
    GenerateCoinyecoins(true, nThreads, Params());
}

void MiningPage::stopMining()
{
    GenerateCoinyecoins(false, 0, Params());
}

void MiningPage::on_toggleMiningButton_clicked()
{
    if (IsMiningActive())
        stopMining();
    else
        startMining();
    updateUI();
}

void MiningPage::on_toggleStratumButton_clicked()
{
    if (StratumServerRunning()) {
        StopStratumServer();
    } else if (!StartStratumServer()) {
        QMessageBox::warning(this, tr("Stratum server"),
            tr("Could not start the stratum server. The port may already be in use, or no "
               "payout address is available yet (open or unlock a wallet, or set "
               "-stratumaddress). See debug.log for details."));
    }
    updateUI();
}


QString MiningPage::firstReceiveAddress() const
{
    if (!walletModel)
        return QString();
    AddressTableModel *atm = walletModel->getAddressTableModel();
    if (!atm)
        return QString();
    for (int i = 0; i < atm->rowCount(QModelIndex()); ++i) {
        QModelIndex idx = atm->index(i, AddressTableModel::Address, QModelIndex());
        if (atm->data(idx, AddressTableModel::TypeRole).toString() == AddressTableModel::Receive)
            return atm->data(idx, Qt::DisplayRole).toString();
    }
    return QString();
}

void MiningPage::refreshExternalMinerInfo()
{
    // Port comes from the running node's stratum config (default 3333).
    int port = DEFAULT_STRATUM_PORT;
    UniValue info = GetStratumInfo();
    if (info.isObject() && info.exists("port") && info["port"].isNum())
        port = info["port"].get_int();

    const QString localUrl = QString("stratum+tcp://127.0.0.1:%1").arg(port);
    const QString lanUrl   = QString("stratum+tcp://YOUR-PC-LAN-IP:%1").arg(port);
    ui->stratumUrlLocalEdit->setText(localUrl);
    ui->stratumUrlLanEdit->setText(lanUrl);

    // Worker/username: a receive address from this wallet if we have one, so the
    // payout lands here. Any address works; any non-address text pays the node
    // default address. Password is not checked.
    QString addr = firstReceiveAddress();
    m_userField = addr.isEmpty() ? QString("YOUR-COYE-ADDRESS") : addr;
    ui->stratumUserEdit->setText(m_userField);
    ui->ccminerCmdEdit->setText(
        QString("ccminer -a scrypt -o %1 -u %2 -p x").arg(localUrl).arg(m_userField));
}

void MiningPage::updateUI()
{
    bool fMining = IsMiningActive();

    ui->threadsSpinBox->setEnabled(!fMining);
    ui->toggleMiningButton->setText(fMining ? tr("Stop Mining") : tr("Start Mining"));

    if (fMining) {
        ui->statusLabel->setText(tr("Mining with %n thread(s)", "", GetMiningThreadCount()));

        double hr = GetMiningHashRate();
        if (hr <= 0.0) {
            // The hash meter fills over a ~10s window; show a friendly message meanwhile.
            ui->hashRateLabel->setText(tr("measuring..."));
        } else {
            QString unit = "H/s";
            if (hr >= 1e6) { hr /= 1e6; unit = "MH/s"; }
            else if (hr >= 1e3) { hr /= 1e3; unit = "kH/s"; }
            ui->hashRateLabel->setText(QString("%1 %2").arg(hr, 0, 'f', 2).arg(unit));
        }
    } else {
        ui->statusLabel->setText(tr("Idle"));
        ui->hashRateLabel->setText("0.00 H/s");
    }

    // External-miner (stratum) live status + indicator + toggle button.
    {
        bool running = StratumServerRunning();
        UniValue info = GetStratumInfo();
        int clients = (info.isObject() && info.exists("clients") && info["clients"].isNum())
                      ? info["clients"].get_int() : 0;
        int port = (info.isObject() && info.exists("port") && info["port"].isNum())
                   ? info["port"].get_int() : DEFAULT_STRATUM_PORT;

        ui->stratumIndicator->setStyleSheet(
            running ? "background-color:#2ecc71; border-radius:7px;"     // green = running
                    : "background-color:#c0392b; border-radius:7px;");   // red = stopped
        ui->toggleStratumButton->setText(running ? tr("Stop Stratum server")
                                                  : tr("Start Stratum server"));
        if (running)
            ui->stratumStatusLabel->setText(
                tr("Stratum server: ON — listening on port %1, %2 client(s) connected.").arg(port).arg(clients));
        else
            ui->stratumStatusLabel->setText(
                tr("Stratum server: OFF — click \"Start Stratum server\" (or set stratum=1 in coinyecoin.conf) to let an external miner connect."));
    }
}
