#include "uds/uds_diagnostic_zcan.h"

#include "app/app_controller.h"
#include "can/can_worker_api.h"
#include "protocol/isotp.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QtGlobal>

namespace {
enum ServiceRole {
    RoleName = Qt::UserRole + 1,
    RoleRequestHex,
    RoleResponseHex
};

void setButtonCompact(QPushButton *btn)
{
    btn->setFixedHeight(24);
    btn->setMinimumWidth(62);
}
}

UdsDiagnosticWidget::UdsDiagnosticWidget(AppController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();
    applyStyle();
    buildServiceTree();
    updateCountersView();

    connect(m_controller, &AppController::connectionChanged, this, &UdsDiagnosticWidget::onConnectionChanged);
    onConnectionChanged(false);
}

void UdsDiagnosticWidget::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(5);

    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(4);
    topRow->addWidget(new QLabel(QStringLiteral("通道: "), this));

    m_channelCombo = new QComboBox(this);
    m_channelCombo->addItem(QStringLiteral("0"));
    m_channelCombo->setFixedHeight(22);
    m_channelCombo->setMinimumWidth(76);
    topRow->addWidget(m_channelCombo);

    auto *subComboA = new QComboBox(this);
    subComboA->addItem(QStringLiteral(" "));
    subComboA->setFixedHeight(22);
    subComboA->setMinimumWidth(64);
    topRow->addWidget(subComboA);

    topRow->addWidget(new QLabel(QStringLiteral("地址: "), this));
    m_txIdEdit = new QLineEdit(QStringLiteral("700"), this);
    m_txIdEdit->setFixedHeight(22);
    m_txIdEdit->setMaximumWidth(58);
    topRow->addWidget(m_txIdEdit);

    m_funcAddrEdit = new QLineEdit(QStringLiteral("7DF"), this);
    m_funcAddrEdit->setFixedHeight(22);
    m_funcAddrEdit->setMaximumWidth(58);
    topRow->addWidget(m_funcAddrEdit);

    m_rxIdEdit = new QLineEdit(QStringLiteral("701"), this);
    m_rxIdEdit->setFixedHeight(22);
    m_rxIdEdit->setMaximumWidth(58);
    topRow->addWidget(m_rxIdEdit);

    m_addrModeCombo = new QComboBox(this);
    m_addrModeCombo->addItem(QStringLiteral("物理地址"));
    m_addrModeCombo->addItem(QStringLiteral("功能地址"));
    m_addrModeCombo->setFixedHeight(22);
    m_addrModeCombo->setMinimumWidth(88);
    topRow->addWidget(m_addrModeCombo);

    auto *moreBtn = new QPushButton(QStringLiteral("更多设置"), this);
    setButtonCompact(moreBtn);
    topRow->addWidget(moreBtn);

    m_autoFlowCheck = new QCheckBox(QStringLiteral("自动流控"), this);
    topRow->addWidget(m_autoFlowCheck);

    topRow->addStretch();

    auto *serviceEditBtn = new QPushButton(QStringLiteral("服务编辑器"), this);
    auto *didEditBtn = new QPushButton(QStringLiteral("故障码编辑器"), this);
    setButtonCompact(serviceEditBtn);
    setButtonCompact(didEditBtn);
    topRow->addWidget(serviceEditBtn);
    topRow->addWidget(didEditBtn);
    mainLayout->addLayout(topRow);

    auto *midLayout = new QHBoxLayout;
    midLayout->setSpacing(6);

    m_serviceTree = new QTreeWidget(this);
    m_serviceTree->setHeaderLabel(QStringLiteral("[系统定义]"));
    m_serviceTree->setMinimumWidth(300);
    m_serviceTree->setColumnCount(1);
    connect(m_serviceTree, &QTreeWidget::itemClicked, this, &UdsDiagnosticWidget::onServiceItemClicked);
    midLayout->addWidget(m_serviceTree, 28);

    auto *rightPanel = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setSpacing(4);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    auto *reqGroup = new QGroupBox(this);
    auto *reqGrid = new QGridLayout(reqGroup);
    reqGrid->setContentsMargins(6, 6, 6, 6);
    reqGrid->setHorizontalSpacing(4);
    reqGrid->setVerticalSpacing(4);

    reqGrid->addWidget(new QLabel(QStringLiteral("请求PDU: "), this), 0, 0);
    m_requestEdit = new QLineEdit(QStringLiteral("10 03"), this);
    m_requestEdit->setPlaceholderText(QStringLiteral("例如: 10 03 / 22 F1 90"));
    reqGrid->addWidget(m_requestEdit, 0, 1);
    reqGrid->addWidget(new QLabel(QStringLiteral("抑制响应: "), this), 0, 2);
    m_suppressCombo = new QComboBox(this);
    m_suppressCombo->addItem(QStringLiteral("否"));
    m_suppressCombo->addItem(QStringLiteral("是"));
    m_suppressCombo->setFixedHeight(22);
    reqGrid->addWidget(m_suppressCombo, 0, 3);

    reqGrid->addWidget(new QLabel(QStringLiteral("响应PDU: "), this), 1, 0);
    m_expectedRespEdit = new QLineEdit(QStringLiteral("50 03"), this);
    reqGrid->addWidget(m_expectedRespEdit, 1, 1);
    reqGrid->addWidget(new QLabel(QStringLiteral("校验响应: "), this), 1, 2);
    m_checkRespCombo = new QComboBox(this);
    m_checkRespCombo->addItem(QStringLiteral("否"));
    m_checkRespCombo->addItem(QStringLiteral("是"));
    m_checkRespCombo->setCurrentIndex(1);
    m_checkRespCombo->setFixedHeight(22);
    reqGrid->addWidget(m_checkRespCombo, 1, 3);

    auto *reqBtns = new QHBoxLayout;
    reqBtns->setSpacing(4);
    m_addToListBtn = new QPushButton(QStringLiteral("添加到列表"), this);
    m_sendNowBtn = new QPushButton(QStringLiteral("立即发送"), this);
    setButtonCompact(m_addToListBtn);
    setButtonCompact(m_sendNowBtn);
    reqBtns->addWidget(m_addToListBtn);
    reqBtns->addWidget(m_sendNowBtn);
    reqBtns->addStretch();
    reqGrid->addLayout(reqBtns, 2, 1, 1, 3);
    connect(m_addToListBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onAddCurrentToList);
    connect(m_sendNowBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onSendNow);
    rightLayout->addWidget(reqGroup);

    auto *listGroup = new QGroupBox(this);
    auto *listLayout = new QVBoxLayout(listGroup);
    listLayout->setContentsMargins(6, 6, 6, 6);
    listLayout->setSpacing(4);

    auto *listToolbar = new QHBoxLayout;
    listToolbar->setSpacing(4);
    m_listDeleteBtn = new QPushButton(QStringLiteral("删除"), this);
    m_listClearBtn = new QPushButton(QStringLiteral("清空"), this);
    m_listUpBtn = new QPushButton(QStringLiteral("上移"), this);
    m_listDownBtn = new QPushButton(QStringLiteral("下移"), this);
    for (QPushButton *btn : QList<QPushButton *>() << m_listDeleteBtn << m_listClearBtn << m_listUpBtn << m_listDownBtn)
        setButtonCompact(btn);
    listToolbar->addWidget(m_listDeleteBtn);
    listToolbar->addWidget(m_listClearBtn);
    listToolbar->addWidget(m_listUpBtn);
    listToolbar->addWidget(m_listDownBtn);
    connect(m_listDeleteBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onListDelete);
    connect(m_listClearBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onListClear);
    connect(m_listUpBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onListMoveUp);
    connect(m_listDownBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onListMoveDown);

    listToolbar->addSpacing(8);
    listToolbar->addWidget(new QLabel(QStringLiteral("循环次数: "), this));
    m_repeatSpin = new QSpinBox(this);
    m_repeatSpin->setRange(1, 9999);
    m_repeatSpin->setValue(1);
    m_repeatSpin->setFixedHeight(22);
    m_repeatSpin->setMaximumWidth(72);
    listToolbar->addWidget(m_repeatSpin);

    listToolbar->addWidget(new QLabel(QStringLiteral("请求间隔(ms): "), this));
    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(0, 60000);
    m_intervalSpin->setValue(1);
    m_intervalSpin->setFixedHeight(22);
    m_intervalSpin->setMaximumWidth(76);
    listToolbar->addWidget(m_intervalSpin);

    m_listSendBtn = new QPushButton(QStringLiteral("列表发送"), this);
    setButtonCompact(m_listSendBtn);
    listToolbar->addWidget(m_listSendBtn);
    connect(m_listSendBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onListSend);
    listToolbar->addStretch();
    listLayout->addLayout(listToolbar);

    m_listTable = new QTableWidget(this);
    m_listTable->setColumnCount(5);
    m_listTable->setHorizontalHeaderLabels(QStringList()
                                           << QStringLiteral("名称")
                                           << QStringLiteral("请求PDU")
                                           << QStringLiteral("请求地址")
                                           << QStringLiteral("抑制响应")
                                           << QStringLiteral("校验响应"));
    m_listTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_listTable->horizontalHeader()->setStretchLastSection(true);
    m_listTable->verticalHeader()->setVisible(false);
    m_listTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_listTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listTable->setAlternatingRowColors(true);
    m_listTable->setColumnWidth(0, 170);
    m_listTable->setColumnWidth(1, 290);
    m_listTable->setColumnWidth(2, 120);
    m_listTable->setColumnWidth(3, 80);
    m_listTable->setColumnWidth(4, 80);
    listLayout->addWidget(m_listTable, 1);
    rightLayout->addWidget(listGroup, 45);

    auto *logGroup = new QGroupBox(this);
    auto *logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(6, 6, 6, 6);
    logLayout->setSpacing(4);

    auto *logToolbar = new QHBoxLayout;
    logToolbar->addWidget(new QLabel(QStringLiteral("十六进制"), this));
    m_logDisplayCombo = new QComboBox(this);
    m_logDisplayCombo->addItem(QStringLiteral(" "));
    m_logDisplayCombo->setFixedHeight(22);
    m_logDisplayCombo->setMaximumWidth(72);
    logToolbar->addWidget(m_logDisplayCombo);

    auto *logExportBtn = new QPushButton(QStringLiteral("导出"), this);
    auto *logRealSaveBtn = new QPushButton(QStringLiteral("实时保存"), this);
    m_logClearBtn = new QPushButton(QStringLiteral("清空"), this);
    setButtonCompact(logExportBtn);
    setButtonCompact(logRealSaveBtn);
    setButtonCompact(m_logClearBtn);
    logToolbar->addWidget(logExportBtn);
    logToolbar->addWidget(logRealSaveBtn);
    logToolbar->addWidget(m_logClearBtn);
    logToolbar->addStretch();
    connect(m_logClearBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onClearLog);
    logLayout->addLayout(logToolbar);

    m_logEdit = new QPlainTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumBlockCount(8000);
    logLayout->addWidget(m_logEdit, 1);
    rightLayout->addWidget(logGroup, 35);

    midLayout->addWidget(rightPanel, 72);
    mainLayout->addLayout(midLayout, 1);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->setSpacing(24);
    m_totalCountLabel = new QLabel(this);
    m_passCountLabel = new QLabel(this);
    m_failCountLabel = new QLabel(this);
    m_noRespCountLabel = new QLabel(this);
    m_resetStatsBtn = new QPushButton(QStringLiteral("重置"), this);
    setButtonCompact(m_resetStatsBtn);
    connect(m_resetStatsBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onResetCounters);

    bottomRow->addWidget(m_totalCountLabel);
    bottomRow->addWidget(m_passCountLabel);
    bottomRow->addWidget(m_failCountLabel);
    bottomRow->addWidget(m_noRespCountLabel);
    bottomRow->addStretch();
    bottomRow->addWidget(m_resetStatsBtn);
    mainLayout->addLayout(bottomRow);
}

