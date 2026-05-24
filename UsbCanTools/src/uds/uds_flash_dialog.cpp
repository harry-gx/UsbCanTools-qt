#include "uds_flash_dialog.h"

#include "can/can_worker_api.h"
#include "uds_flash_worker.h"

// 说明：本文件为 UDS 刷写工作台界面实现，负责流程编辑与执行入口。

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QColor>
#include <QFileDialog>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QFileInfo>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

#include <QtGlobal>

namespace {

enum DelayRowDataRole {
    RoleDelayControl = Qt::UserRole + 100,
};

// 解析 16 进制文本为 32 位整数。
bool parseHex32(const QString &s, quint32 *out)
{
    bool ok = false;
    const quint32 v = s.trimmed().toUInt(&ok, 16);
    if (ok && out)
        *out = v;
    return ok;
}

// 解析 16 进制字节串（允许空格与制表符）。
QByteArray parseHexBytes(const QString &s)
{
    QString t = s;
    t.remove(QLatin1Char(' '));
    t.remove(QLatin1Char('\t'));
    if (t.size() % 2 != 0)
        return {};

    const QByteArray out = QByteArray::fromHex(t.toLatin1());
    if (out.isEmpty() && !t.isEmpty())
        return {};
    return out;
}

// 将字节流格式化为大写且以空格分隔的 HEX 文本（例如 "10 02"）。
QString formatHexWithSpaces(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return QString();
    return QString::fromLatin1(bytes.toHex(' ')).toUpper();
}

// 规范化 HEX 字符串显示格式：非法文本保持原值，合法文本统一转为空格分隔大写。
QString normalizeHexDisplay(const QString &rawText)
{
    const QByteArray bytes = parseHexBytes(rawText);
    if (bytes.isEmpty())
        return rawText.trimmed().toUpper();
    return formatHexWithSpaces(bytes);
}

QString delayDisplayText()
{
    return QStringLiteral("等待，时长请填写在“超时(ms)”列");
}

struct DelayOption
{
    QString label;
    int timeoutMs;
    bool custom;
};

QList<DelayOption> delayOptions()
{
    QList<DelayOption> options;
    options << DelayOption{QStringLiteral("100ms"), 100, false}
            << DelayOption{QStringLiteral("200ms"), 200, false}
            << DelayOption{QStringLiteral("300ms"), 300, false}
            << DelayOption{QStringLiteral("400ms"), 400, false}
            << DelayOption{QStringLiteral("500ms"), 500, false}
            << DelayOption{QStringLiteral("600ms"), 600, false}
            << DelayOption{QStringLiteral("700ms"), 700, false}
            << DelayOption{QStringLiteral("800ms"), 800, false}
            << DelayOption{QStringLiteral("900ms"), 900, false}
            << DelayOption{QStringLiteral("1s"), 1000, false}
            << DelayOption{QStringLiteral("2s"), 2000, false}
            << DelayOption{QStringLiteral("3s"), 3000, false}
            << DelayOption{QStringLiteral("4s"), 4000, false}
            << DelayOption{QStringLiteral("5s"), 5000, false}
            << DelayOption{QStringLiteral("自定义"), -1, true};
    return options;
}

QString delaySummaryText(bool enabled, int timeoutMs)
{
    if (!enabled)
        return QStringLiteral("未添加延时");
    const QList<DelayOption> options = delayOptions();
    for (const DelayOption &option : options) {
        if (!option.custom && option.timeoutMs == timeoutMs)
            return QStringLiteral("延时%1").arg(option.label);
    }
    return QStringLiteral("自定义延时 %1ms").arg(timeoutMs);
}

struct ServiceTemplateEntry
{
    // 模板分组名。
    QString group;
    // 模板键值（唯一标识）。
    QString key;
    // 列表展示文本。
    QString text;
    // 流程步骤名称。
    QString name;
    // 步骤类型（RAW / AUTO_*）。
    QString type;
    // 默认请求数据。
    QString reqHex;
    // 默认超时。
    int timeoutMs = 3000;
    // 默认重试次数。
    int retries = 0;
    // 默认启用状态。
    bool enabled = true;
    // 默认期望正响应 SID。
    QString expectedSidHex;

