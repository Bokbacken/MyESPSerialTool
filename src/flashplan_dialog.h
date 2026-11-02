#pragma once

#include "esptool.h"
#include <QDialog>

class FlashPlanDialog : public QDialog {
    Q_OBJECT
public:
    explicit FlashPlanDialog(const QVector<Esptool::Partition> &partitions, QWidget *parent = nullptr);
};