void UdsDiagnosticWidget::buildServiceTree()
{
    m_serviceTree->clear();

    auto *grp10 = new QTreeWidgetItem(m_serviceTree, QStringList(QStringLiteral("(10) 诊断会话控制")));
    auto *grp11 = new QTreeWidgetItem(m_serviceTree, QStringList(QStringLiteral("(11) ECU重置")));
    auto *grp14 = new QTreeWidgetItem(m_serviceTree, QStringList(QStringLiteral("(14) 清除诊断信息")));
    auto *grp19 = new QTreeWidgetItem(m_serviceTree, QStringList(QStringLiteral("(19) 读DTC信息")));
    auto *grp22 = new QTreeWidgetItem(m_serviceTree, QStringList(QStringLiteral("(22) 读数据标识符")));
    auto *grp27 = new QTreeWidgetItem(m_serviceTree, QStringList(QStringLiteral("(27) 安全访问")));
    auto *grp28 = new QTreeWidgetItem(m_serviceTree, QStringList(QStringLiteral("(28) 通信控制")));
    auto *grp2E = new QTreeWidgetItem(m_serviceTree, QStringList(QStringLiteral("(2E) 写数据标识符")));
    auto *grp3E = new QTreeWidgetItem(m_serviceTree, QStringList(QStringLiteral("(3E) 链路控制")));

    auto addLeaf = [](QTreeWidgetItem *parent,
                      const QString &label,
                      const QString &name,
                      const QString &req,
                      const QString &resp) {
        auto *leaf = new QTreeWidgetItem(parent, QStringList(label));
        leaf->setData(0, RoleName, name);
        leaf->setData(0, RoleRequestHex, req);
        leaf->setData(0, RoleResponseHex, resp);
    };

    addLeaf(grp10, QStringLiteral("01 - 默认会话"), QStringLiteral("默认会话"), QStringLiteral("10 01"), QStringLiteral("50 01"));
    addLeaf(grp10, QStringLiteral("03 - 扩展诊断会话"), QStringLiteral("扩展诊断会话"), QStringLiteral("10 03"), QStringLiteral("50 03"));
    addLeaf(grp11, QStringLiteral("01 - 软复位"), QStringLiteral("ECU软复位"), QStringLiteral("11 01"), QStringLiteral("51 01"));
    addLeaf(grp14, QStringLiteral("FF FF FF - 清除全部DTC"), QStringLiteral("清除全部DTC"), QStringLiteral("14 FF FF FF"), QStringLiteral("54"));
    addLeaf(grp19, QStringLiteral("02 FF - 读取DTC"), QStringLiteral("读取DTC"), QStringLiteral("19 02 FF"), QStringLiteral("59"));
    addLeaf(grp22, QStringLiteral("F1 90 - 读VIN"), QStringLiteral("读VIN"), QStringLiteral("22 F1 90"), QStringLiteral("62 F1 90"));
    addLeaf(grp22, QStringLiteral("F1 87 - 读零件号"), QStringLiteral("读零件号"), QStringLiteral("22 F1 87"), QStringLiteral("62 F1 87"));
    addLeaf(grp27, QStringLiteral("01 - 请求Seed"), QStringLiteral("请求Seed"), QStringLiteral("27 01"), QStringLiteral("67 01"));
    addLeaf(grp27, QStringLiteral("02 - 发送Key"), QStringLiteral("发送Key"), QStringLiteral("27 02 00 00 00 00"), QStringLiteral("67 02"));
    addLeaf(grp28, QStringLiteral("00 - 启用Rx和Tx"), QStringLiteral("启用通信"), QStringLiteral("28 00 03"), QStringLiteral("68 00"));
    addLeaf(grp2E, QStringLiteral("F1 90 - 写DID"), QStringLiteral("写DID"), QStringLiteral("2E F1 90 00 01"), QStringLiteral("6E F1 90"));
    addLeaf(grp3E, QStringLiteral("00 - 保活"), QStringLiteral("TesterPresent"), QStringLiteral("3E 00"), QStringLiteral("7E 00"));

    m_serviceTree->expandToDepth(0);
    if (grp10->childCount() > 0)
        onServiceItemClicked(grp10->child(0), 0);
}

