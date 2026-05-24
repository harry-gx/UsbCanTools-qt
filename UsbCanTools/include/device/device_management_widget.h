// 文件说明：设备管理面板模块，负责设备参数配置、连接控制与状态显示。
#ifndef DEVICE_MANAGEMENT_WIDGET_H
#define DEVICE_MANAGEMENT_WIDGET_H

#include <QWidget>

class AppController;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;

// 设备管理界面组件：面向硬件连接场景，负责参数设置和状态刷新。
class DeviceManagementWidget : public QWidget
{
    Q_OBJECT
public:
    // 构造设备管理面板。
    explicit DeviceManagementWidget(AppController *controller, QWidget *parent = nullptr);

private slots:
    // 连接/断开按钮槽函数。
    void onConnectClicked();
    // 响应连接状态变化并刷新控件状态。
    void onConnectionChanged(bool connected);
    // 连接失败提示。
    void onOpenFailed(const QString &reason);
    // 周期读取并刷新总线状态。
    void refreshBusStatus();

private:
    // 构建界面控件。
    void buildUi();
    // 根据连接状态更新控件可用性。
    void updateUiByConnection(bool connected);

    // 应用控制器引用。
    AppController *m_controller = nullptr;
    // 设备类型选择框。
    QComboBox *m_deviceType = nullptr;
    // 设备索引输入框。
    QSpinBox *m_deviceIndex = nullptr;
    // 通道选择框。
    QComboBox *m_channel = nullptr;
    // 波特率选择框。
    QComboBox *m_baud = nullptr;
    // 工作模式选择框。
    QComboBox *m_mode = nullptr;
    // 连接按钮。
    QPushButton *m_connectBtn = nullptr;
    // 板卡信息标签。
    QLabel *m_boardInfo = nullptr;
    // 总线状态标签。
    QLabel *m_busStatus = nullptr;
    // 周期状态刷新定时器。
    QTimer *m_statusTimer = nullptr;
};

#endif // DEVICE_MANAGEMENT_WIDGET_H
