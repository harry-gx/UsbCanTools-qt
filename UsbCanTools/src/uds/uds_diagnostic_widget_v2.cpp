#include "uds/uds_diagnostic_widget.h"

#include "app/app_controller.h"
#include "can/can_worker_api.h"
#include "uds/uds_flash_worker.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {
struct ServiceTemplateEntry
{
    QString text;
    QString name;
    QString type;
    QString reqHex;
    int timeoutMs;
    int retries;
    bool enabled;
    QString expectedSidHex;
};

QList<ServiceTemplateEntry> diagnosticServiceTemplates()
{
    QList<ServiceTemplateEntry> t;
    t << ServiceTemplateEntry{QStringLiteral("10 自动 - 进入编程会话"), QStringLiteral("进入编程会话"), QStringLiteral("AUTO_10"), QString(), 3000, 0, true, QStringLiteral("50")};
    t << ServiceTemplateEntry{QStringLiteral("11 自动 - ECU复位"), QStringLiteral("ECU复位"), QStringLiteral("AUTO_11"), QString(), 3000, 0, false, QStringLiteral("51")};
    t << ServiceTemplateEntry{QStringLiteral("22 - 读DID"), QStringLiteral("读DID"), QStringLiteral("RAW"), QStringLiteral("22F190"), 3000, 0, true, QStringLiteral("62")};
    t << ServiceTemplateEntry{QStringLiteral("3E - TesterPresent"), QStringLiteral("保持在线"), QStringLiteral("RAW"), QStringLiteral("3E00"), 1000, 0, false, QStringLiteral("7E")};
    return t;
}
}

UdsDiagnosticWidget::UdsDiagnosticWidget(AppController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();
    applyStyle();
    loadDefaultFlow();
    connect(m_controller, &AppController::connectionChanged, this, &UdsDiagnosticWidget::onConnectionChanged);
    onConnectionChanged(false);
}

UdsDiagnosticWidget::~UdsDiagnosticWidget()
{
    if (m_workerThread) {
        m_workerThread->requestAbort();
        m_workerThread->wait(3000);
        delete m_workerThread;
        m_workerThread = nullptr;
    }
}