void UdsDiagnosticWidget::onServiceItemClicked(QTreeWidgetItem *item, int)
{
    if (!item)
        return;
    const QString req = item->data(0, RoleRequestHex).toString();
    if (req.isEmpty())
        return;
    m_requestEdit->setText(req);
    m_expectedRespEdit->setText(item->data(0, RoleResponseHex).toString());
}

void UdsDiagnosticWidget::addListRow(const QString &name,
                                     const QString &requestPduHex,
                                     const QString &requestAddr,
                                     bool suppressResponse,
                                     bool checkResponse,
                                     const QString &expectedResponsePduHex)
{
    const int row = m_listTable->rowCount();
    m_listTable->insertRow(row);
    m_listTable->setItem(row, 0, new QTableWidgetItem(name));
    m_listTable->setItem(row, 1, new QTableWidgetItem(requestPduHex.toUpper()));
    m_listTable->setItem(row, 2, new QTableWidgetItem(requestAddr));
    m_listTable->setItem(row, 3, new QTableWidgetItem(suppressResponse ? QStringLiteral("是") : QStringLiteral("否")));
    auto *checkItem = new QTableWidgetItem(checkResponse ? QStringLiteral("是") : QStringLiteral("否"));
    checkItem->setData(Qt::UserRole, expectedResponsePduHex.toUpper());
    m_listTable->setItem(row, 4, checkItem);
}

