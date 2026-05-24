// 文件说明：主窗口模块，负责工具栏、停靠面板布局与全局交互入口。
#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>

class QString;
class QAction;
class AppController;
class CanPanelWidget;
class DeviceManagementWidget;
class QDockWidget;
class UdsDiagnosticWidget;
class UdsFlashDialog;

// 主界面窗口：统一承载设备管理、CAN 收发、ECU 刷写三个业务面板。
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    // 构造主窗口并初始化界面。
    explicit MainWindow(QWidget *parent = nullptr);
    // 析构主窗口。
    ~MainWindow() override;

private slots:
    // 打开/激活设备管理面板。
    void showDeviceDock();
    // 打开/激活 CAN 收发面板。
    void showCanDock();
    void showDiagDock();
    // 打开/激活 ECU 刷写面板。
    void showFlashDock();
    // 响应设备连接状态变化，刷新全局可用性提示。
    void onConnectionChanged(bool connected);

private:
    // 创建标准停靠窗口容器。
    QDockWidget *createDock(const QString &title, QWidget *content);
    // 构建顶部工具栏。
    void buildToolbar();
    // 构建并布置停靠面板。
    void buildDocks();
    // 应用主窗口统一样式。
    void applyStyle();

    // 应用级业务控制器（管理设备连接与消息分发）。
    AppController *m_controller = nullptr;

    // 三个业务面板的停靠容器。
    QDockWidget *m_deviceDock = nullptr;
    QDockWidget *m_canDock = nullptr;
    QDockWidget *m_diagDock = nullptr;
    QDockWidget *m_flashDock = nullptr;

    // 顶部工具栏对应动作。
    QAction *m_deviceAction = nullptr;
    QAction *m_canAction = nullptr;
    QAction *m_diagAction = nullptr;
    QAction *m_flashAction = nullptr;

    // 三个业务面板实例。
    DeviceManagementWidget *m_deviceWidget = nullptr;
    CanPanelWidget *m_canWidget = nullptr;
    UdsDiagnosticWidget *m_diagWidget = nullptr;
    UdsFlashDialog *m_flashWidget = nullptr;
};

#endif // MAIN_WINDOW_H