void UdsDiagnosticWidget::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    auto *cfgBox = new QGroupBox(QStringLiteral("UDS 诊断参数"), this);
    auto *cfgForm = new QFormLayout(cfgBox);
    m_txIdEdit = new QLineEdit(QStringLiteral("700"), cfgBox);
    m_rxIdEdit = new QLineEdit(QStringLiteral("701"), cfgBox);
    m_extCheck = new QCheckBox(QStringLiteral("扩展帧"), cfgBox);
    m_timeoutSpin = new QSpinBox(cfgBox);
    m_timeoutSpin->setRange(100, 120000);
    m_timeoutSpin->setValue(3000);
    m_timeoutSpin->setSuffix(QStringLiteral(" ms"));
    m_loopCountSpin = new QSpinBox(cfgBox);
    m_loopCountSpin->setRange(1, 1000);
    m_loopCountSpin->setValue(1);
    cfgForm->addRow(QStringLiteral("请求ID (HEX)"), m_txIdEdit);
    cfgForm->addRow(QStringLiteral("响应ID (HEX)"), m_rxIdEdit);
    cfgForm->addRow(QString(), m_extCheck);
    cfgForm->addRow(QStringLiteral("默认超时"), m_timeoutSpin);
    cfgForm->addRow(QStringLiteral("循环次数"), m_loopCountSpin);

    m_serviceList = new QListWidget(this);
    const QList<ServiceTemplateEntry> templates = diagnosticServiceTemplates();
    for (const ServiceTemplateEntry &e : templates)
        m_serviceList->addItem(e.text);

    auto *serviceBox = new QGroupBox(QStringLiteral("服务模板"), this);
    auto *serviceLayout = new QVBoxLayout(serviceBox);
    serviceLayout->addWidget(m_serviceList);

    m_flowTable = new QTableWidget(this);
    m_flowTable->setColumnCount(8);
    m_flowTable->setHorizontalHeaderLabels(QStringList()
                                           << QStringLiteral("启用")
                                           << QStringLiteral("步骤名")
                                           << QStringLiteral("类型")
                                           << QStringLiteral("请求数据(HEX)")
                                           << QStringLiteral("超时(ms)")
                                           << QStringLiteral("重试")
                                           << QStringLiteral("校验SID")
                                           << QStringLiteral("期望SID"));
    m_flowTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_flowTable->horizontalHeader()->setStretchLastSection(true);
    m_flowTable->verticalHeader()->setVisible(false);
    m_flowTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_flowTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_flowTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

    m_execFlowTable = new QTableWidget(this);
    m_execFlowTable->setColumnCount(8);
    m_execFlowTable->setHorizontalHeaderLabels(m_flowTable->horizontalHeaderItem(0)->text().isEmpty()
                                               ? QStringList()
                                               : QStringList()
                                                 << QStringLiteral("启用")
                                                 << QStringLiteral("步骤名")
                                                 << QStringLiteral("类型")
                                                 << QStringLiteral("请求数据(HEX)")
                                                 << QStringLiteral("超时(ms)")
                                                 << QStringLiteral("重试")
                                                 << QStringLiteral("校验SID")
                                                 << QStringLiteral("期望SID"));
    m_execFlowTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_execFlowTable->horizontalHeader()->setStretchLastSection(true);
    m_execFlowTable->verticalHeader()->setVisible(false);
    m_execFlowTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_execFlowTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_execFlowTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto *addRawBtn = new QPushButton(QStringLiteral("新增RAW"), this);
    auto *delBtn = new QPushButton(QStringLiteral("删除"), this);
    auto *upBtn = new QPushButton(QStringLiteral("上移"), this);
    auto *downBtn = new QPushButton(QStringLiteral("下移"), this);
    auto *defaultBtn = new QPushButton(QStringLiteral("默认流程"), this);
    auto *importBtn = new QPushButton(QStringLiteral("导入"), this);
    auto *exportBtn = new QPushButton(QStringLiteral("导出"), this);
    m_applyToExecutorBtn = new QPushButton(QStringLiteral("应用到执行器"), this);

    connect(addRawBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onFlowAddRaw);
    connect(delBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onFlowDelete);
    connect(upBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onFlowMoveUp);
    connect(downBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onFlowMoveDown);
    connect(defaultBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onFlowLoadDefault);
    connect(importBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onFlowImport);
    connect(exportBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onFlowExport);
    connect(m_applyToExecutorBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onApplyToExecutor);

    connect(m_serviceList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item)
            return;
        const QList<ServiceTemplateEntry> templates = diagnosticServiceTemplates();
        const int row = m_serviceList->row(item);
        if (row < 0 || row >= templates.size())
            return;
        const ServiceTemplateEntry &e = templates.at(row);
        addFlowRow(e.name, e.type, e.reqHex, e.timeoutMs, e.retries, e.enabled, e.expectedSidHex);
        m_execFlowState->setText(QStringLiteral("执行器流程状态：未应用（编辑器已变更）"));
    });

    connect(m_flowTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        m_execFlowState->setText(QStringLiteral("执行器流程状态：未应用（编辑器已变更）"));
    });

    auto *flowBtnRow = new QHBoxLayout;
    flowBtnRow->addWidget(addRawBtn);
    flowBtnRow->addWidget(delBtn);
    flowBtnRow->addWidget(upBtn);
    flowBtnRow->addWidget(downBtn);
    flowBtnRow->addWidget(defaultBtn);
    flowBtnRow->addWidget(importBtn);
    flowBtnRow->addWidget(exportBtn);
    flowBtnRow->addWidget(m_applyToExecutorBtn);
    flowBtnRow->addStretch();

    auto *flowBox = new QGroupBox(QStringLiteral("流程编辑器"), this);
    auto *flowLayout = new QVBoxLayout(flowBox);
    flowLayout->addLayout(flowBtnRow);
    flowLayout->addWidget(m_flowTable, 1);

    auto *editorLayout = new QHBoxLayout;
    editorLayout->addWidget(serviceBox, 2);
    editorLayout->addWidget(flowBox, 8);
    auto *editorPage = new QWidget(this);
    editorPage->setLayout(editorLayout);

    m_execFlowState = new QLabel(QStringLiteral("执行器流程状态：未应用"), this);
    m_startBtn = new QPushButton(QStringLiteral("执行流程"), this);
    m_abortBtn = new QPushButton(QStringLiteral("中止"), this);
    m_abortBtn->setEnabled(false);
    m_clearBtn = new QPushButton(QStringLiteral("清空日志"), this);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_logEdit = new QPlainTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumBlockCount(3000);

    connect(m_startBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onStart);
    connect(m_abortBtn, &QPushButton::clicked, this, &UdsDiagnosticWidget::onAbort);
    connect(m_clearBtn, &QPushButton::clicked, this, [this]() { m_logEdit->clear(); });

    auto *execBtnRow = new QHBoxLayout;
    execBtnRow->addWidget(m_startBtn);
    execBtnRow->addWidget(m_abortBtn);
    execBtnRow->addWidget(m_clearBtn);
    execBtnRow->addStretch();

    auto *execFlowBox = new QGroupBox(QStringLiteral("执行器"), this);
    auto *execFlowLayout = new QVBoxLayout(execFlowBox);
    execFlowLayout->addWidget(m_execFlowState);
    execFlowLayout->addWidget(m_execFlowTable, 1);
    execFlowLayout->addLayout(execBtnRow);

    auto *execPageLayout = new QVBoxLayout;
    execPageLayout->addWidget(execFlowBox, 2);
    execPageLayout->addWidget(m_progressBar);
    execPageLayout->addWidget(m_logEdit, 1);
    auto *execPage = new QWidget(this);
    execPage->setLayout(execPageLayout);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(editorPage, QStringLiteral("编辑器"));
    m_tabs->addTab(execPage, QStringLiteral("执行器"));

    mainLayout->addWidget(cfgBox);
    mainLayout->addWidget(m_tabs, 1);
}