    ServiceTemplateEntry() {}
    ServiceTemplateEntry(const QString &groupIn,
                         const QString &keyIn,
                         const QString &textIn,
                         const QString &nameIn,
                         const QString &typeIn,
                         const QString &reqHexIn,
                         int timeoutMsIn,
                         int retriesIn,
                         bool enabledIn,
                         const QString &expectedSidHexIn)
        : group(groupIn)
        , key(keyIn)
        , text(textIn)
        , name(nameIn)
        , type(typeIn)
        , reqHex(reqHexIn)
        , timeoutMs(timeoutMsIn)
        , retries(retriesIn)
        , enabled(enabledIn)
        , expectedSidHex(expectedSidHexIn)
    {
    }
};

QString serviceGroupTitleBySid(const QString &sidHex)
{
    static const QMap<QString, QString> kTitles = {
        {QStringLiteral("DELAY"), QStringLiteral("(延时) 流程控制")},
        {QStringLiteral("10"), QStringLiteral("(10) 诊断会话控制")},
        {QStringLiteral("11"), QStringLiteral("(11) ECU重置")},
        {QStringLiteral("14"), QStringLiteral("(14) 清除诊断信息")},
        {QStringLiteral("19"), QStringLiteral("(19) 读取DTC信息")},
        {QStringLiteral("22"), QStringLiteral("(22) 读取数据标识符")},
        {QStringLiteral("23"), QStringLiteral("(23) 按地址读内存")},
        {QStringLiteral("24"), QStringLiteral("(24) 读标识符缩放数据")},
        {QStringLiteral("27"), QStringLiteral("(27) 安全访问")},
        {QStringLiteral("28"), QStringLiteral("(28) 通信控制")},
        {QStringLiteral("2A"), QStringLiteral("(2A) 周期读数据标识符")},
        {QStringLiteral("2C"), QStringLiteral("(2C) 动态定义数据标识符")},
        {QStringLiteral("2E"), QStringLiteral("(2E) 写数据标识符")},
        {QStringLiteral("2F"), QStringLiteral("(2F) 输入输出控制")},
        {QStringLiteral("31"), QStringLiteral("(31) 例程控制")},
        {QStringLiteral("34"), QStringLiteral("(34) 请求下载")},
        {QStringLiteral("35"), QStringLiteral("(35) 请求上传")},
        {QStringLiteral("36"), QStringLiteral("(36) 传输数据")},
        {QStringLiteral("37"), QStringLiteral("(37) 传输退出")},
        {QStringLiteral("3D"), QStringLiteral("(3D) 按地址写内存")},
        {QStringLiteral("3E"), QStringLiteral("(3E) TesterPresent")},
        {QStringLiteral("83"), QStringLiteral("(83) 访问时序参数")},
        {QStringLiteral("84"), QStringLiteral("(84) 安全数据传输")},
        {QStringLiteral("85"), QStringLiteral("(85) 控制DTC设置")},
        {QStringLiteral("86"), QStringLiteral("(86) 事件触发响应")},
        {QStringLiteral("87"), QStringLiteral("(87) 链路控制")}
    };
    return kTitles.value(sidHex, QStringLiteral("(%1) 其他服务").arg(sidHex));
}

QString sidFromTemplate(const ServiceTemplateEntry &e)
{
    const QString keyPrefix = e.key.section(QLatin1Char('_'), 0, 0).toUpper();
    if (!keyPrefix.isEmpty())
        return keyPrefix;
    return e.text.left(2).toUpper();
}

QString childDisplayText(const ServiceTemplateEntry &e, const QString &sid)
{
    QString text = e.text.trimmed();
    if (text.startsWith(sid + QLatin1Char(' ')))
        text = text.mid(sid.size() + 1).trimmed();
    return text;
}

QList<ServiceTemplateEntry> commonServiceTemplates()
{
    QList<ServiceTemplateEntry> t;
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("10_01"), QStringLiteral("10 01 - 默认会话"), QStringLiteral("默认会话"), QStringLiteral("RAW"), QStringLiteral("1001"), 3000, 0, false, QStringLiteral("50")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("10_02"), QStringLiteral("10 02 - 编程会话"), QStringLiteral("编程会话"), QStringLiteral("RAW"), QStringLiteral("1002"), 3000, 0, true, QStringLiteral("50")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("10_03"), QStringLiteral("10 03 - 扩展诊断会话"), QStringLiteral("扩展诊断会话"), QStringLiteral("RAW"), QStringLiteral("1003"), 3000, 0, false, QStringLiteral("50")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("10_04"), QStringLiteral("10 04 - 安全会话"), QStringLiteral("安全会话"), QStringLiteral("RAW"), QStringLiteral("1004"), 3000, 0, false, QStringLiteral("50")};

    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("11_01"), QStringLiteral("11 01 - HardReset"), QStringLiteral("HardReset"), QStringLiteral("RAW"), QStringLiteral("1101"), 3000, 0, false, QStringLiteral("51")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("11_02"), QStringLiteral("11 02 - KeyOffOnReset"), QStringLiteral("KeyOffOnReset"), QStringLiteral("RAW"), QStringLiteral("1102"), 3000, 0, false, QStringLiteral("51")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("11_03"), QStringLiteral("11 03 - SoftReset"), QStringLiteral("SoftReset"), QStringLiteral("RAW"), QStringLiteral("1103"), 3000, 0, false, QStringLiteral("51")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("11_04"), QStringLiteral("11 04 - EnableRapidPowerShutDown"), QStringLiteral("启用快速断电"), QStringLiteral("RAW"), QStringLiteral("1104"), 3000, 0, false, QStringLiteral("51")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("11_05"), QStringLiteral("11 05 - DisableRapidPowerShutDown"), QStringLiteral("禁用快速断电"), QStringLiteral("RAW"), QStringLiteral("1105"), 3000, 0, false, QStringLiteral("51")};

    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("27_01"), QStringLiteral("27 01 - 请求Seed1"), QStringLiteral("请求Seed1"), QStringLiteral("RAW"), QStringLiteral("2701"), 3000, 1, false, QStringLiteral("67")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("27_02"), QStringLiteral("27 02 - 发送Key1"), QStringLiteral("发送Key1"), QStringLiteral("RAW"), QStringLiteral("270200000000"), 3000, 1, false, QStringLiteral("67")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("27_03"), QStringLiteral("27 03 - 请求Seed2"), QStringLiteral("请求Seed2"), QStringLiteral("RAW"), QStringLiteral("2703"), 3000, 1, false, QStringLiteral("67")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("27_04"), QStringLiteral("27 04 - 发送Key2"), QStringLiteral("发送Key2"), QStringLiteral("RAW"), QStringLiteral("270400000000"), 3000, 1, false, QStringLiteral("67")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("27_05"), QStringLiteral("27 05 - 请求Seed3"), QStringLiteral("请求Seed3"), QStringLiteral("RAW"), QStringLiteral("2705"), 3000, 1, false, QStringLiteral("67")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("27_06"), QStringLiteral("27 06 - 发送Key3"), QStringLiteral("发送Key3"), QStringLiteral("RAW"), QStringLiteral("270600000000"), 3000, 1, false, QStringLiteral("67")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("27_07"), QStringLiteral("27 07 - 请求Seed4"), QStringLiteral("请求Seed4"), QStringLiteral("RAW"), QStringLiteral("2707"), 3000, 1, false, QStringLiteral("67")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("27_08"), QStringLiteral("27 08 - 发送Key4"), QStringLiteral("发送Key4"), QStringLiteral("RAW"), QStringLiteral("270800000000"), 3000, 1, false, QStringLiteral("67")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("27_09"), QStringLiteral("27 09 - 请求Seed5"), QStringLiteral("请求Seed5"), QStringLiteral("RAW"), QStringLiteral("2709"), 3000, 1, false, QStringLiteral("67")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("27_0A"), QStringLiteral("27 0A - 发送Key5"), QStringLiteral("发送Key5"), QStringLiteral("RAW"), QStringLiteral("270A00000000"), 3000, 1, false, QStringLiteral("67")};

    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("28_00"), QStringLiteral("28 00 00 - 启用Rx和Tx"), QStringLiteral("启用Rx和Tx"), QStringLiteral("RAW"), QStringLiteral("280000"), 3000, 0, false, QStringLiteral("68")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("28_01"), QStringLiteral("28 01 00 - 启用Rx禁用Tx"), QStringLiteral("启用Rx禁用Tx"), QStringLiteral("RAW"), QStringLiteral("280100"), 3000, 0, false, QStringLiteral("68")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("28_02"), QStringLiteral("28 02 00 - 禁用Rx启用Tx"), QStringLiteral("禁用Rx启用Tx"), QStringLiteral("RAW"), QStringLiteral("280200"), 3000, 0, false, QStringLiteral("68")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("28_03"), QStringLiteral("28 03 00 - 禁用Rx和Tx"), QStringLiteral("禁用Rx和Tx"), QStringLiteral("RAW"), QStringLiteral("280300"), 3000, 0, false, QStringLiteral("68")};

    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("85_01"), QStringLiteral("85 01 - 开启DTC"), QStringLiteral("开启DTC"), QStringLiteral("RAW"), QStringLiteral("8501"), 3000, 0, false, QStringLiteral("C5")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("85_02"), QStringLiteral("85 02 - 关闭DTC"), QStringLiteral("关闭DTC"), QStringLiteral("RAW"), QStringLiteral("8502"), 3000, 0, false, QStringLiteral("C5")};

    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("31_FF00_01"), QStringLiteral("31 01 FF00 - 启动擦除Flash"), QStringLiteral("启动擦除Flash"), QStringLiteral("AUTO_ERASE"), QStringLiteral("自动附加下载地址和固件长度"), 5000, 0, false, QStringLiteral("71")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("31_FF00_02"), QStringLiteral("31 02 FF00 - 停止擦除Flash"), QStringLiteral("停止擦除Flash"), QStringLiteral("RAW"), QStringLiteral("3102FF00"), 5000, 0, false, QStringLiteral("71")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("31_FF00_03"), QStringLiteral("31 03 FF00 - 查询擦除结果"), QStringLiteral("查询擦除结果"), QStringLiteral("RAW"), QStringLiteral("3103FF00"), 5000, 0, false, QStringLiteral("71")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("34_AUTO"), QStringLiteral("34 - RequestDownload"), QStringLiteral("请求下载"), QStringLiteral("AUTO_34"), QString(), 5000, 1, false, QStringLiteral("74")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("36_AUTO"), QStringLiteral("36 - TransferData"), QStringLiteral("传输数据"), QStringLiteral("AUTO_36"), QString(), 5000, 0, false, QStringLiteral("76")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("37_AUTO"), QStringLiteral("37 - RequestTransferExit"), QStringLiteral("传输退出"), QStringLiteral("AUTO_37"), QString(), 15000, 1, false, QStringLiteral("77")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("31_FF01_01"), QStringLiteral("31 01 FF01 - 启动CRC校验"), QStringLiteral("启动CRC校验"), QStringLiteral("AUTO_CRC"), QStringLiteral("自动附加固件长度和CRC32"), 5000, 0, false, QStringLiteral("71")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("31_FF01_02"), QStringLiteral("31 02 FF01 - 停止CRC校验"), QStringLiteral("停止CRC校验"), QStringLiteral("RAW"), QStringLiteral("3102FF01"), 5000, 0, false, QStringLiteral("71")};
    t << ServiceTemplateEntry{QStringLiteral("刷写升级"), QStringLiteral("31_FF01_03"), QStringLiteral("31 03 FF01 - 查询CRC结果"), QStringLiteral("查询CRC结果"), QStringLiteral("RAW"), QStringLiteral("3103FF01"), 5000, 0, false, QStringLiteral("71")};
    return t;
}

} // namespace