void UdsDiagnosticWidget::onAddCurrentToList()
{
    const QString reqHex = m_requestEdit->text().trimmed();
    if (parseHexBytes(reqHex).isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("请求PDU格式错误，请输入十六进制字节串"));
        return;
    }

    QTreeWidgetItem *item = m_serviceTree->currentItem();
    const QString name = (item && !item->data(0, RoleName).toString().isEmpty())
                             ? item->data(0, RoleName).toString()
                             : QStringLiteral("自定义请求");

    const QString requestAddr = QStringLiteral("%1/%2")
                                    .arg(m_txIdEdit->text().trimmed().toUpper(),
                                         m_rxIdEdit->text().trimmed().toUpper());

    addListRow(name,
               reqHex,
               requestAddr,
               m_suppressCombo->currentText() == QStringLiteral("是"),
               m_checkRespCombo->currentText() == QStringLiteral("是"),
               m_expectedRespEdit->text().trimmed());
}

void UdsDiagnosticWidget::onSendNow()
{
    QTreeWidgetItem *item = m_serviceTree->currentItem();
    const QString name = (item && !item->data(0, RoleName).toString().isEmpty())
                             ? item->data(0, RoleName).toString()
                             : QStringLiteral("立即发送");

    sendRequest(name,
                m_requestEdit->text().trimmed(),
                m_expectedRespEdit->text().trimmed(),
                m_suppressCombo->currentText() == QStringLiteral("是"),
                m_checkRespCombo->currentText() == QStringLiteral("是"));
}

