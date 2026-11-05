#pragma once

#include "flashplan.h"
#include "esptool.h"
#include <QDialog>

class QCloseEvent;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QTabWidget;
class QTextDocument;
class FlashPlanDialog : public QDialog {
    Q_OBJECT
public:
    explicit FlashPlanDialog(const QVector<Esptool::Partition> &partitions,
                             QWidget *parent = nullptr,
                             const QString &detectedChip = {}, quint64 detectedCapacity = 0);
    void setOperationOutput(QTextDocument *document);
    void setOperationState(const QString &status, int value, int maximum, bool running);
    void reject() override;
signals:
    void flashRequested(const FlashPlan::Plan &plan);
    void cancelRequested();
    void runningChanged(bool running);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    QPlainTextEdit *operationOutput;
    QLabel *operationStatus;
    QProgressBar *operationProgress;
    QTabWidget *results;
    bool operationRunning = false;
};
