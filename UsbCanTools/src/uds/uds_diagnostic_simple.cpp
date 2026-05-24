#include "uds/uds_diagnostic_simple.h"

#include "app/app_controller.h"
#include "can/can_worker_api.h"
#include "protocol/isotp.h"

#include <QAbstractItemView>
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
    RoleReq,
    RoleExp
};
}

UdsDiagnosticWidget::UdsDiagnosticWidget(AppController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();
    applyStyle();
    buildServiceTree();
    updateStatsView();

    connect(m_controller, &AppController::connectionChanged, this, &UdsDiagnosticWidget::onConnectionChanged);
    onConnectionChanged(false);
}

void UdsDiagnosticWidget::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    auto *topBar = new QHBoxLayout;
    topBar->addWidget(new QLabel(QStringLiteral("请求ID:"), this));
    m_txIdEdit = new QLineEdit(QStringLiteral("700"), this);
    m_txIdEdit->setMaximumWidth(70);
    topBar->addWidget(m_txIdEdit);
    topBar->addWidget(new QLabel(QStringLiteral("响应ID:"), this));
    m_rxIdEdit = new QLineEdit(QStringLiteral("701"), this);
    m_rxIdEdit->setMaximumWidth(70);
    topBar->addWidget(m_rxIdEdit);
    topBar->addWidget(new QLabel(QStringLiteral("超时(ms):"), this));
    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(100, 120000);
    m_timeoutSpin->setValue(3000);
    m_timeoutSpin->setMaximumWidth(88);
    topBar->addWidget(m_timeoutSpin);
    topBar->addStretch();
    mainLayout->addLayout(topBar);

    auto *mid = new QHBoxLayout;
    mid->setSpacing(6);

    m_serviceTree = new QTreeWidget(this);
    m_serviceTree->setHeaderLabel(QStringLiteral("服务"));
    m_serviceTree->setMinimumWidth(300);
    connect(m_serviceTree, &QTreeWidget::itemClicked, this, &UdsDiagnosticWidget::onServiceItemClicked);
    mid->addWidget(m_serviceTree, 32);

    auto *right = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    auto *reqGroup = new QGroupBox(QStringLiteral("请求编辑"), this);
    auto *reqGrid = new QGridLayout(reqGroup);
    reqGrid->addWidget(new QLabel(QStringLiteral("请求PDU:"), this), 0, 0);
    m_reqEdit = new QLineEdit(QStringLiteral("10 03"), this);
    reqGrid->addWidget(m_reqEdit, 0, 1, 1, 3);
    reqGrid->addWidget(new QLabel(QStringLiteral("期望响应:"), this), 1, 0);
    m_expEdit = new QLineEdit(QStringLiteral("50 03"), this);
    reqGrid->addWidget(m_expEdit, 1, 1, 1, 3);
    m_addStepBtn = new QPushButton(QStringLiteral("添加到流程"), this);
    m_sendOnceBtn = new QPushButton(QStringLiteral("立即发送"), this);
    reqGrid->addWidget(m_addStepBtn, 2, 2);
    reqGrid->addWidget(m_sendOnceBtn, 2, 3);
    connect(m_addStepBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onAddStep);
    connect(m_sendOnceBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onSendOnce);
    rightLayout->addWidget(reqGroup);

    auto *flowGroup = new QGroupBox(QStringLiteral("流程编辑"), this);
    auto *flowLayout = new QVBoxLayout(flowGroup);
    auto *flowBar = new QHBoxLayout;
    m_deleteStepBtn = new QPushButton(QStringLiteral("删除"), this);
    m_upStepBtn = new QPushButton(QStringLiteral("上移"), this);
    m_downStepBtn = new QPushButton(QStringLiteral("下移"), this);
    m_clearStepBtn = new QPushButton(QStringLiteral("清空"), this);
    flowBar->addWidget(m_deleteStepBtn);
    flowBar->addWidget(m_upStepBtn);
    flowBar->addWidget(m_downStepBtn);
    flowBar->addWidget(m_clearStepBtn);
    connect(m_deleteStepBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onDeleteStep);
    connect(m_upStepBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onMoveStepUp);
    connect(m_downStepBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onMoveStepDown);
    connect(m_clearStepBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onClearSteps);

    flowBar->addSpacing(8);
    flowBar->addWidget(new QLabel(QStringLiteral("循环:"), this));
    m_loopSpin = new QSpinBox(this);
    m_loopSpin->setRange(1, 9999);
    m_loopSpin->setValue(1);
    m_loopSpin->setMaximumWidth(70);
    flowBar->addWidget(m_loopSpin);
    flowBar->addWidget(new QLabel(QStringLiteral("间隔(ms):"), this));
    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(0, 60000);
    m_intervalSpin->setValue(1);
    m_intervalSpin->setMaximumWidth(80);
    flowBar->addWidget(m_intervalSpin);
    m_runFlowBtn = new QPushButton(QStringLiteral("执行流程"), this);
    flowBar->addWidget(m_runFlowBtn);
    connect(m_runFlowBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onRunFlow);
    flowBar->addStretch();
    flowLayout->addLayout(flowBar);

    m_flowTable = new QTableWidget(this);
    m_flowTable->setColumnCount(3);
    m_flowTable->setHorizontalHeaderLabels(QStringList() << QStringLiteral("名称")
                                                         << QStringLiteral("请求PDU")
                                                         << QStringLiteral("期望响应"));
    m_flowTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_flowTable->horizontalHeader()->setStretchLastSection(true);
    m_flowTable->verticalHeader()->setVisible(false);
    m_flowTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_flowTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_flowTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_flowTable->setColumnWidth(0, 180);
    m_flowTable->setColumnWidth(1, 320);
    flowLayout->addWidget(m_flowTable, 1);
    rightLayout->addWidget(flowGroup, 60);

    auto *logGroup = new QGroupBox(QStringLiteral("日志"), this);
    auto *logLayout = new QVBoxLayout(logGroup);
    auto *logBar = new QHBoxLayout;
    m_clearLogBtn = new QPushButton(QStringLiteral("清空日志"), this);
    connect(m_clearLogBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onClearLog);
    logBar->addWidget(m_clearLogBtn);
    logBar->addStretch();
    logLayout->addLayout(logBar);
    m_logEdit = new QPlainTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumBlockCount(8000);
    logLayout->addWidget(m_logEdit, 1);
    rightLayout->addWidget(logGroup, 40);

    mid->addWidget(right, 68);
    mainLayout->addLayout(mid, 1);

    auto *bottom = new QHBoxLayout;
    m_totalLabel = new QLabel(this);
    m_okLabel = new QLabel(this);
    m_failLabel = new QLabel(this);
    m_noRespLabel = new QLabel(this);
    m_resetStatsBtn = new QPushButton(QStringLiteral("重置统计"), this);
    connect(m_resetStatsBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onResetStats);
    bottom->addWidget(m_totalLabel);
    bottom->addWidget(m_okLabel);
    bottom->addWidget(m_failLabel);
    bottom->addWidget(m_noRespLabel);
    bottom->addStretch();
    bottom->addWidget(m_resetStatsBtn);
    mainLayout->addLayout(bottom);
}