bool UdsDiagnosticWidget::sendRequest(const QString &name,
                                      const QString &requestPduHex,
                                      const QString &expectedResponsePduHex,
                                      bool suppressResponse,
                                      bool checkResponse)
{
    CanWorker *worker = m_controller ? m_controller->worker() : nullptr;
    if (!worker || !worker->isDeviceOpen()) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("请先连接CAN设备"));
        return false;
    }

    quint32 txId = 0;
    quint32 rxId = 0;
    if (!parseHexU32(m_txIdEdit->text(), &txId) || !parseHexU32(m_rxIdEdit->text(), &rxId)) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("请求ID或响应ID格式错误，请输入十六进制"));
        return false;
    }

    const QByteArray req = parseHexBytes(requestPduHex);
    if (req.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("请求PDU格式错误"));
        return false;
    }

    ++m_totalCount;
    appendLog(QStringLiteral("TX [%1] %2")
                  .arg(name, toHexSpaced(req)));

    const bool ext = false;
    if (suppressResponse) {
        if (req.size() > 7) {
            appendLog(QStringLiteral("失败: 抑制响应模式当前仅支持单帧请求"));
            ++m_failCount;
            updateCountersView();
            return false;
        }
        QByteArray sf(8, char(0x00));
        sf[0] = char(req.size() & 0x0F);
        for (int i = 0; i < req.size(); ++i)
            sf[1 + i] = req[i];
        worker->transmitFrame(txId, sf, ext, false);
        appendLog(QStringLiteral("发送完成(抑制响应)"));
        ++m_passCount;
        updateCountersView();
        return true;
    }

    QByteArray resp;
    const bool ok = IsoTp::sendReceive(worker,
                                       txId,
                                       rxId,
                                       ext,
                                       req,
                                       &resp,
                                       m_timeoutSpin->value());
    if (!ok) {
        appendLog(QStringLiteral("RX 超时或收发失败"));
        ++m_failCount;
        ++m_noRespCount;
        updateCountersView();
        return false;
    }

    appendLog(QStringLiteral("RX %1").arg(toHexSpaced(resp)));

    bool passed = true;
    if (checkResponse) {
        const QByteArray expected = parseHexBytes(expectedResponsePduHex);
        if (!expected.isEmpty()) {
            passed = resp.startsWith(expected);
        } else {
            passed = (!req.isEmpty() && !resp.isEmpty() && quint8(resp[0]) == quint8(req[0]) + 0x40u);
        }
    }

    if (passed) {
        ++m_passCount;
    } else {
        ++m_failCount;
        appendLog(QStringLiteral("响应校验失败"));
    }

    updateCountersView();
    return passed;
}