UdsFlashDialog::UdsFlashDialog(CanWorker *worker, QWidget *parent)
    : QDialog(parent)
    , m_worker(worker)
{
    setWindowTitle(QStringLiteral("UDS ECU 刷写工作台"));
    resize(1100, 760);

    m_txId = new QLineEdit(QStringLiteral("511"));
    m_rxId = new QLineEdit(QStringLiteral("666"));
    m_ext = new QCheckBox(QStringLiteral("扩展帧"));
    m_addr = new QLineEdit(QStringLiteral("00014000"));

    m_payload = new QSpinBox;
    m_payload->setRange(1, 4090);
    m_payload->setValue(512);

    m_timeout = new QSpinBox;
    m_timeout->setRange(500, 120000);
    m_timeout->setValue(5000);
    m_timeout->setSuffix(QStringLiteral(" ms"));

    m_testerPresent = new QCheckBox(QStringLiteral("刷写传输时周期发送 TesterPresent(3E 80)"));
    m_testerPresent->setChecked(true);

    m_fileLabel = new QLabel(QStringLiteral("未选择固件"));
    m_fileLabel->setWordWrap(true);
    m_browse = new QPushButton(QStringLiteral("打开固件..."));
    m_start = new QPushButton(QStringLiteral("执行流程"));
    m_abort = new QPushButton(QStringLiteral("中止"));
    m_abort->setEnabled(false);

    auto *topForm = new QFormLayout;
    topForm->addRow(QStringLiteral("请求ID (HEX)"), m_txId);
    topForm->addRow(QStringLiteral("响应ID (HEX)"), m_rxId);
    topForm->addRow(QString(), m_ext);
    topForm->addRow(QStringLiteral("下载起始地址 (HEX)"), m_addr);
    topForm->addRow(QStringLiteral("每块最大数据字节"), m_payload);
    topForm->addRow(QStringLiteral("默认超时"), m_timeout);
    topForm->addRow(QString(), m_testerPresent);

    auto *topBox = new QGroupBox(QStringLiteral("连接与刷写参数"));
    topBox->setLayout(topForm);

    m_applyToExecutor = new QPushButton(QStringLiteral("应用到执行器"));
    m_execFlowState = new QLabel(QStringLiteral("执行器流程状态: 未应用"));

    m_serviceList = new QTreeWidget;
    m_serviceList->setHeaderHidden(true);
    m_serviceList->setRootIsDecorated(true);
    m_serviceList->setIndentation(16);
    m_serviceList->setUniformRowHeights(true);
    const QList<ServiceTemplateEntry> templates = commonServiceTemplates();
    QMap<QString, QTreeWidgetItem*> groupNodes;
    for (const ServiceTemplateEntry &e : templates) {
        const QString sid = sidFromTemplate(e);
        QTreeWidgetItem *groupItem = groupNodes.value(sid, nullptr);
        if (!groupItem) {
            groupItem = new QTreeWidgetItem(m_serviceList, QStringList(serviceGroupTitleBySid(sid)));
            QFont hfont = groupItem->font(0);
            hfont.setBold(true);
            groupItem->setFont(0, hfont);
            groupItem->setForeground(0, QColor(QStringLiteral("#27486B")));
            groupItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            groupNodes.insert(sid, groupItem);
        }
        auto *child = new QTreeWidgetItem(groupItem, QStringList(childDisplayText(e, sid)));
        child->setData(0, Qt::UserRole, e.key);
    }
    m_serviceList->expandAll();

    auto *serviceBox = new QGroupBox(QStringLiteral("UDS服务模板"));
    auto *serviceLayout = new QVBoxLayout(serviceBox);
    serviceLayout->addWidget(m_serviceList);

    m_flowTable = new QTableWidget;
    m_flowTable->setColumnCount(8);
    m_flowTable->setHorizontalHeaderLabels(QStringList()
                                           << QStringLiteral("启用")
                                           << QStringLiteral("步骤名")
                                           << QStringLiteral("类型")
                                           << QStringLiteral("请求数据(HEX)")
                                           << QStringLiteral("超时(ms)")
                                           << QStringLiteral("重试")
                                           << QStringLiteral("校验SID?")
                                           << QStringLiteral("期望SID"));
    m_flowTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_flowTable->horizontalHeader()->setStretchLastSection(true);
    m_flowTable->verticalHeader()->setVisible(false);
    m_flowTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_flowTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_flowTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_flowTable->setAlternatingRowColors(true);
    m_flowTable->setColumnWidth(0, 55);
    m_flowTable->setColumnWidth(1, 170);
    m_flowTable->setColumnWidth(2, 110);
    m_flowTable->setColumnWidth(3, 300);
    m_flowTable->setColumnWidth(4, 85);
    m_flowTable->setColumnWidth(5, 55);
    m_flowTable->setColumnWidth(6, 70);
    m_flowTable->setColumnWidth(7, 70);

    m_execFlowTable = new QTableWidget;
    m_execFlowTable->setColumnCount(8);
    m_execFlowTable->setHorizontalHeaderLabels(QStringList()
                                               << QStringLiteral("启用")
                                               << QStringLiteral("步骤名")
                                               << QStringLiteral("类型")
                                               << QStringLiteral("请求数据(HEX)")
                                               << QStringLiteral("超时(ms)")
                                               << QStringLiteral("重试")
                                               << QStringLiteral("校验SID?")
                                               << QStringLiteral("期望SID"));
    m_execFlowTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_execFlowTable->horizontalHeader()->setStretchLastSection(true);
    m_execFlowTable->verticalHeader()->setVisible(false);
    m_execFlowTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_execFlowTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_execFlowTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_execFlowTable->setAlternatingRowColors(true);
    m_execFlowTable->setColumnWidth(0, 55);
    m_execFlowTable->setColumnWidth(1, 170);
    m_execFlowTable->setColumnWidth(2, 110);
    m_execFlowTable->setColumnWidth(3, 300);
    m_execFlowTable->setColumnWidth(4, 85);
    m_execFlowTable->setColumnWidth(5, 55);
    m_execFlowTable->setColumnWidth(6, 70);
    m_execFlowTable->setColumnWidth(7, 70);

    auto *flowBtnAddRaw = new QPushButton(QStringLiteral("新增RAW"));
    auto *flowBtnDel = new QPushButton(QStringLiteral("删除"));
    auto *flowBtnUp = new QPushButton(QStringLiteral("上移"));
    auto *flowBtnDown = new QPushButton(QStringLiteral("下移"));
    auto *flowBtnDefault = new QPushButton(QStringLiteral("清空流程"));
    auto *flowBtnImport = new QPushButton(QStringLiteral("导入流程"));
    auto *flowBtnExport = new QPushButton(QStringLiteral("导出流程"));

    connect(flowBtnAddRaw, &QPushButton::clicked, this, &UdsFlashDialog::onFlowAddRaw);
    connect(flowBtnDel, &QPushButton::clicked, this, &UdsFlashDialog::onFlowDelete);
    connect(flowBtnUp, &QPushButton::clicked, this, &UdsFlashDialog::onFlowMoveUp);
    connect(flowBtnDown, &QPushButton::clicked, this, &UdsFlashDialog::onFlowMoveDown);
    connect(flowBtnDefault, &QPushButton::clicked, this, &UdsFlashDialog::onFlowLoadDefault);
    connect(flowBtnImport, &QPushButton::clicked, this, &UdsFlashDialog::onFlowImport);
    connect(flowBtnExport, &QPushButton::clicked, this, &UdsFlashDialog::onFlowExport);
    connect(m_applyToExecutor, &QPushButton::clicked, this, &UdsFlashDialog::onApplyToExecutor);
    connect(m_flowTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item && item->column() == 2) {
            const QString fixedType = item->text().trimmed().toUpper();
            if (fixedType != item->text()) {
                m_flowTable->blockSignals(true);
                item->setText(fixedType);
                m_flowTable->blockSignals(false);
            }
        }
        if (item && item->column() == 3) {
            const QString rowType = m_flowTable->item(item->row(), 2) ? m_flowTable->item(item->row(), 2)->text().trimmed().toUpper() : QString();
            const QString normalized = (rowType == QStringLiteral("AUTO_36"))
                                       ? transferDataDisplayText()
                                       : (rowType == QStringLiteral("AUTO_CRC"))
                                         ? QStringLiteral("自动附加固件长度和CRC32")
                                       : (rowType == QStringLiteral("DELAY"))
                                         ? item->text()
                                       : normalizeHexDisplay(item->text());
            if (normalized != item->text()) {
                m_flowTable->blockSignals(true);
                item->setText(normalized);
                m_flowTable->blockSignals(false);
            }
        }
        refreshTransferDataRowWidgets();
        refreshDelayRowWidgets();
        m_execFlowState->setText(QStringLiteral("执行器流程状态: 未应用(编辑器已变更)"));
    });

    connect(m_serviceList, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item)
            return;
        const QString key = item->data(0, Qt::UserRole).toString();
        if (key.isEmpty())
            return;
        const QList<ServiceTemplateEntry> templates = commonServiceTemplates();
        for (const ServiceTemplateEntry &e : templates) {
            if (e.key != key)
                continue;
            const int row = addFlowRow(e.name, e.type, e.reqHex, e.timeoutMs, e.retries, e.enabled, e.expectedSidHex);
            if (e.key.startsWith(QStringLiteral("11_")) && e.type == QStringLiteral("RAW")) {
                const int delayRow = addFlowRow(QStringLiteral("%1后延时").arg(e.name),
                                                QStringLiteral("DELAY"),
                                                delayDisplayText(),
                                                1000,
                                                0,
                                                false,
                                                QString());
                if (QTableWidgetItem *delayReqItem = m_flowTable->item(delayRow, 3))
                    delayReqItem->setData(RoleDelayControl, true);
            }
            Q_UNUSED(row);
            refreshDelayRowWidgets();
            m_execFlowState->setText(QStringLiteral("执行器流程状态: 未应用(编辑器已变更)"));
            break;
        }
    });

    auto *flowToolRow = new QHBoxLayout;
    flowToolRow->addWidget(flowBtnAddRaw);
    flowToolRow->addWidget(flowBtnDel);
    flowToolRow->addWidget(flowBtnUp);
    flowToolRow->addWidget(flowBtnDown);
    flowToolRow->addWidget(flowBtnDefault);
    flowToolRow->addWidget(flowBtnImport);
    flowToolRow->addWidget(flowBtnExport);
    flowToolRow->addWidget(m_applyToExecutor);
    flowToolRow->addStretch();

    auto *flowBox = new QGroupBox(QStringLiteral("刷写流程编排(可编辑)"));
    auto *flowLayout = new QVBoxLayout(flowBox);
    flowLayout->addLayout(flowToolRow);
    flowLayout->addWidget(m_flowTable, 1);

    auto *editorLayout = new QHBoxLayout;
    editorLayout->addWidget(serviceBox, 2);
    editorLayout->addWidget(flowBox, 8);

    auto *editorPage = new QWidget;
    editorPage->setLayout(editorLayout);

    auto *execBtnRow = new QHBoxLayout;
    execBtnRow->addWidget(m_start);
    execBtnRow->addWidget(m_abort);
    execBtnRow->addStretch();

    auto *execFlowBox = new QGroupBox(QStringLiteral("执行器流程(来自编辑器)"));
    auto *execFlowLayout = new QVBoxLayout(execFlowBox);
    execFlowLayout->addWidget(m_execFlowState);
    execFlowLayout->addWidget(m_execFlowTable, 1);
    execFlowLayout->addLayout(execBtnRow);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);

    m_blockStatus = new QLabel(QStringLiteral("TransferData 块进度: 未开始"));
    m_blockTable = new QTableWidget;
    m_blockTable->setColumnCount(3);
    m_blockTable->setHorizontalHeaderLabels(QStringList()
                                            << QStringLiteral("块")
                                            << QStringLiteral("状态")
                                            << QStringLiteral("进度"));
    m_blockTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_blockTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_blockTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_blockTable->verticalHeader()->setVisible(false);
    m_blockTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_blockTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_blockTable->setAlternatingRowColors(true);
    m_blockTable->setMaximumHeight(220);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(3000);

    auto *execPageLayout = new QVBoxLayout;
    execPageLayout->addWidget(execFlowBox, 2);
    execPageLayout->addWidget(m_progress);
    execPageLayout->addWidget(m_blockStatus);
    execPageLayout->addWidget(m_blockTable);
    execPageLayout->addWidget(m_log, 1);
    auto *execPage = new QWidget;
    execPage->setLayout(execPageLayout);

    m_tabs = new QTabWidget;
    m_tabs->addTab(editorPage, QStringLiteral("编辑器"));
    m_tabs->addTab(execPage, QStringLiteral("执行器"));

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(topBox);
    mainLayout->addWidget(m_tabs, 1);

    connect(m_browse, &QPushButton::clicked, this, &UdsFlashDialog::onBrowse);
    connect(m_start, &QPushButton::clicked, this, &UdsFlashDialog::onStart);
    connect(m_abort, &QPushButton::clicked, this, &UdsFlashDialog::onAbort);

    loadDefaultFlow();
    refreshTransferDataRowWidgets();
    applyWorkbenchStyle();
}

