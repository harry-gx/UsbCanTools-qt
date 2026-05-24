// 文件说明：CAN 收发面板实现，负责发送参数校验与日志展示。
#include "can/can_panel_widget.h"

#include "app/app_controller.h"
#include "can/can_worker_api.h"

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

CanPanelWidget::CanPanelWidget(AppController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();

    connect(m_sendBtn, &QPushButton::clicked, this, &CanPanelWidget::onSendClicked);
    connect(m_clearBtn, &QPushButton::clicked, this, &CanPanelWidget::onClearClicked);
    connect(m_controller, &AppController::connectionChanged, this, &CanPanelWidget::onConnectionChanged);
    connect(m_controller, &AppController::frameReceived, this, &CanPanelWidget::onFrameReceived);

    onConnectionChanged(false);
}

// 处理发送动作并执行参数校验。
void CanPanelWidget::onSendClicked()
{
    CanWorker *worker = m_controller->worker();
    if (!worker || !worker->isDeviceOpen()) {
        QMessageBox::information(this, QStringLiteral("CAN收发"), QStringLiteral("请先在设备管理中连接硬件设备。"));
        return;
    }

    const bool ext = m_extCombo->currentIndex() == 1;
    const bool rtr = m_rtrCombo->currentIndex() == 1;

    bool ok = false;
    const quint32 id = parseCanIdHex(m_idEdit->text(), &ok);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("CAN收发"), QStringLiteral("ID 请输入十六进制。"));
        return;
    }

    if (!ext && id > 0x7FFu) {
        QMessageBox::warning(this, QStringLiteral("CAN收发"), QStringLiteral("标准帧 ID 范围为 0x000~0x7FF。"));
        return;
    }

    QByteArray data = parseHexBytes(m_dataEdit->text());
    if (!rtr && data.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("CAN收发"), QStringLiteral("数据帧至少需要 1 字节数据。"));
        return;
    }
    if (rtr)
        data.clear();

    worker->transmitFrame(id, data, ext, rtr);
}

// 清空日志与统计计数。
void CanPanelWidget::onClearClicked()
{
    m_table->setRowCount(0);
    m_txCount = 0;
    m_rxCount = 0;
    m_statsLabel->setText(QStringLiteral("TX: 0  RX: 0"));
}

// 根据连接状态控制发送按钮是否可用。
void CanPanelWidget::onConnectionChanged(bool connected)
{
    m_sendBtn->setEnabled(connected);
}

// 收到帧后追加到表格并更新统计信息。
void CanPanelWidget::onFrameReceived(const QString &timeMs, const QString &dir, const QString &idHex,
                                     const QString &frameType, const QString &dlc, const QString &dataHex)
{
    if (dir == QStringLiteral("TX"))
        ++m_txCount;
    else if (dir == QStringLiteral("RX"))
        ++m_rxCount;
    m_statsLabel->setText(QStringLiteral("TX: %1  RX: %2").arg(m_txCount).arg(m_rxCount));

    while (m_table->rowCount() >= kMaxRows)
        m_table->removeRow(0);

    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(timeMs));
    m_table->setItem(row, 1, new QTableWidgetItem(dir));
    m_table->setItem(row, 2, new QTableWidgetItem(idHex));
    m_table->setItem(row, 3, new QTableWidgetItem(frameType));
    m_table->setItem(row, 4, new QTableWidgetItem(dlc));
    m_table->setItem(row, 5, new QTableWidgetItem(dataHex));
    m_table->scrollToBottom();
}

// 构建 CAN 面板界面。
void CanPanelWidget::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    auto *txBox = new QGroupBox(QStringLiteral("CAN发送"), this);
    auto *txGrid = new QGridLayout(txBox);

    m_idEdit = new QLineEdit(QStringLiteral("511"), txBox);
    m_dataEdit = new QLineEdit(QStringLiteral("02 10 02 00 00 00 00 00"), txBox);
    m_extCombo = new QComboBox(txBox);
    m_extCombo->addItems(QStringList() << QStringLiteral("标准帧") << QStringLiteral("扩展帧"));
    m_rtrCombo = new QComboBox(txBox);
    m_rtrCombo->addItems(QStringList() << QStringLiteral("数据帧") << QStringLiteral("远程帧"));
    m_sendBtn = new QPushButton(QStringLiteral("发送"), txBox);

    txGrid->addWidget(new QLabel(QStringLiteral("ID (HEX)"), txBox), 0, 0);
    txGrid->addWidget(m_idEdit, 0, 1);
    txGrid->addWidget(new QLabel(QStringLiteral("帧格式"), txBox), 0, 2);
    txGrid->addWidget(m_extCombo, 0, 3);
    txGrid->addWidget(new QLabel(QStringLiteral("数据 (HEX)"), txBox), 1, 0);
    txGrid->addWidget(m_dataEdit, 1, 1);
    txGrid->addWidget(new QLabel(QStringLiteral("帧类型"), txBox), 1, 2);
    txGrid->addWidget(m_rtrCombo, 1, 3);
    txGrid->addWidget(m_sendBtn, 0, 4, 2, 1);

    auto *rxBox = new QGroupBox(QStringLiteral("CAN收发日志"), this);
    auto *rxLayout = new QVBoxLayout(rxBox);

    auto *topRow = new QHBoxLayout;
    m_clearBtn = new QPushButton(QStringLiteral("清空日志"), rxBox);
    m_statsLabel = new QLabel(QStringLiteral("TX: 0  RX: 0"), rxBox);
    topRow->addWidget(m_clearBtn);
    topRow->addStretch();
    topRow->addWidget(m_statsLabel);

    m_table = new QTableWidget(rxBox);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(QStringList()
                                       << QStringLiteral("时间(ms)")
                                       << QStringLiteral("方向")
                                       << QStringLiteral("帧ID")
                                       << QStringLiteral("类型")
                                       << QStringLiteral("DLC")
                                       << QStringLiteral("数据"));
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setColumnWidth(0, 90);
    m_table->setColumnWidth(1, 60);
    m_table->setColumnWidth(2, 90);
    m_table->setColumnWidth(3, 100);
    m_table->setColumnWidth(4, 50);

    rxLayout->addLayout(topRow);
    rxLayout->addWidget(m_table);

    mainLayout->addWidget(txBox);
    mainLayout->addWidget(rxBox, 1);
}

// 解析 HEX 文本为字节数组（最多 8 字节）。
QByteArray CanPanelWidget::parseHexBytes(const QString &text) const
{
    QByteArray out;
    const QStringList parts = text.simplified().split(QLatin1Char(' '), QString::SkipEmptyParts);
    for (const QString &part : parts) {
        if (out.size() >= 8)
            break;
        bool ok = false;
        const int value = part.toInt(&ok, 16);
        if (ok && value >= 0 && value <= 255)
            out.append(char(value));
    }
    return out;
}

// 解析 HEX 格式的 CAN ID。
quint32 CanPanelWidget::parseCanIdHex(const QString &text, bool *ok) const
{
    return text.trimmed().toUInt(ok, 16);
}
