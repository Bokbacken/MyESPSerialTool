#pragma once

#include "config.h"
#include "esptool.h"
#include <QDialog>
#include <QProcess>
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

class EsptoolDialog : public QDialog {
    Q_OBJECT
public:
    using AcquirePort = std::function<bool(QString &)>;
    using ReleasePort = std::function<void()>;

    EsptoolDialog(const Profile &profile, AcquirePort acquirePort,
                  ReleasePort releasePort, QWidget *parent = nullptr);
    ~EsptoolDialog() override;
    QString configuredPath() const;
    void cancelCurrent();
    void cancelAndWait();
signals:
    void pathChanged(const QString &path);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    enum class Operation { Version, ChipAndFlash, PartitionTable, RawDump, PartitionDump };
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
    QComboBox *partitionsBox;
    QPlainTextEdit *output;
    QProgressBar *progress;
    QLabel *status;
    QPushButton *chipButton;
    QPushButton *partitionButton;
    QPushButton *rawButton;
    QPushButton *partitionDumpButton;
    QPushButton *cancelButton;
    QPushButton *versionButton;
    QPushButton *browseButton;
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

    void begin(Operation requested);
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
    void updateControls();
};