UdsFlashDialog::~UdsFlashDialog()
{
    if (m_flash) {
        m_flash->requestAbort();
        m_flash->wait(5000);
        delete m_flash;
        m_flash = nullptr;
    }
}

// 切换底层工作线程（设备重连后可复用当前界面）。
void UdsFlashDialog::setWorker(CanWorker *worker)
{
    m_worker = worker;
}

// 统一设置界面忙闲态，避免刷写中误操作。
void UdsFlashDialog::setUiBusy(bool busy)
{
    m_start->setEnabled(!busy);
    m_browse->setEnabled(!busy);
    m_abort->setEnabled(busy);
    m_applyToExecutor->setEnabled(!busy);

    m_txId->setEnabled(!busy);
    m_rxId->setEnabled(!busy);
    m_ext->setEnabled(!busy);
    m_addr->setEnabled(!busy);
    m_payload->setEnabled(!busy);
    m_timeout->setEnabled(!busy);
    m_testerPresent->setEnabled(!busy);
    m_flowTable->setEnabled(!busy);
    m_execFlowTable->setEnabled(!busy);
    m_serviceList->setEnabled(!busy);
    refreshTransferDataRowWidgets();
    refreshDelayRowWidgets();
}

// 向流程编辑表追加一条步骤记录。
int UdsFlashDialog::addFlowRow(const QString &name,
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

    const QString normalizedType = type.trimmed().toUpper();
    const QString normalizedReq = (normalizedType == QStringLiteral("AUTO_36"))
                                  ? transferDataDisplayText()
                                  : (normalizedType == QStringLiteral("AUTO_CRC"))
                                    ? QStringLiteral("自动附加固件长度和CRC32")
                                  : (normalizedType == QStringLiteral("DELAY"))
                                    ? delayDisplayText()
                                  : normalizeHexDisplay(requestHex);
    m_flowTable->setItem(row, 1, new QTableWidgetItem(name));
    m_flowTable->setItem(row, 2, new QTableWidgetItem(normalizedType));
    m_flowTable->setItem(row, 3, new QTableWidgetItem(normalizedReq));
    m_flowTable->setItem(row, 4, new QTableWidgetItem(QString::number(timeoutMs)));
    m_flowTable->setItem(row, 5, new QTableWidgetItem(QString::number(retries)));
    auto *check = new QTableWidgetItem;
    check->setFlags((check->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    check->setCheckState(expectedSidHex.isEmpty() ? Qt::Unchecked : Qt::Checked);
    m_flowTable->setItem(row, 6, check);
    m_flowTable->setItem(row, 7, new QTableWidgetItem(expectedSidHex.trimmed().toUpper()));
    refreshTransferDataRowWidgets();
    refreshDelayRowWidgets();
    return row;
}

// 加载默认刷写流程模板。
void UdsFlashDialog::loadDefaultFlow()
{
    m_flowTable->setRowCount(0);
    m_execFlowState->setText(QStringLiteral("执行器流程状态: 未应用(编辑器已变更)"));
    refreshTransferDataRowWidgets();
    refreshDelayRowWidgets();
}

// 将表格配置解析为可执行步骤列表，并执行合法性校验。
bool UdsFlashDialog::buildFlowFromTable(QTableWidget *table, QList<UdsFlashStep> *steps, QString *err) const
{
    if (!table || !steps)
        return false;
    steps->clear();

    for (int row = 0; row < table->rowCount(); ++row) {
        UdsFlashStep step;

        const QTableWidgetItem *en = table->item(row, 0);
        step.enabled = en && en->checkState() == Qt::Checked;

        const QString name = table->item(row, 1) ? table->item(row, 1)->text().trimmed() : QString();
        const QString type = table->item(row, 2) ? table->item(row, 2)->text().trimmed().toUpper() : QString();
        const QString reqHex = table->item(row, 3) ? table->item(row, 3)->text().trimmed() : QString();
        const QString timeoutStr = table->item(row, 4) ? table->item(row, 4)->text().trimmed() : QString();
        const QString retryStr = table->item(row, 5) ? table->item(row, 5)->text().trimmed() : QString();
        const QTableWidgetItem *checkItem = table->item(row, 6);
        const QString sidHex = table->item(row, 7) ? table->item(row, 7)->text().trimmed() : QString();

        step.name = name.isEmpty() ? QStringLiteral("Step%1").arg(row + 1) : name;

        if (type == QStringLiteral("RAW")) {
            step.type = UdsFlashStep::StepRawRequest;
            step.request = parseHexBytes(reqHex);
            if (step.request.isEmpty()) {
                if (err)
                    *err = QStringLiteral("第%1行 RAW 请求数据非法").arg(row + 1);
                return false;
            }
        } else if (type == QStringLiteral("DELAY")) {
            step.type = UdsFlashStep::StepDelay;
        } else if (type == QStringLiteral("AUTO_ERASE")) {
            step.type = UdsFlashStep::StepRoutineEraseAuto;
        } else if (type == QStringLiteral("AUTO_CRC")) {
            step.type = UdsFlashStep::StepRoutineCrcAuto;
        } else if (type == QStringLiteral("AUTO_34")) {
            step.type = UdsFlashStep::StepRequestDownloadAuto;
        } else if (type == QStringLiteral("AUTO_36")) {
            step.type = UdsFlashStep::StepTransferDataAuto;
        } else if (type == QStringLiteral("AUTO_37")) {
            step.type = UdsFlashStep::StepTransferExitAuto;
        } else {
            if (err)
                *err = QStringLiteral("第%1行 类型非法，仅支持 RAW / DELAY / AUTO_ERASE / AUTO_CRC / AUTO_34 / AUTO_36 / AUTO_37").arg(row + 1);
            return false;
        }

        bool ok = false;
        int timeoutVal = timeoutStr.toInt(&ok);
        if (!ok || timeoutVal <= 0)
            timeoutVal = m_timeout->value();
        step.timeoutMs = timeoutVal;

        int retryVal = retryStr.toInt(&ok);
        if (!ok || retryVal < 0)
            retryVal = 0;
        step.retries = retryVal;

        step.checkPositiveSid = checkItem && checkItem->checkState() == Qt::Checked;
        if (step.checkPositiveSid) {
            quint32 sidVal = 0;
            if (!parseHex32(sidHex, &sidVal) || sidVal > 0xFFu) {
                if (err)
                    *err = QStringLiteral("第%1行 期望SID非法").arg(row + 1);
                return false;
            }
            step.expectedPositiveSid = quint8(sidVal);
        }

        steps->append(step);
    }

    return true;
}

// 将编辑器中的流程完整复制到执行器表格。
void UdsFlashDialog::syncEditorToExecutor()
{
    m_execFlowTable->setRowCount(0);
    for (int row = 0; row < m_flowTable->rowCount(); ++row) {
        const int nrow = m_execFlowTable->rowCount();
        m_execFlowTable->insertRow(nrow);
        for (int c = 0; c < m_flowTable->columnCount(); ++c) {
            QTableWidgetItem *src = m_flowTable->item(row, c);
            if (!src) {
                m_execFlowTable->setItem(nrow, c, new QTableWidgetItem);
                continue;
            }
            QTableWidgetItem *dst = src->clone();
            dst->setFlags(dst->flags() & ~Qt::ItemIsEditable);
            if (c == 0 || c == 6)
                dst->setFlags((dst->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
            m_execFlowTable->setItem(nrow, c, dst);
        }
    }
}

QString UdsFlashDialog::transferDataDisplayText() const
{
    if (m_filePath.isEmpty())
        return QStringLiteral("选择固件后自动按块/分帧传输");
    return QStringLiteral("固件: %1").arg(QFileInfo(m_filePath).fileName());
}

void UdsFlashDialog::refreshTransferDataRowWidgets()
{
    for (int row = 0; row < m_flowTable->rowCount(); ++row) {
        const QString type = m_flowTable->item(row, 2) ? m_flowTable->item(row, 2)->text().trimmed().toUpper() : QString();
        if (type != QStringLiteral("AUTO_36")) {
            if (m_flowTable->cellWidget(row, 3))
                m_flowTable->removeCellWidget(row, 3);
            continue;
        }

        QTableWidgetItem *reqItem = m_flowTable->item(row, 3);
        if (!reqItem) {
            reqItem = new QTableWidgetItem;
            m_flowTable->setItem(row, 3, reqItem);
        }
        reqItem->setText(transferDataDisplayText());
        reqItem->setToolTip(m_filePath.isEmpty() ? QStringLiteral("未选择固件") : m_filePath);

        auto *cell = new QWidget(m_flowTable);
        auto *layout = new QHBoxLayout(cell);
        layout->setContentsMargins(4, 0, 4, 0);
        layout->setSpacing(6);

        auto *btn = new QPushButton(QStringLiteral("打开固件..."), cell);
        btn->setEnabled(!m_abort->isEnabled());
        auto *label = new QLabel(m_filePath.isEmpty() ? QStringLiteral("未选择固件") : QFileInfo(m_filePath).fileName(), cell);
        label->setToolTip(m_filePath.isEmpty() ? QStringLiteral("未选择固件") : m_filePath);
        label->setWordWrap(false);

        layout->addWidget(btn);
        layout->addWidget(label, 1);
        connect(btn, &QPushButton::clicked, this, &UdsFlashDialog::onBrowse);
        m_flowTable->setCellWidget(row, 3, cell);
    }
}

void UdsFlashDialog::refreshDelayRowWidgets()
{
    const QList<DelayOption> options = delayOptions();
    for (int row = 0; row < m_flowTable->rowCount(); ++row) {
        const QString type = m_flowTable->item(row, 2) ? m_flowTable->item(row, 2)->text().trimmed().toUpper() : QString();
        if (type != QStringLiteral("DELAY"))
            continue;

        QTableWidgetItem *reqItem = m_flowTable->item(row, 3);
        if (!reqItem) {
            reqItem = new QTableWidgetItem;
            m_flowTable->setItem(row, 3, reqItem);
        }
        reqItem->setData(RoleDelayControl, true);

        QTableWidgetItem *enableItem = m_flowTable->item(row, 0);
        if (!enableItem)
            continue;
        const bool enabled = enableItem->checkState() == Qt::Checked;

        QTableWidgetItem *timeoutItem = m_flowTable->item(row, 4);
        if (!timeoutItem) {
            timeoutItem = new QTableWidgetItem(QStringLiteral("1000"));
            m_flowTable->setItem(row, 4, timeoutItem);
        }

        bool ok = false;
        int timeoutMs = timeoutItem->text().trimmed().toInt(&ok);
        if (!ok || timeoutMs < 0)
            timeoutMs = 1000;

        const QString summaryText = delaySummaryText(enabled, timeoutMs);
        if (reqItem->text() != summaryText) {
            m_flowTable->blockSignals(true);
            reqItem->setText(summaryText);
            m_flowTable->blockSignals(false);
        }
        reqItem->setToolTip(QStringLiteral("勾选后启用延时；选择“自定义”时可在“超时(ms)”列输入毫秒值"));

        auto *cell = new QWidget(m_flowTable);
        auto *layout = new QHBoxLayout(cell);
        layout->setContentsMargins(4, 0, 4, 0);
        layout->setSpacing(6);

        auto *check = new QCheckBox(QStringLiteral("添加延时"), cell);
        auto *combo = new QComboBox(cell);
        combo->setMinimumWidth(140);
        for (const DelayOption &option : options)
            combo->addItem(option.label, option.timeoutMs);

        int currentIndex = combo->count() - 1;
        for (int i = 0; i < options.size(); ++i) {
            if (!options.at(i).custom && options.at(i).timeoutMs == timeoutMs) {
                currentIndex = i;
                break;
            }
        }

        check->setChecked(enabled);
        combo->setCurrentIndex(qMax(0, currentIndex));
        combo->setEnabled(enabled);

        layout->addWidget(check);
        layout->addWidget(combo, 1);

        connect(check, &QCheckBox::toggled, this, [this, row, combo](bool checked) {
            combo->setEnabled(checked);
            if (QTableWidgetItem *item = m_flowTable->item(row, 0)) {
                m_flowTable->blockSignals(true);
                item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
                m_flowTable->blockSignals(false);
            }
            refreshDelayRowWidgets();
            m_execFlowState->setText(QStringLiteral("执行器流程状态: 未应用(编辑器已变更)"));
        });

        connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, row](int) {
            QComboBox *source = qobject_cast<QComboBox*>(sender());
            if (!source)
                return;
            const int timeoutValue = source->currentData().toInt();
            if (timeoutValue >= 0) {
                if (QTableWidgetItem *item = m_flowTable->item(row, 4)) {
                    m_flowTable->blockSignals(true);
                    item->setText(QString::number(timeoutValue));
                    m_flowTable->blockSignals(false);
                }
            }
            refreshDelayRowWidgets();
            m_execFlowState->setText(QStringLiteral("执行器流程状态: 未应用(编辑器已变更)"));
        });

        m_flowTable->setCellWidget(row, 3, cell);
    }
}

void UdsFlashDialog::resetTransferBlockProgress(int totalBlocks)
{
    m_totalTransferBlocks = qMax(0, totalBlocks);
    m_activeTransferBlock = 0;
    m_currentBlockFrame = 0;

    m_blockTable->setRowCount(m_totalTransferBlocks);
    for (int row = 0; row < m_totalTransferBlocks; ++row) {
        m_blockTable->setItem(row, 0, new QTableWidgetItem(QStringLiteral("块 %1").arg(row + 1)));
        m_blockTable->setItem(row, 1, new QTableWidgetItem(QStringLiteral("等待")));
        auto *bar = new QProgressBar(m_blockTable);
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setFormat(QStringLiteral("%p%"));
        m_blockTable->setCellWidget(row, 2, bar);
    }

    m_blockStatus->setText(m_totalTransferBlocks > 0
                           ? QStringLiteral("TransferData 块进度: 0 / %1").arg(m_totalTransferBlocks)
                           : QStringLiteral("TransferData 块进度: 未开始"));
}

int UdsFlashDialog::estimateTransferFramesPerBlock() const
{
    const int chunkPayload = (m_runtimeChunkPayload > 0) ? m_runtimeChunkPayload : m_payload->value();
    const int pduBytes = chunkPayload + 2;
    if (pduBytes <= 7)
        return 1;
    return 1 + ((pduBytes - 6) + 6) / 7;
}

// 选择固件文件。
void UdsFlashDialog::onBrowse()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("选择固件文件"),
                                                      QString(),
                                                      QStringLiteral("Firmware (*.bin *.hex *.s19 *.elf);;All (*.*)"));
    if (path.isEmpty())
        return;

    m_filePath = path;
    m_fileLabel->setText(path);
    refreshTransferDataRowWidgets();
    refreshDelayRowWidgets();
}

