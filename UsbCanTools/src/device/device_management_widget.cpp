// 文件说明：设备管理面板实现，负责连接参数配置与连接状态刷新。
#include "device/device_management_widget.h"

#include "app/app_controller.h"
#include "can/can_worker_api.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

DeviceManagementWidget::DeviceManagementWidget(AppController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();

    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &DeviceManagementWidget::refreshBusStatus);
    m_statusTimer->start(300);

    connect(m_connectBtn, &QPushButton::clicked, this, &DeviceManagementWidget::onConnectClicked);
    connect(m_controller, &AppController::connectionChanged, this, &DeviceManagementWidget::onConnectionChanged);
    connect(m_controller, &AppController::openFailed, this, &DeviceManagementWidget::onOpenFailed);
    connect(m_controller, &AppController::boardInfoChanged, m_boardInfo, &QLabel::setText);
    connect(m_controller, &AppController::busStatusChanged, m_busStatus, &QLabel::setText);

    updateUiByConnection(false);
    m_boardInfo->setText(QStringLiteral("未连接"));
    m_busStatus->setText(QStringLiteral("--"));
}

// 点击连接按钮：连接或断开设备。
void DeviceManagementWidget::onConnectClicked()
{
    if (m_controller->isConnected()) {
        m_controller->disconnectDevice();
        return;
    }

    const int devType = m_deviceType->currentIndex() == 0 ? USBCAN1 : USBCAN2;
    const int devIndex = m_deviceIndex->value();
    const int canIndex = m_channel->currentIndex();
    const int baudIndex = m_baud->currentIndex();
    const int mode = m_mode->currentIndex();

    m_controller->connectDevice(devType, devIndex, canIndex, baudIndex, mode);
}

// 响应连接状态变化。
void DeviceManagementWidget::onConnectionChanged(bool connected)
{
    updateUiByConnection(connected);
}

// 提示打开设备失败原因。
void DeviceManagementWidget::onOpenFailed(const QString &reason)
{
    QMessageBox::critical(this, QStringLiteral("设备连接失败"), reason);
}

// 周期刷新总线状态文本。
void DeviceManagementWidget::refreshBusStatus()
{
    if (!m_controller->isConnected())
        return;

    CanWorker *worker = m_controller->worker();
    if (!worker)
        return;

    const QString status = worker->busStatusLine();
    m_busStatus->setText(status.isEmpty() ? QStringLiteral("--") : status);
}

// 构建设备管理界面。
void DeviceManagementWidget::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    auto *cfgBox = new QGroupBox(QStringLiteral("设备连接参数"), this);
    auto *cfgForm = new QFormLayout(cfgBox);

    m_deviceType = new QComboBox(cfgBox);
    m_deviceType->addItems(QStringList() << QStringLiteral("USBCAN-I") << QStringLiteral("USBCAN-II"));
    cfgForm->addRow(QStringLiteral("设备类型"), m_deviceType);

    m_deviceIndex = new QSpinBox(cfgBox);
    m_deviceIndex->setRange(0, 7);
    cfgForm->addRow(QStringLiteral("设备索引"), m_deviceIndex);

    m_channel = new QComboBox(cfgBox);
    m_channel->addItems(QStringList() << QStringLiteral("通道 0") << QStringLiteral("通道 1"));
    cfgForm->addRow(QStringLiteral("CAN 通道"), m_channel);

    m_baud = new QComboBox(cfgBox);
    for (int i = 0; i < CanVci::baudLabelCount(); ++i)
        m_baud->addItem(CanVci::baudIndexToLabel(i));
    m_baud->setCurrentIndex(3);
    cfgForm->addRow(QStringLiteral("波特率"), m_baud);

    m_mode = new QComboBox(cfgBox);
    m_mode->addItems(QStringList() << QStringLiteral("正常") << QStringLiteral("只听") << QStringLiteral("自发自收"));
    cfgForm->addRow(QStringLiteral("工作模式"), m_mode);

    auto *btnRow = new QHBoxLayout;
    m_connectBtn = new QPushButton(QStringLiteral("打开设备"), cfgBox);
    btnRow->addWidget(m_connectBtn);
    btnRow->addStretch();
    cfgForm->addRow(btnRow);

    auto *stateBox = new QGroupBox(QStringLiteral("设备状态"), this);
    auto *stateForm = new QFormLayout(stateBox);
    m_boardInfo = new QLabel(stateBox);
    m_boardInfo->setWordWrap(true);
    m_busStatus = new QLabel(stateBox);
    m_busStatus->setWordWrap(true);
    stateForm->addRow(QStringLiteral("板卡信息"), m_boardInfo);
    stateForm->addRow(QStringLiteral("总线状态"), m_busStatus);

    mainLayout->addWidget(cfgBox);
    mainLayout->addWidget(stateBox);
    mainLayout->addStretch();
}

// 根据连接状态更新界面可编辑性。
void DeviceManagementWidget::updateUiByConnection(bool connected)
{
    m_connectBtn->setText(connected ? QStringLiteral("关闭设备") : QStringLiteral("打开设备"));
    m_deviceType->setEnabled(!connected);
    m_deviceIndex->setEnabled(!connected);
    m_channel->setEnabled(!connected);
    m_baud->setEnabled(!connected);
    m_mode->setEnabled(!connected);
}