void UdsDiagnosticWidget::setUiBusy(bool busy)
{
    m_startBtn->setEnabled(!busy && m_controller && m_controller->isConnected());
    m_abortBtn->setEnabled(busy);
    m_applyToExecutorBtn->setEnabled(!busy);
    m_txIdEdit->setEnabled(!busy);
    m_rxIdEdit->setEnabled(!busy);
    m_extCheck->setEnabled(!busy);
    m_timeoutSpin->setEnabled(!busy);
    m_loopCountSpin->setEnabled(!busy);
    m_serviceList->setEnabled(!busy);
    m_flowTable->setEnabled(!busy);
    m_execFlowTable->setEnabled(!busy);
}

void UdsDiagnosticWidget::addFlowRow(const QString &name,
                                     const QString &type,
                                     const QString &requestHex,
                                     int timeoutMs,
                                     int retries,
                                     bool enabled,
                                     const QString &expectedSidHex)
{
    const int row = m_flowTable->rowCount();
    m_flowTable->insertRow(row);

    auto *en = new QTableWidgetItem;
    en->setFlags((en->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    en->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    m_flowTable->setItem(row, 0, en);

    m_flowTable->setItem(row, 1, new QTableWidgetItem(name));
    m_flowTable->setItem(row, 2, new QTableWidgetItem(type));
    m_flowTable->setItem(row, 3, new QTableWidgetItem(requestHex.toUpper()));
    m_flowTable->setItem(row, 4, new QTableWidgetItem(QString::number(timeoutMs)));
    m_flowTable->setItem(row, 5, new QTableWidgetItem(QString::number(retries)));

    auto *sidCheck = new QTableWidgetItem;
    sidCheck->setFlags((sidCheck->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    sidCheck->setCheckState(expectedSidHex.isEmpty() ? Qt::Unchecked : Qt::Checked);
    m_flowTable->setItem(row, 6, sidCheck);
    m_flowTable->setItem(row, 7, new QTableWidgetItem(expectedSidHex.toUpper()));
}

void UdsDiagnosticWidget::loadDefaultFlow()
{
    m_flowTable->setRowCount(0);
    addFlowRow(QStringLiteral("进入编程会话"), QStringLiteral("AUTO_10"), QString(), 3000, 0, true, QStringLiteral("50"));
    addFlowRow(QStringLiteral("读DID"), QStringLiteral("RAW"), QStringLiteral("22F190"), 3000, 0, true, QStringLiteral("62"));
    addFlowRow(QStringLiteral("ECU复位"), QStringLiteral("AUTO_11"), QString(), 3000, 0, false, QStringLiteral("51"));
    m_execFlowState->setText(QStringLiteral("执行器流程状态：未应用（编辑器已变更）"));
}

bool UdsDiagnosticWidget::buildFlowFromTable(QTableWidget *table, QList<UdsFlashStep> *steps, QString *err) const
{
    if (!table || !steps)
        return false;

    steps->clear();
    for (int row = 0; row < table->rowCount(); ++row) {
        UdsFlashStep step;
        const QTableWidgetItem *enabledItem = table->item(row, 0);
        step.enabled = enabledItem && enabledItem->checkState() == Qt::Checked;

        step.name = table->item(row, 1) ? table->item(row, 1)->text().trimmed() : QString();
        if (step.name.isEmpty())
            step.name = QStringLiteral("Step%1").arg(row + 1);

        const QString type = table->item(row, 2) ? table->item(row, 2)->text().trimmed().toUpper() : QString();
        const QString reqHex = table->item(row, 3) ? table->item(row, 3)->text().trimmed() : QString();
        const QString timeoutText = table->item(row, 4) ? table->item(row, 4)->text().trimmed() : QString();
        const QString retryText = table->item(row, 5) ? table->item(row, 5)->text().trimmed() : QString();
        const QTableWidgetItem *sidCheckItem = table->item(row, 6);
        const QString sidText = table->item(row, 7) ? table->item(row, 7)->text().trimmed() : QString();

        if (type == QStringLiteral("RAW")) {
            step.type = UdsFlashStep::StepRawRequest;
            step.request = parseHexBytes(reqHex);
            if (step.request.isEmpty()) {
                if (err)
                    *err = QStringLiteral("第 %1 行 RAW 请求数据非法").arg(row + 1);
                return false;
            }
        } else if (type == QStringLiteral("AUTO_10")) {
            step.type = UdsFlashStep::StepSessionControlAuto;
        } else if (type == QStringLiteral("AUTO_11")) {
            step.type = UdsFlashStep::StepEcuResetAuto;
        } else {
            if (err)
                *err = QStringLiteral("第 %1 行类型仅支持 RAW/AUTO_10/AUTO_11").arg(row + 1);
            return false;
        }

        bool ok = false;
        int timeoutVal = timeoutText.toInt(&ok);
        if (!ok || timeoutVal <= 0)
            timeoutVal = m_timeoutSpin->value();
        step.timeoutMs = timeoutVal;

        int retriesVal = retryText.toInt(&ok);
        if (!ok || retriesVal < 0)
            retriesVal = 0;
        step.retries = retriesVal;

        step.checkPositiveSid = sidCheckItem && sidCheckItem->checkState() == Qt::Checked;
        if (step.checkPositiveSid) {
            quint32 sidVal = 0;
            if (!parseHexU32(sidText, &sidVal) || sidVal > 0xFFu) {
                if (err)
                    *err = QStringLiteral("第 %1 行期望SID非法").arg(row + 1);
                return false;
            }
            step.expectedPositiveSid = quint8(sidVal);
        }

        steps->append(step);
    }
    return true;
}

void UdsDiagnosticWidget::syncEditorToExecutor()
{
    m_execFlowTable->setRowCount(0);
    for (int row = 0; row < m_flowTable->rowCount(); ++row) {
        const int dstRow = m_execFlowTable->rowCount();
        m_execFlowTable->insertRow(dstRow);
        for (int c = 0; c < m_flowTable->columnCount(); ++c) {
            QTableWidgetItem *src = m_flowTable->item(row, c);
            QTableWidgetItem *dst = src ? src->clone() : new QTableWidgetItem;
            dst->setFlags(dst->flags() & ~Qt::ItemIsEditable);
            if (c == 0 || c == 6)
                dst->setFlags((dst->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
            m_execFlowTable->setItem(dstRow, c, dst);
        }
    }
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

void UdsDiagnosticWidget::onStart()
{
    CanWorker *canWorker = m_controller ? m_controller->worker() : nullptr;
    if (!canWorker || !canWorker->isDeviceOpen()) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("请先连接 CAN 设备"));
        return;
    }

    if (m_execFlowTable->rowCount() <= 0) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("执行器中没有流程，请先应用编辑器流程"));
        return;
    }

    quint32 txId = 0;
    quint32 rxId = 0;
    if (!parseHexU32(m_txIdEdit->text(), &txId) || !parseHexU32(m_rxIdEdit->text(), &rxId)) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("请求ID/响应ID请输入十六进制"));
        return;
    }

    QList<UdsFlashStep> baseSteps;
    QString flowErr;
    if (!buildFlowFromTable(m_execFlowTable, &baseSteps, &flowErr)) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), flowErr);
        return;
    }

    QList<UdsFlashStep> execSteps;
    const int loops = m_loopCountSpin->value();
    for (int i = 0; i < loops; ++i)
        execSteps.append(baseSteps);

    if (m_workerThread) {
        m_workerThread->requestAbort();
        m_workerThread->wait(3000);
        delete m_workerThread;
        m_workerThread = nullptr;
    }

    m_workerThread = new UdsFlashWorker(this);
    connect(m_workerThread, &UdsFlashWorker::logLine, this, &UdsDiagnosticWidget::onFlashLog, Qt::QueuedConnection);
    connect(m_workerThread, &UdsFlashWorker::progressValue, this, &UdsDiagnosticWidget::onFlashProgress, Qt::QueuedConnection);
    connect(m_workerThread, &UdsFlashWorker::finishedOk, this, &UdsDiagnosticWidget::onFlashOk, Qt::QueuedConnection);
    connect(m_workerThread, &UdsFlashWorker::finishedError, this, &UdsDiagnosticWidget::onFlashErr, Qt::QueuedConnection);
    connect(m_workerThread, &QThread::finished, this, &UdsDiagnosticWidget::onThreadFinished, Qt::QueuedConnection);

    m_workerThread->configure(canWorker,
                              txId,
                              rxId,
                              m_extCheck->isChecked(),
                              QString(),
                              0u,
                              512,
                              m_timeoutSpin->value(),
                              false,
                              execSteps,
                              false);

    m_progressBar->setValue(0);
    appendLog(QStringLiteral("开始执行诊断流程，循环次数=%1").arg(loops));
    setUiBusy(true);
    m_tabs->setCurrentIndex(1);
    m_workerThread->start();
}

