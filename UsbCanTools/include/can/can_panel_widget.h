// 文件说明：CAN 收发面板模块，负责发送参数输入、日志显示与统计。
#ifndef CAN_PANEL_WIDGET_H
#define CAN_PANEL_WIDGET_H

#include <QWidget>

class AppController;
class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QTableWidget;

// CAN 收发界面组件：仅负责界面交互，底层收发由 AppController/CanWorker 完成。
class CanPanelWidget : public QWidget
{
    Q_OBJECT
public:
    // 构造 CAN 面板。
    explicit CanPanelWidget(AppController *controller, QWidget *parent = nullptr);

private slots:
    // 发送按钮槽函数。
    void onSendClicked();
    // 清空日志槽函数。
    void onClearClicked();
    // 响应连接状态变化（控制按钮可用性）。
    void onConnectionChanged(bool connected);
    // 追加一条收发帧到日志表格。
    void onFrameReceived(const QString &timeMs, const QString &dir, const QString &idHex,
                         const QString &frameType, const QString &dlc, const QString &dataHex);

private:
    // 构建界面控件。
    void buildUi();
    // 解析十六进制数据文本为字节数组。
    QByteArray parseHexBytes(const QString &text) const;
    // 解析十六进制 CAN ID。
    quint32 parseCanIdHex(const QString &text, bool *ok) const;

    // 应用控制器引用。
    AppController *m_controller = nullptr;
    // 发送 ID 输入框。
    QLineEdit *m_idEdit = nullptr;
    // 发送数据输入框。
    QLineEdit *m_dataEdit = nullptr;
    // 帧格式选择框（标准/扩展）。
    QComboBox *m_extCombo = nullptr;
    // 帧类型选择框（数据/远程）。
    QComboBox *m_rtrCombo = nullptr;
    // 发送按钮。
    QPushButton *m_sendBtn = nullptr;
    // 清空按钮。
    QPushButton *m_clearBtn = nullptr;
    // 收发统计标签。
    QLabel *m_statsLabel = nullptr;
    // 报文日志表格。
    QTableWidget *m_table = nullptr;
    // TX 统计计数。
    qint64 m_txCount = 0;
    // RX 统计计数。
    qint64 m_rxCount = 0;
    // 最大日志行数。
    static const int kMaxRows = 5000;
};

#endif // CAN_PANEL_WIDGET_H
