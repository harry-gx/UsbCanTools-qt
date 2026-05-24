#ifndef UDS_DIAGNOSTIC_ZCAN_H
#define UDS_DIAGNOSTIC_ZCAN_H

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
    AppController *m_controller = nullptr;

    QComboBox *m_channelCombo = nullptr;
    QLineEdit *m_txIdEdit = nullptr;
    QLineEdit *m_funcAddrEdit = nullptr;
    QLineEdit *m_rxIdEdit = nullptr;
    QComboBox *m_addrModeCombo = nullptr;
    QCheckBox *m_autoFlowCheck = nullptr;
    QSpinBox *m_timeoutSpin = nullptr;

    QTreeWidget *m_serviceTree = nullptr;

    QLineEdit *m_requestEdit = nullptr;
    QLineEdit *m_expectedRespEdit = nullptr;
    QComboBox *m_suppressCombo = nullptr;
    QComboBox *m_checkRespCombo = nullptr;
    QPushButton *m_addToListBtn = nullptr;
    QPushButton *m_sendNowBtn = nullptr;

    QTableWidget *m_listTable = nullptr;
    QSpinBox *m_repeatSpin = nullptr;
    QSpinBox *m_intervalSpin = nullptr;
    QPushButton *m_listDeleteBtn = nullptr;
    QPushButton *m_listClearBtn = nullptr;
    QPushButton *m_listUpBtn = nullptr;
    QPushButton *m_listDownBtn = nullptr;
    QPushButton *m_listSendBtn = nullptr;

    QComboBox *m_logDisplayCombo = nullptr;
    QPushButton *m_logClearBtn = nullptr;
    QPlainTextEdit *m_logEdit = nullptr;

    QLabel *m_totalCountLabel = nullptr;
    QLabel *m_passCountLabel = nullptr;
    QLabel *m_failCountLabel = nullptr;
    QLabel *m_noRespCountLabel = nullptr;
    QPushButton *m_resetStatsBtn = nullptr;

    int m_totalCount = 0;
    int m_passCount = 0;
    int m_failCount = 0;
    int m_noRespCount = 0;
};

#endif // UDS_DIAGNOSTIC_ZCAN_H
