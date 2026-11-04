#pragma once

#include "flashplan.h"
#include "esptool.h"
#include <QDialog>
#include <functional>

class FlashPlanDialog : public QDialog {
    Q_OBJECT
public:
    using FlashCallback = std::function<void(const FlashPlan::Plan &)>;
    explicit FlashPlanDialog(const QVector<Esptool::Partition> &partitions,
                             FlashCallback flash, QWidget *parent = nullptr,
                             const QString &detectedChip = {}, quint64 detectedCapacity = 0);
};
