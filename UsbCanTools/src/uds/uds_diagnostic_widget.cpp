#include "uds/uds_diagnostic_panel.h"

#include "app/app_controller.h"
#include "can/can_worker_api.h"
#include "protocol/isotp.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

// 文件说明：UDS 诊断页面实现，面向单帧/多帧 UDS 诊断收发调试。

UdsDiagnosticWidget::UdsDiagnosticWidget(AppController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();

    connect(m_sendBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onSendClicked);
    connect(m_clearBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onClearClicked);
    connect(m_controller, &AppController::connectionChanged, this, &UdsDiagnosticWidget::onConnectionChanged);

    onConnectionChanged(false);
}

void UdsDiagnosticWidget::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    auto *cfgBox = new QGroupBox(QStringLiteral("UDS 诊断参数"), this);
    auto *cfgForm = new QFormLayout(cfgBox);

    m_txIdEdit = new QLineEdit(QStringLiteral("511"), cfgBox);
    m_rxIdEdit = new QLineEdit(QStringLiteral("666"), cfgBox);
    m_extCheck = new QCheckBox(QStringLiteral("扩展帧"), cfgBox);

    m_timeoutSpin = new QSpinBox(cfgBox);
    m_timeoutSpin->setRange(100, 120000);
    m_timeoutSpin->setValue(3000);
    m_timeoutSpin->setSuffix(QStringLiteral(" ms"));

    m_reqDataEdit = new QLineEdit(QStringLiteral("10 02"), cfgBox);
    m_reqDataEdit->setPlaceholderText(QStringLiteral("例如: 10 02 / 22 F1 90"));

    cfgForm->addRow(QStringLiteral("请求ID (HEX)"), m_txIdEdit);
    cfgForm->addRow(QStringLiteral("响应ID (HEX)"), m_rxIdEdit);
    cfgForm->addRow(QString(), m_extCheck);
    cfgForm->addRow(QStringLiteral("超时"), m_timeoutSpin);
    cfgForm->addRow(QStringLiteral("请求数据 (HEX)"), m_reqDataEdit);

    auto *btnRow = new QHBoxLayout;
    m_sendBtn = new QPushButton(QStringLiteral("发送诊断请求"), this);
    m_clearBtn = new QPushButton(QStringLiteral("清空日志"), this);
    btnRow->addWidget(m_sendBtn);
    btnRow->addWidget(m_clearBtn);
    btnRow->addStretch();

    auto *logBox = new QGroupBox(QStringLiteral("诊断日志"), this);
    auto *logLayout = new QVBoxLayout(logBox);
    m_logEdit = new QPlainTextEdit(logBox);
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumBlockCount(3000);
    logLayout->addWidget(m_logEdit);

    mainLayout->addWidget(cfgBox);
    mainLayout->addLayout(btnRow);
    mainLayout->addWidget(logBox, 1);
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

void UdsDiagnosticWidget::onSendClicked()
{
    CanWorker *worker = m_controller ? m_controller->worker() : nullptr;
    if (!worker || !worker->isDeviceOpen()) {
        QMessageBox::information(this, QStringLiteral("UDS 诊断"), QStringLiteral("请先连接 CAN 设备。"));
        return;
    }

    quint32 txId = 0;
    quint32 rxId = 0;
    if (!parseHexU32(m_txIdEdit->text(), &txId) || !parseHexU32(m_rxIdEdit->text(), &rxId)) {
        QMessageBox::warning(this, QStringLiteral("UDS 诊断"), QStringLiteral("请求ID/响应ID请输入十六进制。"));
        return;
    }

    const QByteArray request = parseHexBytes(m_reqDataEdit->text());
    if (request.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("UDS 诊断"), QStringLiteral("请求数据格式错误，请输入偶数位十六进制。"));
        return;
    }

    appendLog(QStringLiteral("TX[%1->%2] %3")
                  .arg(QString::number(txId, 16).toUpper(),
                       QString::number(rxId, 16).toUpper(),
                       toHexSpaced(request)));

    QByteArray response;
    const bool ok = IsoTp::sendReceive(worker,
                                       txId,
                                       rxId,
                                       m_extCheck->isChecked(),
                                       request,
                                       &response,
                                       m_timeoutSpin->value());
    if (!ok) {
        appendLog(QStringLiteral("RX 超时或收发失败"));
        return;
    }

    appendLog(QStringLiteral("RX %1").arg(toHexSpaced(response)));
}

void UdsDiagnosticWidget::onClearClicked()
{
    m_logEdit->clear();
}

void UdsDiagnosticWidget::onConnectionChanged(bool connected)
{
    m_sendBtn->setEnabled(connected);
    if (!connected)
        appendLog(QStringLiteral("设备未连接，UDS 诊断发送已禁用。"));
}
