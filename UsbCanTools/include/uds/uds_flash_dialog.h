#ifndef UDS_FLASH_DIALOG_H
#define UDS_FLASH_DIALOG_H

#include <QDialog>

class CanWorker;
// 文件说明：UDS 刷写工作台界面，负责流程编辑、参数配置与执行控制。
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QComboBox;
class QSpinBox;
class QTableWidget;
class QCheckBox;
class QTreeWidget;
class QTabWidget;
class QTableWidgetItem;
class UdsFlashWorker;
struct UdsFlashStep;

class UdsFlashDialog : public QDialog
{
    Q_OBJECT
public:
    // 构造刷写工作台界面。
    explicit UdsFlashDialog(CanWorker *worker, QWidget *parent = nullptr);
    // 析构并安全回收刷写线程。
    ~UdsFlashDialog() override;
    // 动态设置底层 CAN worker（用于设备切换后同步）。
    void setWorker(CanWorker *worker);

private slots:
    // 选择固件文件。
    void onBrowse();
    // 启动刷写流程。
    void onStart();
    // 中止刷写流程。
    void onAbort();

    // 新增 RAW 步骤。
    void onFlowAddRaw();
    // 删除选中步骤。
    void onFlowDelete();
    // 步骤上移。
    void onFlowMoveUp();
    // 步骤下移。
    void onFlowMoveDown();
    // 装载默认流程模板。
    void onFlowLoadDefault();
    // 导出流程文件。
    void onFlowExport();
    // 导入流程文件。
    void onFlowImport();
    // 将编辑器流程应用到执行器。
    void onApplyToExecutor();

    // 追加刷写日志。
    void onFlashLog(const QString &line);
    // 更新进度条。
    void onFlashProgress(int pct);
    // 刷写成功处理。
    void onFlashOk(const QString &summary);
    // 刷写失败处理。
    void onFlashErr(const QString &reason);
    // 刷写线程结束处理。
    void onThreadFinished();
    // ISO-TP 发帧事件显示。
    void onFlashIsoTpTxFrame(const QString &stepName, int frameIndex1Based);
    // TransferData 块开始事件显示。
    void onTransferDataBlockStarted(int block1Based, int totalBlocks);

private:
    // 设置界面忙闲状态（控制控件可用性）。
    void setUiBusy(bool busy);
    // 向流程表新增一行步骤。
    int addFlowRow(const QString &name,
                   const QString &type,
                   const QString &requestHex,
                   int timeoutMs,
                   int retries,
                   bool enabled,
                   const QString &expectedSidHex = QString());
    // 装载默认流程到编辑器。
    void loadDefaultFlow();
    // 从表格构建可执行步骤列表。
    bool buildFlowFromTable(QTableWidget *table, QList<UdsFlashStep> *steps, QString *err) const;
    // 同步编辑器流程到执行器。
    void syncEditorToExecutor();
    // 应用工作台样式。
    void applyWorkbenchStyle();
    // 刷新 AUTO_36 行的文件选择控件与显示文本。
    void refreshTransferDataRowWidgets();
    // 刷新 DELAY 行的勾选与延时下拉控件。
    void refreshDelayRowWidgets();
    // 根据当前固件状态返回 36 行展示文本。
    QString transferDataDisplayText() const;
    // 根据块数初始化块进度视图。
    void resetTransferBlockProgress(int totalBlocks);
    // 计算单个 TransferData 块预计需要多少 ISO-TP 帧。
    int estimateTransferFramesPerBlock() const;

    CanWorker *m_worker = nullptr;     // 底层 CAN worker
    UdsFlashWorker *m_flash = nullptr; // 刷写线程

    QLineEdit *m_txId = nullptr;          // 请求 ID 输入
    QLineEdit *m_rxId = nullptr;          // 响应 ID 输入
    QCheckBox *m_ext = nullptr;           // 扩展帧开关
    QLineEdit *m_addr = nullptr;          // 下载地址输入
    QSpinBox *m_payload = nullptr;        // 每块最大载荷
    QSpinBox *m_timeout = nullptr;        // 默认超时
    QCheckBox *m_testerPresent = nullptr; // TesterPresent 开关

    QLabel *m_fileLabel = nullptr;            // 固件路径标签
    QPushButton *m_browse = nullptr;          // 浏览按钮
    QPushButton *m_start = nullptr;           // 启动按钮
    QPushButton *m_abort = nullptr;           // 中止按钮
    QPushButton *m_applyToExecutor = nullptr; // 应用流程按钮

    QTabWidget *m_tabs = nullptr;           // 页签容器
    QTableWidget *m_flowTable = nullptr;    // 编辑器流程表
    QTableWidget *m_execFlowTable = nullptr;// 执行器流程表
    QTreeWidget *m_serviceList = nullptr;   // 服务模板树（可展开/折叠）
    QLabel *m_execFlowState = nullptr;      // 执行器状态标签

    QProgressBar *m_progress = nullptr;   // 进度条
    QLabel *m_blockStatus = nullptr;      // 块进度状态
    QTableWidget *m_blockTable = nullptr; // 块进度明细表
    QPlainTextEdit *m_log = nullptr;      // 日志输出框

    QString m_filePath; // 当前固件路径
    int m_runtimeChunkPayload = 0; // 运行时实际块载荷
    int m_activeTransferBlock = 0; // 当前块号（1-based）
    int m_totalTransferBlocks = 0; // 总块数
    int m_currentBlockFrame = 0;   // 当前块已发送帧数
};

#endif