void UdsDiagnosticWidget::buildServiceTree()
{
    m_serviceTree->clear();
    auto *g10 = new QTreeWidgetItem(m_serviceTree, QStringList(QStringLiteral("(10) 会话控制")));
    auto *g11 = new QTreeWidgetItem(m_serviceTree, QStringList(QStringLiteral("(11) ECU重置")));
    auto *g22 = new QTreeWidgetItem(m_serviceTree, QStringList(QStringLiteral("(22) 读数据标识符")));
    auto *g27 = new QTreeWidgetItem(m_serviceTree, QStringList(QStringLiteral("(27) 安全访问")));
    auto *g3E = new QTreeWidgetItem(m_serviceTree, QStringList(QStringLiteral("(3E) 保活")));

    auto addLeaf = [](QTreeWidgetItem *p, const QString &title, const QString &name, const QString &req, const QString &exp) {
        QTreeWidgetItem *n = new QTreeWidgetItem(p, QStringList(title));
        n->setData(0, RoleName, name);
        n->setData(0, RoleReq, req);
        n->setData(0, RoleExp, exp);
    };
    addLeaf(g10, QStringLiteral("03 - 扩展诊断会话"), QStringLiteral("扩展诊断会话"), QStringLiteral("10 03"), QStringLiteral("50 03"));
    addLeaf(g10, QStringLiteral("02 - 编程会话"), QStringLiteral("编程会话"), QStringLiteral("10 02"), QStringLiteral("50 02"));
    addLeaf(g11, QStringLiteral("01 - 软复位"), QStringLiteral("软复位"), QStringLiteral("11 01"), QStringLiteral("51 01"));
    addLeaf(g22, QStringLiteral("F1 90 - 读VIN"), QStringLiteral("读VIN"), QStringLiteral("22 F1 90"), QStringLiteral("62 F1 90"));
    addLeaf(g22, QStringLiteral("F1 87 - 读零件号"), QStringLiteral("读零件号"), QStringLiteral("22 F1 87"), QStringLiteral("62 F1 87"));
    addLeaf(g27, QStringLiteral("01 - 请求Seed"), QStringLiteral("请求Seed"), QStringLiteral("27 01"), QStringLiteral("67 01"));
    addLeaf(g3E, QStringLiteral("00 - TesterPresent"), QStringLiteral("TesterPresent"), QStringLiteral("3E 00"), QStringLiteral("7E 00"));

    m_serviceTree->expandAll();
    if (g10->childCount() > 0) {
        m_serviceTree->setCurrentItem(g10->child(0));
        onServiceItemClicked(g10->child(0), 0);
    }
}