// 执行刷写入口：参数校验、流程构建、线程启动。
void UdsFlashDialog::onStart()
{
    if (!m_worker || !m_worker->isDeviceOpen()) {
        QMessageBox::warning(this, QStringLiteral("UDS刷写"), QStringLiteral("请先连接CAN设备"));
        return;
    }

    if (m_filePath.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("UDS刷写"), QStringLiteral("请先选择固件文件"));
        return;
    }

    quint32 tx = 0;
    quint32 rx = 0;
    quint32 addr = 0;
    if (!parseHex32(m_txId->text(), &tx) || !parseHex32(m_rxId->text(), &rx)) {
        QMessageBox::warning(this, QStringLiteral("UDS刷写"), QStringLiteral("请求ID/响应ID请输入十六进制"));
        return;
    }

    if (!parseHex32(m_addr->text(), &addr)) {
        QMessageBox::warning(this, QStringLiteral("UDS刷写"), QStringLiteral("下载起始地址请输入十六进制"));
        return;
    }

    if (m_execFlowTable->rowCount() <= 0) {
        QMessageBox::warning(this, QStringLiteral("UDS刷写"), QStringLiteral("执行器中没有流程，请先在编辑器中点击“应用到执行器”"));
        return;
    }

    QList<UdsFlashStep> steps;
    QString flowErr;
    if (!buildFlowFromTable(m_execFlowTable, &steps, &flowErr)) {
        QMessageBox::warning(this, QStringLiteral("UDS刷写"), flowErr);
        return;
    }

    if (m_flash) {
        m_flash->requestAbort();
        m_flash->wait(3000);
        delete m_flash;
        m_flash = nullptr;
    }

    m_flash = new UdsFlashWorker(this);
    connect(m_flash, &UdsFlashWorker::logLine, this, &UdsFlashDialog::onFlashLog, Qt::QueuedConnection);
    connect(m_flash, &UdsFlashWorker::progressValue, this, &UdsFlashDialog::onFlashProgress, Qt::QueuedConnection);
    connect(m_flash, &UdsFlashWorker::finishedOk, this, &UdsFlashDialog::onFlashOk, Qt::QueuedConnection);
    connect(m_flash, &UdsFlashWorker::finishedError, this, &UdsFlashDialog::onFlashErr, Qt::QueuedConnection);
    connect(m_flash, &UdsFlashWorker::flashIsoTpTxFrame, this, &UdsFlashDialog::onFlashIsoTpTxFrame, Qt::QueuedConnection);
    connect(m_flash, &UdsFlashWorker::transferDataBlockStarted, this, &UdsFlashDialog::onTransferDataBlockStarted, Qt::QueuedConnection);
    connect(m_flash, &QThread::finished, this, &UdsFlashDialog::onThreadFinished, Qt::QueuedConnection);

    m_flash->configure(m_worker,
                       tx,
                       rx,
                       m_ext->isChecked(),
                       m_filePath,
                       addr,
                       m_payload->value(),
                       m_timeout->value(),
                       m_testerPresent->isChecked(),
                       steps);

    m_log->clear();
    m_progress->setValue(0);
    m_runtimeChunkPayload = 0;
    resetTransferBlockProgress(0);
    setUiBusy(true);
    m_flash->start();
}

