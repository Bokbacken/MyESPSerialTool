#pragma once

#include "config.h"
#include "esptool.h"
#include "flashplan.h"
#include <QDialog>
#include <QProcess>
#include <QPointer>
#include <QVector>
#include <functional>
#include <memory>

class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTemporaryFile;
class FlashPlanDialog;

class EsptoolDialog : public QDialog {
    Q_OBJECT
public:
    using AcquirePort = std::function<bool(QString &)>;
    using ReleasePort = std::function<void()>;

    EsptoolDialog(const Profile &profile, AcquirePort acquirePort,
                  ReleasePort releasePort, bool confirmReset = true,
                  QWidget *parent = nullptr);
    ~EsptoolDialog() override;
    QString configuredPath() const;
    void settingsSaved(const QString &iniPath);
    void cancelCurrent();
    void cancelAndWait();
    void openFlashPlan();
signals:
    void saveSettingsRequested();
    void pathChanged(const QString &path);
    void operationStateChanged(const QString &status, int value, int maximum, bool running);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    enum class Operation { Version, ChipAndFlash, PartitionTable, OtaInfo, RawDump, PartitionDump, FlashWrite };
    struct Step {
        Esptool::Command command;
        QStringList extra;
    };

    Profile profile;
    AcquirePort acquirePort;
    ReleasePort releasePort;
    QLineEdit *pathEdit;
    QLineEdit *portEdit;
    QLineEdit *baudEdit;
    QLineEdit *tableOffset;
    QComboBox *partitionsBox;
    QPlainTextEdit *output;
    QProgressBar *progress;
    QLabel *status;
    QLabel *otaStatus;
    QPushButton *chipButton;
    QPushButton *partitionButton;
    QPushButton *rawButton;
    QPushButton *partitionDumpButton;
    QPushButton *cancelButton;
    QPushButton *versionButton;
    QPushButton *browseButton;
    QPushButton *otaButton;
    QPushButton *planButton;
    QProcess process;
    Operation operation = Operation::Version;
    Esptool::Version version;
    QVector<Esptool::Partition> partitions;
    QVector<Step> steps;
    std::unique_ptr<QTemporaryFile> tempFile;
    QString destination;
    QString rawAddress, rawSize;
    quint64 expectedBytes = 0;
    QByteArray stdoutBytes, stderrBytes;
    QByteArray progressTail;
    bool busy = false;
    bool probePhase = false;
    bool portLoaned = false;
    bool handlingResult = false;
    bool closeAfterFinish = false;
    bool readingOta = false;
    bool confirmReset;
    FlashPlan::Plan flashPlan;
    Esptool::Command currentCommand = Esptool::Command::ChipId;
    QString detectedChip;
    quint64 detectedFlashSize = 0;
    QString snapshotChip;
    quint64 snapshotCapacity = 0;
    int completedWrites = 0;
    int totalWrites = 0;
    QPointer<FlashPlanDialog> flashWindow;

    void begin(Operation requested);
    void startFlash(const FlashPlan::Plan &plan);
    bool authorizeFlash();
    void launch(const QStringList &arguments, bool probe);
    void readStdout();
    void readStderr();
    void updateProgress(const QByteArray &bytes);
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void processError(QProcess::ProcessError error);
    void prepareOperation();
    void runNextStep();
    void finishOperation(const QString &message = {});
    void failOperation(const QString &message);
    bool chooseRawDump();
    bool createTemporaryOutput();
    bool commitTemporaryOutput();
    bool parsePartitionOutput();
    bool startOtaRead();
    bool parseOtaOutput();
    void clearLayout();
    void updateControls();
    void publishOperationState();
    QWidget *operationParent();
};
