#ifndef UDS_DIAGNOSTIC_WIDGET_H
#define UDS_DIAGNOSTIC_WIDGET_H

#include <QWidget>

class AppController;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class UdsFlashWorker;
struct UdsFlashStep;

// UDS 诊断流程面板：支持流程编排、导入导出、执行与日志查看。
class UdsDiagnosticWidget : public QWidget
{
    Q_OBJECT
public:
    explicit UdsDiagnosticWidget(AppController *controller, QWidget *parent = nullptr);
    ~UdsDiagnosticWidget() override;

private slots:
    void onStart();
    void onAbort();
    void onFlowAddRaw();
    void onFlowDelete();
    void onFlowMoveUp();
    void onFlowMoveDown();
    void onFlowLoadDefault();
    void onFlowImport();
    void onFlowExport();
    void onApplyToExecutor();

    void onFlashLog(const QString &line);
    void onFlashProgress(int pct);
    void onFlashOk(const QString &summary);
    void onFlashErr(const QString &reason);
    void onThreadFinished();
    void onConnectionChanged(bool connected);

private:
    void buildUi();
    void setUiBusy(bool busy);
    void addFlowRow(const QString &name,
                    const QString &type,
                    const QString &requestHex,
                    int timeoutMs,
                    int retries,
                    bool enabled,
                    const QString &expectedSidHex = QString());
    void loadDefaultFlow();
    bool buildFlowFromTable(QTableWidget *table, QList<UdsFlashStep> *steps, QString *err) const;
    void syncEditorToExecutor();
    void appendLog(const QString &line);
    bool parseHexU32(const QString &text, quint32 *out) const;
    QByteArray parseHexBytes(const QString &text) const;
    QString toHexSpaced(const QByteArray &bytes) const;
    void applyStyle();

    AppController *m_controller = nullptr;
    QLineEdit *m_txIdEdit = nullptr;
    QLineEdit *m_rxIdEdit = nullptr;
    QCheckBox *m_extCheck = nullptr;
    QSpinBox *m_timeoutSpin = nullptr;
    QSpinBox *m_loopCountSpin = nullptr;

    QListWidget *m_serviceList = nullptr;
    QTableWidget *m_flowTable = nullptr;
    QTableWidget *m_execFlowTable = nullptr;
    QLabel *m_execFlowState = nullptr;

    QPushButton *m_applyToExecutorBtn = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_abortBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;

    QTabWidget *m_tabs = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPlainTextEdit *m_logEdit = nullptr;
    UdsFlashWorker *m_workerThread = nullptr;
};

#endif // UDS_DIAGNOSTIC_WIDGET_H
