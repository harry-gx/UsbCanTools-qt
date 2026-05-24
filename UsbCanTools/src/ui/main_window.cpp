// 文件说明：主窗口实现，负责工具栏与多停靠面板协同。
#include "ui/main_window.h"

#include "app/app_controller.h"
#include "can/can_panel_widget.h"
#include "device/device_management_widget.h"
#include "uds/uds_diagnostic_simple.h"
#include "uds/uds_flash_dialog.h"

#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QSizePolicy>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_controller(new AppController(this))
{
    // 初始化主窗口基础属性与停靠策略。
    setWindowTitle(QStringLiteral("EleCAN - CAN Debug Studio"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/elecan_app_256.ico")));
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);
    setAnimated(true);

    auto *placeholder = new QWidget(this);
    auto *placeholderLayout = new QVBoxLayout(placeholder);
    placeholderLayout->setContentsMargins(0, 0, 0, 0);
    placeholderLayout->addStretch();
    auto *welcomeLabel = new QLabel(QStringLiteral("欢迎使用 USB-CAN 工具平台"), placeholder);
    welcomeLabel->setObjectName(QStringLiteral("welcomeLabel"));
    welcomeLabel->setAlignment(Qt::AlignCenter);
    placeholderLayout->addWidget(welcomeLabel, 0, Qt::AlignCenter);
    auto *welcomeHint = new QLabel(QStringLiteral("请从顶部工具栏选择“设备管理 / CAN收发 / UDS诊断 / ECU刷写”开始工作"), placeholder);
    welcomeHint->setObjectName(QStringLiteral("welcomeHintLabel"));
    welcomeHint->setAlignment(Qt::AlignCenter);
    placeholderLayout->addWidget(welcomeHint, 0, Qt::AlignCenter);
    placeholderLayout->addStretch();
    placeholder->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    placeholder->setMinimumSize(0, 0);
    setCentralWidget(placeholder);
    if (QLabel *welcomeMain = placeholder->findChild<QLabel *>(QStringLiteral("welcomeLabel")))
        welcomeMain->setText(QStringLiteral("EleCAN"));
    if (QLabel *welcomeHint = placeholder->findChild<QLabel *>(QStringLiteral("welcomeHintLabel")))
        welcomeHint->setText(QStringLiteral("稳得像大象，快得像闪电"));
    auto *welcomeSubTitle = new QLabel(QStringLiteral("CAN Debug Studio"), placeholder);
    welcomeSubTitle->setObjectName(QStringLiteral("welcomeSubTitleLabel"));
    welcomeSubTitle->setAlignment(Qt::AlignCenter);
    placeholderLayout->insertWidget(2, welcomeSubTitle, 0, Qt::AlignCenter);

    buildToolbar();
    buildDocks();
    applyStyle();

    statusBar()->showMessage(QStringLiteral("状态: 未连接设备"));
    connect(m_controller, &AppController::connectionChanged, this, &MainWindow::onConnectionChanged);
    onConnectionChanged(false);
}

MainWindow::~MainWindow()
{
}

// 显示设备管理面板并同步工具栏选中状态。
void MainWindow::showDeviceDock()
{
    m_deviceDock->show();
    m_deviceDock->raise();
    m_deviceAction->setChecked(true);
}

// 显示 CAN 收发面板，未连接时给出提示。
void MainWindow::showCanDock()
{
    m_canDock->show();
    m_canDock->raise();
    m_canAction->setChecked(true);
    if (!m_controller->isConnected())
        statusBar()->showMessage(QStringLiteral("CAN收发已打开，连接设备后可发送。"), 3000);
}

void MainWindow::showDiagDock()
{
    m_diagDock->show();
    m_diagDock->raise();
    m_diagAction->setChecked(true);
    if (!m_controller->isConnected())
        statusBar()->showMessage(QStringLiteral("UDS诊断已打开，连接设备后可发送请求。"), 3000);
}

// 显示 ECU 刷写面板，未连接时给出提示。
void MainWindow::showFlashDock()
{
    m_flashDock->show();
    m_flashDock->raise();
    m_flashAction->setChecked(true);
    if (!m_controller->isConnected())
        statusBar()->showMessage(QStringLiteral("ECU刷写已打开，连接设备后可执行刷写。"), 3000);
}

// 响应连接变化并将最新 worker 注入刷写面板。
void MainWindow::onConnectionChanged(bool connected)
{
    m_flashWidget->setWorker(m_controller->worker());

    statusBar()->showMessage(connected
                                 ? QStringLiteral("状态: 设备已连接，可使用 CAN收发/UDS诊断/ECU刷写。")
                                 : QStringLiteral("状态: 未连接设备，仅可使用设备管理。"));
}

// 创建统一样式与行为的停靠窗口容器。
QDockWidget *MainWindow::createDock(const QString &title, QWidget *content)
{
    auto *dock = new QDockWidget(title, this);
    dock->setFeatures(QDockWidget::DockWidgetClosable
                      | QDockWidget::DockWidgetMovable
                      | QDockWidget::DockWidgetFloatable);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setWidget(content);
    return dock;
}

// 构建顶部功能工具栏。
void MainWindow::buildToolbar()
{
    auto *toolBar = new QToolBar(QStringLiteral("功能工具栏"), this);
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolBar->setIconSize(QSize(36, 36));
    toolBar->setFixedHeight(122);
    addToolBar(Qt::TopToolBarArea, toolBar);

    auto *brandWidget = new QWidget(this);
    auto *brandLayout = new QHBoxLayout(brandWidget);
    brandLayout->setContentsMargins(2, 0, 10, 0);
    brandLayout->setSpacing(8);
    auto *logoLabel = new QLabel(brandWidget);
    logoLabel->setObjectName(QStringLiteral("appLogoLabel"));
    logoLabel->setPixmap(QIcon(QStringLiteral(":/icons/elecan_brand_2.svg")).pixmap(58, 58));
    auto *brandTextWidget = new QWidget(brandWidget);
    auto *brandTextLayout = new QVBoxLayout(brandTextWidget);
    brandTextLayout->setContentsMargins(0, 0, 0, 0);
    brandTextLayout->setSpacing(0);
    auto *brandTitle = new QLabel(QStringLiteral("EleCAN"), brandTextWidget);
    brandTitle->setObjectName(QStringLiteral("appBrandTitle"));
    auto *brandSubTitle = new QLabel(QStringLiteral("CAN Debug Studio"), brandTextWidget);
    brandSubTitle->setObjectName(QStringLiteral("appBrandSubTitle"));
    brandTextLayout->addWidget(brandTitle);
    brandTextLayout->addWidget(brandSubTitle);
    brandLayout->addWidget(logoLabel);
    brandLayout->addWidget(brandTextWidget);
    toolBar->addWidget(brandWidget);
    toolBar->addSeparator();

    auto *groupConn = new QLabel(QStringLiteral("连接"), this);
    groupConn->setObjectName(QStringLiteral("toolbarGroupLabel"));
    toolBar->addWidget(groupConn);

    m_deviceAction = toolBar->addAction(QIcon(QStringLiteral(":/icons/device_management.svg")),
                                        QStringLiteral("设备管理"));
    m_deviceAction->setToolTip(QStringLiteral("连接和配置 CAN 硬件设备"));
    m_deviceAction->setCheckable(true);
    auto *deviceMenu = new QMenu(this);
    deviceMenu->addAction(QStringLiteral("打开设备管理面板"), this, &MainWindow::showDeviceDock);
    m_deviceAction->setMenu(deviceMenu);

    toolBar->addSeparator();

    auto *groupBiz = new QLabel(QStringLiteral("业务"), this);
    groupBiz->setObjectName(QStringLiteral("toolbarGroupLabel"));
    toolBar->addWidget(groupBiz);

    m_canAction = toolBar->addAction(QIcon(QStringLiteral(":/icons/can_panel.svg")),
                                     QStringLiteral("CAN收发"));
    m_canAction->setToolTip(QStringLiteral("常规 CAN 报文收发与日志"));
    m_canAction->setCheckable(true);
    auto *canMenu = new QMenu(this);
    canMenu->addAction(QStringLiteral("打开CAN收发面板"), this, &MainWindow::showCanDock);
    m_canAction->setMenu(canMenu);

    m_diagAction = toolBar->addAction(QIcon(QStringLiteral(":/icons/uds_diag.svg")),
                                      QStringLiteral("UDS诊断"));
    m_diagAction->setToolTip(QStringLiteral("UDS 诊断请求收发与响应查看"));
    m_diagAction->setCheckable(true);
    auto *diagMenu = new QMenu(this);
    diagMenu->addAction(QStringLiteral("打开UDS诊断面板"), this, &MainWindow::showDiagDock);
    m_diagAction->setMenu(diagMenu);

    m_flashAction = toolBar->addAction(QIcon(QStringLiteral(":/icons/ecu_flash.svg")),
                                       QStringLiteral("ECU刷写"));
    m_flashAction->setToolTip(QStringLiteral("UDS 固件刷写流程"));
    m_flashAction->setCheckable(true);
    auto *flashMenu = new QMenu(this);
    flashMenu->addAction(QStringLiteral("打开ECU刷写面板"), this, &MainWindow::showFlashDock);
    m_flashAction->setMenu(flashMenu);

    auto *group = new QActionGroup(this);
    group->setExclusive(true);
    group->addAction(m_deviceAction);
    group->addAction(m_canAction);
    group->addAction(m_diagAction);
    group->addAction(m_flashAction);

    auto *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(spacer);

    connect(m_deviceAction, &QAction::triggered, this, &MainWindow::showDeviceDock);
    connect(m_canAction, &QAction::triggered, this, &MainWindow::showCanDock);
    connect(m_diagAction, &QAction::triggered, this, &MainWindow::showDiagDock);
    connect(m_flashAction, &QAction::triggered, this, &MainWindow::showFlashDock);
}

// 构建业务面板并初始化默认停靠布局。
void MainWindow::buildDocks()
{
    m_deviceWidget = new DeviceManagementWidget(m_controller, this);
    m_canWidget = new CanPanelWidget(m_controller, this);
    m_diagWidget = new UdsDiagnosticWidget(m_controller, this);
    m_flashWidget = new UdsFlashDialog(nullptr, this);
    m_flashWidget->setWindowFlags(Qt::Widget);

    m_deviceDock = createDock(QStringLiteral("设备管理"), m_deviceWidget);
    m_canDock = createDock(QStringLiteral("CAN收发"), m_canWidget);
    m_diagDock = createDock(QStringLiteral("UDS诊断"), m_diagWidget);
    m_flashDock = createDock(QStringLiteral("ECU刷写"), m_flashWidget);

    m_deviceDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_canDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_diagDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_flashDock->setAllowedAreas(Qt::AllDockWidgetAreas);

    // Allow free dragging/floating; docking happens only when dropped on allowed edge areas.
    m_deviceDock->setFeatures(QDockWidget::DockWidgetClosable
                              | QDockWidget::DockWidgetMovable
                              | QDockWidget::DockWidgetFloatable);
    m_canDock->setFeatures(QDockWidget::DockWidgetClosable
                           | QDockWidget::DockWidgetMovable
                           | QDockWidget::DockWidgetFloatable);
    m_diagDock->setFeatures(QDockWidget::DockWidgetClosable
                            | QDockWidget::DockWidgetMovable
                            | QDockWidget::DockWidgetFloatable);
    m_flashDock->setFeatures(QDockWidget::DockWidgetClosable
                             | QDockWidget::DockWidgetMovable
                             | QDockWidget::DockWidgetFloatable);

    // If user drags out without docking, force a readable fixed horizontal rectangle size.
    connect(m_deviceDock, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        if (floating)
            m_deviceDock->resize(560, 340);
    });
    connect(m_canDock, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        if (floating)
            m_canDock->resize(900, 460);
    });
    connect(m_diagDock, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        if (floating)
            m_diagDock->resize(900, 460);
    });
    connect(m_flashDock, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        if (floating)
            m_flashDock->resize(900, 460);
    });

    addDockWidget(Qt::LeftDockWidgetArea, m_deviceDock);
    addDockWidget(Qt::RightDockWidgetArea, m_canDock);
    addDockWidget(Qt::RightDockWidgetArea, m_diagDock);
    addDockWidget(Qt::RightDockWidgetArea, m_flashDock);
    splitDockWidget(m_canDock, m_diagDock, Qt::Vertical);
    splitDockWidget(m_diagDock, m_flashDock, Qt::Vertical);
    resizeDocks(QList<QDockWidget *>() << m_deviceDock << m_canDock, QList<int>() << 1 << 1, Qt::Horizontal);
    resizeDocks(QList<QDockWidget *>() << m_canDock << m_diagDock << m_flashDock, QList<int>() << 1 << 1 << 1, Qt::Vertical);

    m_deviceDock->hide();
    m_canDock->hide();
    m_diagDock->hide();
    m_flashDock->hide();
}