// 用户点击中止按钮后请求线程停止。
void UdsFlashDialog::onAbort()
{
    if (m_flash)
        m_flash->requestAbort();
}

// 插入一条自定义 RAW 步骤。
void UdsFlashDialog::onFlowAddRaw()
{
    addFlowRow(QStringLiteral("自定义RAW"), QStringLiteral("RAW"), QString(), m_timeout->value(), 0, true, QString());
    m_execFlowState->setText(QStringLiteral("执行器流程状态: 未应用(编辑器已变更)"));
}

// 删除当前选中流程步骤。
void UdsFlashDialog::onFlowDelete()
{
    const int row = m_flowTable->currentRow();
    if (row < 0)
        return;
    m_flowTable->removeRow(row);
    m_execFlowState->setText(QStringLiteral("执行器流程状态: 未应用(编辑器已变更)"));
    refreshTransferDataRowWidgets();
    refreshDelayRowWidgets();
}

// 将当前步骤上移一位。
void UdsFlashDialog::onFlowMoveUp()
{
    const int row = m_flowTable->currentRow();
    if (row <= 0)
        return;

    m_flowTable->insertRow(row - 1);
    for (int c = 0; c < m_flowTable->columnCount(); ++c)
        m_flowTable->setItem(row - 1, c, m_flowTable->takeItem(row + 1, c));
    m_flowTable->removeRow(row + 1);
    m_flowTable->selectRow(row - 1);
    m_execFlowState->setText(QStringLiteral("执行器流程状态: 未应用(编辑器已变更)"));
    refreshTransferDataRowWidgets();
    refreshDelayRowWidgets();
}