void UdsDiagnosticWidget::onListSend()
{
    const int rows = m_listTable->rowCount();
    if (rows <= 0) {
        QMessageBox::information(this, QStringLiteral("UDS诊断"), QStringLiteral("发送列表为空"));
        return;
    }

    const int loops = m_repeatSpin->value();
    const int intervalMs = m_intervalSpin->value();

    setEnabled(false);
    qApp->processEvents();

    for (int i = 0; i < loops; ++i) {
        appendLog(QStringLiteral("---- 开始第 %1/%2 轮列表发送 ----").arg(i + 1).arg(loops));
        for (int row = 0; row < rows; ++row) {
            const QString name = m_listTable->item(row, 0) ? m_listTable->item(row, 0)->text() : QStringLiteral("列表请求");
            const QString req = m_listTable->item(row, 1) ? m_listTable->item(row, 1)->text() : QString();
            const bool suppress = m_listTable->item(row, 3) && m_listTable->item(row, 3)->text() == QStringLiteral("是");
            const bool check = m_listTable->item(row, 4) && m_listTable->item(row, 4)->text() == QStringLiteral("是");
            const QString expect = m_listTable->item(row, 4) ? m_listTable->item(row, 4)->data(Qt::UserRole).toString() : QString();

            sendRequest(name, req, expect, suppress, check);

            if (intervalMs > 0 && !(row == rows - 1 && i == loops - 1))
                QThread::msleep(static_cast<unsigned long>(intervalMs));

            QCoreApplication::processEvents();
        }
    }

    setEnabled(true);
    updateUiEnabled(m_controller && m_controller->isConnected());
}

void UdsDiagnosticWidget::onListDelete()
{
    const int row = m_listTable->currentRow();
    if (row >= 0)
        m_listTable->removeRow(row);
}

void UdsDiagnosticWidget::onListClear()
{
    m_listTable->setRowCount(0);
}

void UdsDiagnosticWidget::onListMoveUp()
{
    const int row = m_listTable->currentRow();
    if (row <= 0)
        return;

    m_listTable->insertRow(row - 1);
    for (int c = 0; c < m_listTable->columnCount(); ++c)
        m_listTable->setItem(row - 1, c, m_listTable->takeItem(row + 1, c));
    m_listTable->removeRow(row + 1);
    m_listTable->selectRow(row - 1);
}