void UdsDiagnosticWidget::onServiceItemClicked(QTreeWidgetItem *item, int)
{
    if (!item)
        return;
    const QString req = item->data(0, RoleReq).toString();
    if (req.isEmpty())
        return;
    m_reqEdit->setText(req);
    m_expEdit->setText(item->data(0, RoleExp).toString());
}

void UdsDiagnosticWidget::onAddStep()
{
    const QByteArray req = parseHexBytes(m_reqEdit->text().trimmed());
    if (req.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("请求PDU格式错误"));
        return;
    }

    const int row = m_flowTable->rowCount();
    m_flowTable->insertRow(row);
    QTreeWidgetItem *item = m_serviceTree->currentItem();
    const QString name = (item && !item->data(0, RoleName).toString().isEmpty())
                             ? item->data(0, RoleName).toString()
                             : QStringLiteral("自定义请求");
    m_flowTable->setItem(row, 0, new QTableWidgetItem(name));
    m_flowTable->setItem(row, 1, new QTableWidgetItem(m_reqEdit->text().trimmed().toUpper()));
    m_flowTable->setItem(row, 2, new QTableWidgetItem(m_expEdit->text().trimmed().toUpper()));
}

void UdsDiagnosticWidget::onDeleteStep()
{
    const int row = m_flowTable->currentRow();
    if (row >= 0)
        m_flowTable->removeRow(row);
}

void UdsDiagnosticWidget::onMoveStepUp()
{
    const int row = m_flowTable->currentRow();
    if (row <= 0)
        return;
    m_flowTable->insertRow(row - 1);
    for (int c = 0; c < m_flowTable->columnCount(); ++c)
        m_flowTable->setItem(row - 1, c, m_flowTable->takeItem(row + 1, c));
    m_flowTable->removeRow(row + 1);
    m_flowTable->selectRow(row - 1);
}

void UdsDiagnosticWidget::onMoveStepDown()
{
    const int row = m_flowTable->currentRow();
    if (row < 0 || row >= m_flowTable->rowCount() - 1)
        return;
    m_flowTable->insertRow(row + 2);
    for (int c = 0; c < m_flowTable->columnCount(); ++c)
        m_flowTable->setItem(row + 2, c, m_flowTable->takeItem(row, c));
    m_flowTable->removeRow(row);
    m_flowTable->selectRow(row + 1);
}

void UdsDiagnosticWidget::onClearSteps()
{
    m_flowTable->setRowCount(0);
}

void UdsDiagnosticWidget::onSendOnce()
{
    QTreeWidgetItem *item = m_serviceTree->currentItem();
    const QString name = (item && !item->data(0, RoleName).toString().isEmpty())
                             ? item->data(0, RoleName).toString()
                             : QStringLiteral("立即发送");
    sendUds(name, m_reqEdit->text().trimmed(), m_expEdit->text().trimmed(), true);
}

void UdsDiagnosticWidget::onRunFlow()
{
    if (m_flowTable->rowCount() <= 0) {
        QMessageBox::information(this, QStringLiteral("UDS诊断"), QStringLiteral("流程为空"));
        return;
    }

    const int loops = m_loopSpin->value();
    const int intervalMs = m_intervalSpin->value();

    for (int i = 0; i < loops; ++i) {
        appendLog(QStringLiteral("---- 执行流程 %1/%2 ----").arg(i + 1).arg(loops));
        for (int row = 0; row < m_flowTable->rowCount(); ++row) {
            const QString name = m_flowTable->item(row, 0) ? m_flowTable->item(row, 0)->text() : QStringLiteral("流程项");
            const QString req = m_flowTable->item(row, 1) ? m_flowTable->item(row, 1)->text() : QString();
            const QString exp = m_flowTable->item(row, 2) ? m_flowTable->item(row, 2)->text() : QString();
            sendUds(name, req, exp, true);
            if (intervalMs > 0 && !(i == loops - 1 && row == m_flowTable->rowCount() - 1))
                QThread::msleep(static_cast<unsigned long>(intervalMs));
            QCoreApplication::processEvents();
        }
    }
}