void UdsDiagnosticWidget::onAbort()
{
    if (m_workerThread)
        m_workerThread->requestAbort();
}

void UdsDiagnosticWidget::onFlowAddRaw()
{
    addFlowRow(QStringLiteral("自定义RAW"), QStringLiteral("RAW"), QString(), m_timeoutSpin->value(), 0, true, QString());
    m_execFlowState->setText(QStringLiteral("执行器流程状态：未应用（编辑器已变更）"));
}

void UdsDiagnosticWidget::onFlowDelete()
{
    const int row = m_flowTable->currentRow();
    if (row < 0)
        return;
    m_flowTable->removeRow(row);
    m_execFlowState->setText(QStringLiteral("执行器流程状态：未应用（编辑器已变更）"));
}

void UdsDiagnosticWidget::onFlowMoveUp()
{
    const int row = m_flowTable->currentRow();
    if (row <= 0)
        return;
    m_flowTable->insertRow(row - 1);
    for (int c = 0; c < m_flowTable->columnCount(); ++c)
        m_flowTable->setItem(row - 1, c, m_flowTable->takeItem(row + 1, c));
    m_flowTable->removeRow(row + 1);
    m_flowTable->selectRow(row - 1);
    m_execFlowState->setText(QStringLiteral("执行器流程状态：未应用（编辑器已变更）"));
}