void UdsDiagnosticWidget::onListMoveDown()
{
    const int row = m_listTable->currentRow();
    if (row < 0 || row >= m_listTable->rowCount() - 1)
        return;

    m_listTable->insertRow(row + 2);
    for (int c = 0; c < m_listTable->columnCount(); ++c)
        m_listTable->setItem(row + 2, c, m_listTable->takeItem(row, c));
    m_listTable->removeRow(row);
    m_listTable->selectRow(row + 1);
}

void UdsDiagnosticWidget::onClearLog()
{
    m_logEdit->clear();
}

void UdsDiagnosticWidget::onResetCounters()
{
    m_totalCount = 0;
    m_passCount = 0;
    m_failCount = 0;
    m_noRespCount = 0;
    updateCountersView();
}

void UdsDiagnosticWidget::updateCountersView()
{
    m_totalCountLabel->setText(QStringLiteral("测试次数: %1").arg(m_totalCount));
    m_passCountLabel->setText(QStringLiteral("通过: %1").arg(m_passCount));
    m_failCountLabel->setText(QStringLiteral("未通过: %1").arg(m_failCount));
    m_noRespCountLabel->setText(QStringLiteral("未检索响应: %1").arg(m_noRespCount));
}

void UdsDiagnosticWidget::appendLog(const QString &line)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_logEdit->appendPlainText(QStringLiteral("[%1] %2").arg(ts, line));
}

bool UdsDiagnosticWidget::parseHexU32(const QString &text, quint32 *out) const
{
    bool ok = false;
    const quint32 value = text.trimmed().toUInt(&ok, 16);
    if (ok && out)
        *out = value;
    return ok;
}

QByteArray UdsDiagnosticWidget::parseHexBytes(const QString &text) const
{
    QString t = text;
    t.remove(QLatin1Char(' '));
    t.remove(QLatin1Char('\t'));
    if (t.isEmpty() || (t.size() % 2) != 0)
        return {};
    return QByteArray::fromHex(t.toLatin1());
}

QString UdsDiagnosticWidget::toHexSpaced(const QByteArray &bytes) const
{
    QString s;
    for (int i = 0; i < bytes.size(); ++i) {
        if (i > 0)
            s += QLatin1Char(' ');
        s += QStringLiteral("%1").arg(quint8(bytes.at(i)), 2, 16, QChar('0')).toUpper();
    }
    return s;
}

void UdsDiagnosticWidget::onConnectionChanged(bool connected)
{
    updateUiEnabled(connected);
    appendLog(connected ? QStringLiteral("设备已连接，UDS诊断可用") : QStringLiteral("设备未连接，UDS诊断发送禁用"));
}

void UdsDiagnosticWidget::updateUiEnabled(bool connected)
{
    m_sendNowBtn->setEnabled(connected);
    m_addToListBtn->setEnabled(connected);
    m_listSendBtn->setEnabled(connected);
}

void UdsDiagnosticWidget::applyStyle()
{
    setStyleSheet(QStringLiteral(
        "QWidget { background:#CFD8E3; color:#1E2C3A; }"
        "QGroupBox { border:1px solid #9FB0C1; margin-top:6px; background:#E8EEF5; }"
        "QGroupBox::title { subcontrol-origin:margin; left:8px; padding:0 2px; }"
        "QLineEdit, QComboBox, QSpinBox, QTreeWidget, QTableWidget, QPlainTextEdit {"
        "  border:1px solid #A9B7C7; background:#F8FAFC; min-height:20px; }"
        "QHeaderView::section { background:#D7E1EC; border:1px solid #B0C0CF; padding:4px; }"
        "QPushButton { background:#E6EEF7; border:1px solid #8EA4BC; border-radius:2px; padding:2px 8px; }"
        "QPushButton:hover { background:#D5E4F5; }"
        "QPushButton:pressed { background:#C6D9EE; }"
        "QPushButton:disabled { color:#8090A0; background:#E3E8EF; border-color:#AAB5C1; }"
        "QTreeWidget::item:selected, QTableWidget::item:selected { background:#BFD0E2; color:#1E2C3A; }"));
}
