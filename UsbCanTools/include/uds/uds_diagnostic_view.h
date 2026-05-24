#ifndef UDS_DIAGNOSTIC_VIEW_H
#define UDS_DIAGNOSTIC_VIEW_H

#include <QWidget>

class AppController;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

// UDS 诊断面板：按 ZCANPRO 风格提供服务树、请求编辑、列表发送、日志与统计。
class UdsDiagnosticWidget : public QWidget
{
    Q_OBJECT
public:
    explicit UdsDiagnosticWidget(AppController *controller, QWidget *parent = nullptr);
    ~UdsDiagnosticWidget() override = default;

private slots:
    void onConnectionChanged(bool connected);
    void onServiceItemClicked(QTreeWidgetItem *item, int column);

    void onAddCurrentToList();
    void onSendNow();

    void onListDelete();
    void onListClear();
    void onListMoveUp();
    void onListMoveDown();
    void onListSend();

    void onClearLog();
    void onResetCounters();

private:
    void buildUi();
    void applyStyle();
    void buildServiceTree();
    void updateUiEnabled(bool connected);
    void updateCountersView();

    void addListRow(const QString &name,
                    const QString &requestPduHex,
                    const QString &requestAddr,
                    bool suppressResponse,
                    bool checkResponse,
                    const QString &expectedResponsePduHex);

    bool sendRequest(const QString &name,
                     const QString &requestPduHex,
                     const QString &expectedResponsePduHex,
                     bool suppressResponse,
                     bool checkResponse);

    void appendLog(const QString &line);
    bool parseHexU32(const QString &text, quint32 *out) const;
    QByteArray parseHexBytes(const QString &text) const;
    QString toHexSpaced(const QByteArray &bytes) const;

private:
    AppController *m_controller = nullptr;      // 应用控制器

    QComboBox *m_channelCombo = nullptr;        // 通道选择
    QLineEdit *m_txIdEdit = nullptr;            // 请求ID
    QLineEdit *m_funcAddrEdit = nullptr;        // 功能地址
    QLineEdit *m_rxIdEdit = nullptr;            // 响应ID
    QComboBox *m_addrModeCombo = nullptr;       // 地址模式
    QCheckBox *m_autoFlowCheck = nullptr;       // 自动流控开关
    QSpinBox *m_timeoutSpin = nullptr;          // 请求超时(ms)

    QTreeWidget *m_serviceTree = nullptr;       // 左侧服务树

    QLineEdit *m_requestEdit = nullptr;         // 请求PDU
    QLineEdit *m_expectedRespEdit = nullptr;    // 响应PDU(期望)
    QComboBox *m_suppressCombo = nullptr;       // 抑制响应
    QComboBox *m_checkRespCombo = nullptr;      // 校验响应
    QPushButton *m_addToListBtn = nullptr;      // 添加到列表
    QPushButton *m_sendNowBtn = nullptr;        // 立即发送

    QTableWidget *m_listTable = nullptr;        // 列表发送表格
    QSpinBox *m_repeatSpin = nullptr;           // 发送次数
    QSpinBox *m_intervalSpin = nullptr;         // 请求间隔(ms)
    QPushButton *m_listDeleteBtn = nullptr;     // 删除
    QPushButton *m_listClearBtn = nullptr;      // 清空
    QPushButton *m_listUpBtn = nullptr;         // 上移
    QPushButton *m_listDownBtn = nullptr;       // 下移
    QPushButton *m_listSendBtn = nullptr;       // 列表发送

    QComboBox *m_logDisplayCombo = nullptr;     // 日志显示方式
    QPushButton *m_logClearBtn = nullptr;       // 清空日志
    QPlainTextEdit *m_logEdit = nullptr;        // 日志窗口

    QLabel *m_totalCountLabel = nullptr;        // 测试次数
    QLabel *m_passCountLabel = nullptr;         // 通过
    QLabel *m_failCountLabel = nullptr;         // 未通过
    QLabel *m_noRespCountLabel = nullptr;       // 未检索响应
    QPushButton *m_resetStatsBtn = nullptr;     // 重置统计

    int m_totalCount = 0;                       // 总次数
    int m_passCount = 0;                        // 成功次数
    int m_failCount = 0;                        // 失败次数
    int m_noRespCount = 0;                      // 无响应次数
};

#endif // UDS_DIAGNOSTIC_VIEW_H