bool UdsDiagnosticWidget::sendUds(const QString &name,
                                  const QString &reqHex,
                                  const QString &expHex,
                                  bool checkResp)
{
    CanWorker *worker = m_controller ? m_controller->worker() : nullptr;
    if (!worker || !worker->isDeviceOpen()) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("请先在设备管理中连接硬件"));
        return false;
    }

    quint32 txId = 0;
    quint32 rxId = 0;
    if (!parseHexU32(m_txIdEdit->text(), &txId) || !parseHexU32(m_rxIdEdit->text(), &rxId)) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("请求ID/响应ID请输入十六进制"));
        return false;
    }

    const QByteArray req = parseHexBytes(reqHex);
    if (req.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("请求PDU格式错误"));
        return false;
    }

    ++m_totalCount;
    appendLog(QStringLiteral("TX [%1] %2").arg(name, toHexSpaced(req)));

    QByteArray resp;
    const bool ok = IsoTp::sendReceive(worker, txId, rxId, false, req, &resp, m_timeoutSpin->value());
    if (!ok) {
        appendLog(QStringLiteral("RX 超时或收发失败"));
        ++m_failCount;
        ++m_noRespCount;
        updateStatsView();
        return false;
    }

    appendLog(QStringLiteral("RX %1").arg(toHexSpaced(resp)));

    bool pass = true;
    if (checkResp) {
        const QByteArray exp = parseHexBytes(expHex);
        if (!exp.isEmpty()) {
            pass = resp.startsWith(exp);
        } else {
            pass = (!resp.isEmpty() && quint8(resp[0]) == quint8(req[0]) + 0x40u);
        }
    }

    if (pass) {
        ++m_okCount;
    } else {
        ++m_failCount;
        appendLog(QStringLiteral("响应校验失败"));
    }
    updateStatsView();
    return pass;
}

void UdsDiagnosticWidget::onClearLog()
{
    m_logEdit->clear();
}

void UdsDiagnosticWidget::onResetStats()
{
    m_totalCount = 0;
    m_okCount = 0;
    m_failCount = 0;
    m_noRespCount = 0;
    updateStatsView();
}

void UdsDiagnosticWidget::updateStatsView()
{
    m_totalLabel->setText(QStringLiteral("测试次数: %1").arg(m_totalCount));
    m_okLabel->setText(QStringLiteral("通过: %1").arg(m_okCount));
    m_failLabel->setText(QStringLiteral("未通过: %1").arg(m_failCount));
    m_noRespLabel->setText(QStringLiteral("未检索响应: %1").arg(m_noRespCount));
}

void UdsDiagnosticWidget::appendLog(const QString &line)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_logEdit->appendPlainText(QStringLiteral("[%1] %2").arg(ts, line));
}

bool UdsDiagnosticWidget::parseHexU32(const QString &text, quint32 *out) const
{
    bool ok = false;
    quint32 v = text.trimmed().toUInt(&ok, 16);
    if (ok && out)
        *out = v;
    return ok;
}

QByteArray UdsDiagnosticWidget::parseHexBytes(const QString &text) const
{
    QString t = text;
    t.remove(QLatin1Char(' '));
    t.remove(QLatin1Char('\t'));
    if (t.isEmpty() || (t.size() % 2) != 0)
        return QByteArray();
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
    updateSendButtons(connected);
    appendLog(connected ? QStringLiteral("设备已连接，发送功能可用") : QStringLiteral("设备未连接，发送功能不可用"));
}

void UdsDiagnosticWidget::updateSendButtons(bool connected)
{
    m_sendOnceBtn->setEnabled(connected);
    m_runFlowBtn->setEnabled(connected);
}

void UdsDiagnosticWidget::applyStyle()
{
    setStyleSheet(QStringLiteral(
        "QWidget { background:#CFD8E3; color:#1E2C3A; }"
        "QGroupBox { border:1px solid #9FB0C1; margin-top:6px; background:#E8EEF5; }"
        "QGroupBox::title { subcontrol-origin:margin; left:8px; padding:0 2px; }"
        "QLineEdit, QSpinBox, QTreeWidget, QTableWidget, QPlainTextEdit { border:1px solid #A9B7C7; background:#F8FAFC; min-height:20px; }"
        "QHeaderView::section { background:#D7E1EC; border:1px solid #B0C0CF; padding:4px; }"
        "QPushButton { background:#E6EEF7; border:1px solid #8EA4BC; border-radius:2px; padding:2px 8px; }"
        "QPushButton:hover { background:#D5E4F5; }"
        "QPushButton:pressed { background:#C6D9EE; }"
        "QPushButton:disabled { color:#8090A0; background:#E3E8EF; border-color:#AAB5C1; }"
        "QTreeWidget::item:selected, QTableWidget::item:selected { background:#BFD0E2; color:#1E2C3A; }"));
}