void UdsDiagnosticWidget::onFlowMoveDown()
{
    const int row = m_flowTable->currentRow();
    if (row < 0 || row >= m_flowTable->rowCount() - 1)
        return;
    m_flowTable->insertRow(row + 2);
    for (int c = 0; c < m_flowTable->columnCount(); ++c)
        m_flowTable->setItem(row + 2, c, m_flowTable->takeItem(row, c));
    m_flowTable->removeRow(row);
    m_flowTable->selectRow(row + 1);
    m_execFlowState->setText(QStringLiteral("执行器流程状态：未应用（编辑器已变更）"));
}

void UdsDiagnosticWidget::onFlowLoadDefault()
{
    loadDefaultFlow();
}

void UdsDiagnosticWidget::onFlowImport()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入流程"), QString(), QStringLiteral("UDS Flow (*.json)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("导入失败：无法读取文件"));
        return;
    }

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    f.close();
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("导入失败：JSON 格式错误"));
        return;
    }

    const QJsonArray arr = doc.object().value(QStringLiteral("steps")).toArray();
    if (arr.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("导入失败：steps 为空"));
        return;
    }

    m_flowTable->setRowCount(0);
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject o = arr.at(i).toObject();
        addFlowRow(o.value(QStringLiteral("name")).toString(),
                   o.value(QStringLiteral("type")).toString().toUpper(),
                   o.value(QStringLiteral("request")).toString().toUpper(),
                   o.value(QStringLiteral("timeoutMs")).toInt(3000),
                   o.value(QStringLiteral("retries")).toInt(0),
                   o.value(QStringLiteral("enabled")).toBool(true),
                   o.value(QStringLiteral("checkSid")).toBool(false) ? o.value(QStringLiteral("expectedSid")).toString().toUpper() : QString());
    }
    appendLog(QStringLiteral("流程已导入：%1").arg(path));
    m_execFlowState->setText(QStringLiteral("执行器流程状态：未应用（编辑器已变更）"));
}