// 应用主窗口与工具栏样式。
void MainWindow::applyStyle()
{
    setStyleSheet(QStringLiteral(
        "QMainWindow { background:#E7EEF6; }"
        "QLabel#welcomeLabel {"
        "  color:#0F172A;"
        "  font-size:36px;"
        "  font-weight:700;"
        "}"
        "QLabel#welcomeSubTitleLabel {"
        "  color:#0B84C8;"
        "  font-size:19px;"
        "  font-weight:600;"
        "  letter-spacing:1px;"
        "  padding-top:4px;"
        "}"
        "QLabel#welcomeHintLabel {"
        "  color:#475569;"
        "  font-size:16px;"
        "  padding-top:10px;"
        "}"
        "QToolBar {"
        "  background:qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #B7CADF, stop:0.5 #9CB7D2, stop:1 #89A9C8);"
        "  spacing:6px;"
        "  padding:10px 12px;"
        "  border:none;"
        "  border-bottom:1px solid #7D9CBC;"
        "}"
        "QToolBar::separator {"
        "  width:1px;"
        "  margin:8px 10px;"
        "  background:#7D97B4;"
        "}"
        "QLabel#toolbarGroupLabel {"
        "  color:#334155;"
        "  font-size:16px;"
        "  font-weight:700;"
        "  padding:0 6px;"
        "}"
        "QLabel#appBrandTitle {"
        "  color:#0F172A;"
        "  font-size:25px;"
        "  font-weight:700;"
        "  padding-right:2px;"
        "}"
        "QLabel#appBrandSubTitle {"
        "  color:#334155;"
        "  font-size:15px;"
        "  font-weight:500;"
        "}"
        "QLabel#appLogoLabel {"
        "  min-width:58px;"
        "  min-height:58px;"
        "}"
        "QToolButton {"
        "  color:#10243A;"
        "  background:#F3F8FD;"
        "  border:1px solid #9EB7D2;"
        "  font-size:15px;"
        "  font-weight:600;"
        "  padding:6px 10px 4px 10px;"
        "  border-radius:6px;"
        "  min-width:112px;"
        "  min-height:76px;"
        "}"
        "QToolButton::menu-indicator {"
        "  image:url(:/icons/dropdown_red.svg);"
        "  width:8px;"
        "  height:6px;"
        "  subcontrol-position: right bottom;"
        "  right:6px;"
        "  bottom:6px;"
        "}"
        "QToolButton:hover { background:#E6F0FB; border-color:#6995C2; }"
        "QToolButton:pressed { background:#D8E8F7; }"
        "QToolButton:checked { background:#D8EEFF; border:2px solid #2E86D1; color:#0B1A2E; }"
        "QToolButton:disabled { color:#7C8EA3; background:#E8EEF4; border-color:#C5D0DC; }"
        "QDockWidget { border:1px solid #8FA8C3; background:#C9D8E8; }"
        "QDockWidget::title {"
        "  text-align:left;"
        "  background:qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #BDD1E4, stop:1 #A9C2DA);"
        "  color:#10243A;"
        "  font-weight:600;"
        "  padding:7px 9px;"
        "  border-bottom:1px solid #8FA8C3;"
        "}"
        "QDockWidget QWidget { background:#C9D8E8; color:#1E293B; }"
        "QDockWidget QGroupBox { background:#D9E5F1; border:1px solid #A9BED3; margin-top:8px; }"
        "QDockWidget QGroupBox::title { subcontrol-origin:margin; left:8px; padding:0 2px; color:#24384F; }"
        "QDockWidget QLineEdit, QDockWidget QComboBox, QDockWidget QSpinBox, QDockWidget QPlainTextEdit, QDockWidget QTableWidget, QDockWidget QTreeWidget {"
        "  background:#FDFEFF; border:1px solid #96AEC6; color:#0F172A;"
        "}"
        "QDockWidget QHeaderView::section { background:#C8D8E8; border:1px solid #9DB2C8; color:#24384F; padding:4px; }"
        "QDockWidget QPushButton { background:#F8FBFF; border:1px solid #96AEC6; border-radius:4px; padding:4px 10px; color:#1E293B; }"
        "QDockWidget QPushButton:hover { background:#E9F3FC; border-color:#5E88B3; }"
        "QDockWidget QPushButton:pressed { background:#D9E9F7; }"
        "QDockWidget::separator { background:#8FA8C3; }"
        "QStatusBar { background:#EEF4FA; color:#475569; border-top:1px solid #B7C7D8; }"));
}