// 将当前步骤下移一位。
void UdsFlashDialog::onFlowMoveDown()
{
    const int row = m_flowTable->currentRow();
    if (row < 0 || row >= m_flowTable->rowCount() - 1)
        return;

    m_flowTable->insertRow(row + 2);
    for (int c = 0; c < m_flowTable->columnCount(); ++c)
        m_flowTable->setItem(row + 2, c, m_flowTable->takeItem(row, c));
    m_flowTable->removeRow(row);
    m_flowTable->selectRow(row + 1);
    m_execFlowState->setText(QStringLiteral("执行器流程状态: 未应用(编辑器已变更)"));
    refreshTransferDataRowWidgets();
    refreshDelayRowWidgets();
}

// 重新加载默认流程。
void UdsFlashDialog::onFlowLoadDefault()
{
    loadDefaultFlow();
}

// 对编辑器流程做校验后同步到执行器。
void UdsFlashDialog::onApplyToExecutor()
{
    QString flowErr;
    QList<UdsFlashStep> temp;
    if (!buildFlowFromTable(m_flowTable, &temp, &flowErr)) {
        QMessageBox::warning(this, QStringLiteral("UDS刷写"), QStringLiteral("编辑器流程有误: %1").arg(flowErr));
        return;
    }

    syncEditorToExecutor();
    m_execFlowState->setText(QStringLiteral("执行器流程状态: 已应用 (%1 步)")
                                 .arg(m_execFlowTable->rowCount()));
    m_tabs->setCurrentIndex(1);
}

// 导出流程为 JSON 文件。
void UdsFlashDialog::onFlowExport()
{
    const QString path = QFileDialog::getSaveFileName(this,
                                                      QStringLiteral("导出流程"),
                                                      QStringLiteral("uds_flow.json"),
                                                      QStringLiteral("UDS Flow (*.json)"));
    if (path.isEmpty())
        return;

    QJsonArray arr;
    for (int row = 0; row < m_flowTable->rowCount(); ++row) {
        QJsonObject o;
        const QTableWidgetItem *en = m_flowTable->item(row, 0);
        const QTableWidgetItem *sidChk = m_flowTable->item(row, 6);
        o.insert(QStringLiteral("enabled"), en && en->checkState() == Qt::Checked);
        o.insert(QStringLiteral("name"), m_flowTable->item(row, 1) ? m_flowTable->item(row, 1)->text().trimmed() : QString());
        o.insert(QStringLiteral("type"), m_flowTable->item(row, 2) ? m_flowTable->item(row, 2)->text().trimmed().toUpper() : QString());
        o.insert(QStringLiteral("request"), m_flowTable->item(row, 3) ? m_flowTable->item(row, 3)->text().trimmed().toUpper() : QString());
        o.insert(QStringLiteral("timeoutMs"), m_flowTable->item(row, 4) ? m_flowTable->item(row, 4)->text().trimmed().toInt() : 3000);
        o.insert(QStringLiteral("retries"), m_flowTable->item(row, 5) ? m_flowTable->item(row, 5)->text().trimmed().toInt() : 0);
        o.insert(QStringLiteral("checkSid"), sidChk && sidChk->checkState() == Qt::Checked);
        o.insert(QStringLiteral("expectedSid"), m_flowTable->item(row, 7) ? m_flowTable->item(row, 7)->text().trimmed().toUpper() : QString());
        arr.append(o);
    }

    QJsonObject params;
    params.insert(QStringLiteral("txIdHex"), m_txId->text().trimmed().toUpper());
    params.insert(QStringLiteral("rxIdHex"), m_rxId->text().trimmed().toUpper());
    params.insert(QStringLiteral("extendedFrame"), m_ext->isChecked());
    params.insert(QStringLiteral("flashStartAddressHex"), m_addr->text().trimmed().toUpper());
    params.insert(QStringLiteral("maxPayloadPerBlock"), m_payload->value());
    params.insert(QStringLiteral("defaultTimeoutMs"), m_timeout->value());
    params.insert(QStringLiteral("testerPresentDuringFlash"), m_testerPresent->isChecked());

    QJsonObject firmware;
    firmware.insert(QStringLiteral("path"), m_filePath);
    firmware.insert(QStringLiteral("name"), m_filePath.isEmpty() ? QString() : QFileInfo(m_filePath).fileName());

    QJsonObject root;
    root.insert(QStringLiteral("formatVersion"), 2);
    root.insert(QStringLiteral("params"), params);
    root.insert(QStringLiteral("firmware"), firmware);
    root.insert(QStringLiteral("steps"), arr);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("UDS刷写"), QStringLiteral("流程导出失败: 无法写入文件"));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    m_log->appendPlainText(QStringLiteral("流程已导出: %1").arg(path));
}

// 从 JSON 文件导入流程。
void UdsFlashDialog::onFlowImport()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("导入流程"),
                                                      QString(),
                                                      QStringLiteral("UDS Flow (*.json)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("UDS刷写"), QStringLiteral("流程导入失败: 无法读取文件"));
        return;
    }

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    f.close();
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, QStringLiteral("UDS刷写"), QStringLiteral("流程导入失败: JSON格式不正确"));
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonArray arr = root.value(QStringLiteral("steps")).toArray();
    if (arr.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("UDS刷写"), QStringLiteral("流程导入失败: steps为空"));
        return;
    }

    const QJsonObject params = root.value(QStringLiteral("params")).toObject();
    if (!params.isEmpty()) {
        const QString txIdHex = params.value(QStringLiteral("txIdHex")).toString().trimmed().toUpper();
        const QString rxIdHex = params.value(QStringLiteral("rxIdHex")).toString().trimmed().toUpper();
        const QString flashStartHex = params.value(QStringLiteral("flashStartAddressHex")).toString().trimmed().toUpper();
        if (!txIdHex.isEmpty())
            m_txId->setText(txIdHex);
        if (!rxIdHex.isEmpty())
            m_rxId->setText(rxIdHex);
        if (!flashStartHex.isEmpty())
            m_addr->setText(flashStartHex);
        m_ext->setChecked(params.value(QStringLiteral("extendedFrame")).toBool(m_ext->isChecked()));
        if (params.contains(QStringLiteral("maxPayloadPerBlock")))
            m_payload->setValue(qBound(m_payload->minimum(),
                                       params.value(QStringLiteral("maxPayloadPerBlock")).toInt(m_payload->value()),
                                       m_payload->maximum()));
        if (params.contains(QStringLiteral("defaultTimeoutMs")))
            m_timeout->setValue(qBound(m_timeout->minimum(),
                                       params.value(QStringLiteral("defaultTimeoutMs")).toInt(m_timeout->value()),
                                       m_timeout->maximum()));
        m_testerPresent->setChecked(params.value(QStringLiteral("testerPresentDuringFlash")).toBool(m_testerPresent->isChecked()));
    }

    QString importWarning;
    const QJsonObject firmware = root.value(QStringLiteral("firmware")).toObject();
    if (!firmware.isEmpty()) {
        m_filePath = firmware.value(QStringLiteral("path")).toString().trimmed();
        if (!m_filePath.isEmpty() && !QFileInfo::exists(m_filePath)) {
            importWarning = QStringLiteral("已恢复固件路径，但文件不存在: %1").arg(m_filePath);
        }
        m_fileLabel->setText(m_filePath.isEmpty() ? QStringLiteral("未选择固件") : m_filePath);
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

    syncEditorToExecutor();
    m_log->appendPlainText(QStringLiteral("流程已导入: %1").arg(path));
    if (!importWarning.isEmpty())
        m_log->appendPlainText(importWarning);
    m_execFlowState->setText(QStringLiteral("执行器流程状态: 已应用 (%1 步)")
                                 .arg(m_execFlowTable->rowCount()));
    refreshTransferDataRowWidgets();
    refreshDelayRowWidgets();
    if (!importWarning.isEmpty())
        QMessageBox::warning(this, QStringLiteral("UDS刷写"), importWarning);
}