void UdsDiagnosticWidget::onFlowExport()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出流程"), QStringLiteral("uds_diag_flow.json"), QStringLiteral("UDS Flow (*.json)"));
    if (path.isEmpty())
        return;

    QJsonArray arr;
    for (int row = 0; row < m_flowTable->rowCount(); ++row) {
        QJsonObject o;
        const QTableWidgetItem *enabledItem = m_flowTable->item(row, 0);
        const QTableWidgetItem *sidCheckItem = m_flowTable->item(row, 6);
        o.insert(QStringLiteral("enabled"), enabledItem && enabledItem->checkState() == Qt::Checked);
        o.insert(QStringLiteral("name"), m_flowTable->item(row, 1) ? m_flowTable->item(row, 1)->text().trimmed() : QString());
        o.insert(QStringLiteral("type"), m_flowTable->item(row, 2) ? m_flowTable->item(row, 2)->text().trimmed().toUpper() : QString());
        o.insert(QStringLiteral("request"), m_flowTable->item(row, 3) ? m_flowTable->item(row, 3)->text().trimmed().toUpper() : QString());
        o.insert(QStringLiteral("timeoutMs"), m_flowTable->item(row, 4) ? m_flowTable->item(row, 4)->text().trimmed().toInt() : 3000);
        o.insert(QStringLiteral("retries"), m_flowTable->item(row, 5) ? m_flowTable->item(row, 5)->text().trimmed().toInt() : 0);
        o.insert(QStringLiteral("checkSid"), sidCheckItem && sidCheckItem->checkState() == Qt::Checked);
        o.insert(QStringLiteral("expectedSid"), m_flowTable->item(row, 7) ? m_flowTable->item(row, 7)->text().trimmed().toUpper() : QString());
        arr.append(o);
    }

    QJsonObject root;
    root.insert(QStringLiteral("steps"), arr);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("导出失败：无法写入文件"));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    appendLog(QStringLiteral("流程已导出：%1").arg(path));
}

void UdsDiagnosticWidget::onApplyToExecutor()
{
    QString flowErr;
    QList<UdsFlashStep> temp;
    if (!buildFlowFromTable(m_flowTable, &temp, &flowErr)) {
        QMessageBox::warning(this, QStringLiteral("UDS诊断"), QStringLiteral("编辑器流程有误：%1").arg(flowErr));
        return;
    }
    syncEditorToExecutor();
    m_execFlowState->setText(QStringLiteral("执行器流程状态：已应用（%1 步）").arg(m_execFlowTable->rowCount()));
    m_tabs->setCurrentIndex(1);
}

void UdsDiagnosticWidget::onFlashLog(const QString &line)
{
    appendLog(line);
}

void UdsDiagnosticWidget::onFlashProgress(int pct)
{
    m_progressBar->setValue(qBound(0, pct, 100));
}

void UdsDiagnosticWidget::onFlashOk(const QString &summary)
{
    appendLog(summary);
    QMessageBox::information(this, QStringLiteral("UDS诊断"), summary);
}

void UdsDiagnosticWidget::onFlashErr(const QString &reason)
{
    appendLog(QStringLiteral("错误：%1").arg(reason));
    QMessageBox::warning(this, QStringLiteral("UDS诊断"), reason);
}

void UdsDiagnosticWidget::onThreadFinished()
{
    setUiBusy(false);
}

void UdsDiagnosticWidget::onConnectionChanged(bool connected)
{
    if (!m_workerThread || !m_workerThread->isRunning())
        setUiBusy(false);
    m_startBtn->setEnabled(connected && (!m_workerThread || !m_workerThread->isRunning()));
}

void UdsDiagnosticWidget::applyStyle()
{
    setStyleSheet(QStringLiteral(
        "QWidget { background:#CED6E0; }"
        "QGroupBox { border:1px solid #8EA6C1; border-radius:4px; margin-top:10px; background:#EAF0F6; font-weight:bold; }"
        "QGroupBox::title { subcontrol-origin:margin; left:8px; padding:0 4px; color:#27486B; }"
        "QLineEdit,QSpinBox,QListWidget,QTableWidget,QPlainTextEdit { background:#FFFFFF; border:1px solid #9DB2C9; selection-background-color:#6E95BF; }"
        "QHeaderView::section { background:#D9E4F1; border:1px solid #9DB2C9; padding:4px; }"
        "QPushButton { background:#E7EEF7; border:1px solid #89A3C0; border-radius:3px; padding:4px 10px; }"
        "QPushButton:hover { background:#D6E6F8; }"
        "QPushButton:pressed { background:#C8DBF1; }"
        "QProgressBar { border:1px solid #8EA6C1; background:#F5F8FB; text-align:center; }"
        "QProgressBar::chunk { background:#5E89B9; }"));
}