// 应用工作台样式（接近 ZCANPRO 视觉风格）。
void UdsFlashDialog::applyWorkbenchStyle()
{
    setStyleSheet(QStringLiteral(
        "QDialog { background:#CED6E0; }"
        "QGroupBox {"
        "  border:1px solid #8EA6C1;"
        "  border-radius:4px;"
        "  margin-top:10px;"
        "  background:#EAF0F6;"
        "  font-weight:bold;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin:margin;"
        "  left:8px;"
        "  padding:0 4px;"
        "  color:#27486B;"
        "}"
        "QLineEdit,QSpinBox,QTreeWidget,QTableWidget,QPlainTextEdit {"
        "  background:#FFFFFF;"
        "  border:1px solid #9DB2C9;"
        "  selection-background-color:#6E95BF;"
        "}"
        "QHeaderView::section {"
        "  background:#D9E4F1;"
        "  border:1px solid #9DB2C9;"
        "  padding:4px;"
        "}"
        "QPushButton {"
        "  background:#E7EEF7;"
        "  border:1px solid #89A3C0;"
        "  border-radius:3px;"
        "  padding:4px 10px;"
        "}"
        "QPushButton:hover { background:#D6E6F8; }"
        "QPushButton:pressed { background:#C8DBF1; }"
        "QProgressBar {"
        "  border:1px solid #8EA6C1;"
        "  background:#F5F8FB;"
        "  text-align:center;"
        "}"
        "QProgressBar::chunk { background:#5E89B9; }"));
}

// 追加日志行。
void UdsFlashDialog::onFlashLog(const QString &line)
{
    m_log->appendPlainText(line);
    const QString marker = QStringLiteral("RequestDownload OK, chunk=");
    const int idx = line.indexOf(marker);
    if (idx >= 0) {
        bool ok = false;
        const int value = line.mid(idx + marker.size()).trimmed().toInt(&ok);
        if (ok && value > 0)
            m_runtimeChunkPayload = value;
    }
}

// 更新进度条值。
void UdsFlashDialog::onFlashProgress(int pct)
{
    m_progress->setValue(qBound(0, pct, 100));
}

// 刷写成功回调。
void UdsFlashDialog::onFlashOk(const QString &summary)
{
    if (m_activeTransferBlock > 0 && m_activeTransferBlock <= m_blockTable->rowCount()) {
        if (QTableWidgetItem *status = m_blockTable->item(m_activeTransferBlock - 1, 1))
            status->setText(QStringLiteral("完成"));
        if (auto *bar = qobject_cast<QProgressBar*>(m_blockTable->cellWidget(m_activeTransferBlock - 1, 2)))
            bar->setValue(100);
    }
    if (m_totalTransferBlocks > 0)
        m_blockStatus->setText(QStringLiteral("TransferData 块进度: %1 / %1").arg(m_totalTransferBlocks));
    m_log->appendPlainText(summary);
    QMessageBox::information(this, QStringLiteral("UDS刷写"), summary);
}

// 刷写失败回调。
void UdsFlashDialog::onFlashErr(const QString &reason)
{
    if (m_activeTransferBlock > 0 && m_activeTransferBlock <= m_blockTable->rowCount()) {
        if (QTableWidgetItem *status = m_blockTable->item(m_activeTransferBlock - 1, 1))
            status->setText(QStringLiteral("失败"));
    }
    m_log->appendPlainText(QStringLiteral("错误: %1").arg(reason));
    QMessageBox::warning(this, QStringLiteral("UDS刷写"), reason);
}

// 刷写线程结束后恢复界面可编辑状态。
void UdsFlashDialog::onThreadFinished()
{
    setUiBusy(false);
    refreshTransferDataRowWidgets();
    refreshDelayRowWidgets();
}

// 输出 ISO-TP 发帧细节（用于调试 TransferData）。
void UdsFlashDialog::onFlashIsoTpTxFrame(const QString &stepName, int frameIndex1Based)
{
    if (!stepName.contains(QLatin1String("TransferData")))
        return;

    m_log->appendPlainText(QStringLiteral("[ISO-TP发送] %1 帧序号 %2").arg(stepName).arg(frameIndex1Based));
    if (m_activeTransferBlock <= 0 || m_activeTransferBlock > m_blockTable->rowCount())
        return;

    m_currentBlockFrame = qMax(m_currentBlockFrame, frameIndex1Based);
    const int expectedFrames = qMax(1, estimateTransferFramesPerBlock());
    const int pct = qMin(99, m_currentBlockFrame * 100 / expectedFrames);
    if (QTableWidgetItem *status = m_blockTable->item(m_activeTransferBlock - 1, 1))
        status->setText(QStringLiteral("发送中(%1/%2帧)").arg(m_currentBlockFrame).arg(expectedFrames));
    if (auto *bar = qobject_cast<QProgressBar*>(m_blockTable->cellWidget(m_activeTransferBlock - 1, 2)))
        bar->setValue(pct);
}

// 输出 TransferData 逻辑块进度。
void UdsFlashDialog::onTransferDataBlockStarted(int block1Based, int totalBlocks)
{
    m_log->appendPlainText(QStringLiteral("TransferData 逻辑块 %1 / %2").arg(block1Based).arg(totalBlocks));
    if (m_totalTransferBlocks != totalBlocks || m_blockTable->rowCount() != totalBlocks)
        resetTransferBlockProgress(totalBlocks);

    if (m_activeTransferBlock > 0 && m_activeTransferBlock <= m_blockTable->rowCount()) {
        if (QTableWidgetItem *status = m_blockTable->item(m_activeTransferBlock - 1, 1))
            status->setText(QStringLiteral("完成"));
        if (auto *bar = qobject_cast<QProgressBar*>(m_blockTable->cellWidget(m_activeTransferBlock - 1, 2)))
            bar->setValue(100);
    }

    m_activeTransferBlock = block1Based;
    m_currentBlockFrame = 0;
    if (m_activeTransferBlock > 0 && m_activeTransferBlock <= m_blockTable->rowCount()) {
        if (QTableWidgetItem *status = m_blockTable->item(m_activeTransferBlock - 1, 1))
            status->setText(QStringLiteral("准备发送"));
        if (auto *bar = qobject_cast<QProgressBar*>(m_blockTable->cellWidget(m_activeTransferBlock - 1, 2)))
            bar->setValue(0);
    }

    m_blockStatus->setText(QStringLiteral("TransferData 块进度: %1 / %2").arg(block1Based).arg(totalBlocks));
}
