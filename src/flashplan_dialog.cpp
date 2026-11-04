#include "flashplan_dialog.h"
#include "flashplan.h"

#include <QComboBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextCursor>
#include <QVBoxLayout>
#include <memory>

namespace {

bool selectedNumber(const QComboBox *box, quint64 &value) {
    if (box->currentIndex() >= 0 && box->currentText() == box->itemText(box->currentIndex())
        && box->currentData().isValid()) {
        value = box->currentData().toULongLong();
        return true;
    }
    return FlashPlan::parseNumber(box->currentText(), value);
}

void setNumber(QComboBox *box, quint64 value) {
    const int index = box->findData(QVariant::fromValue(value));
    if (index >= 0) box->setCurrentIndex(index);
    else box->setEditText(QString("0x%1").arg(value, 0, 16));
}

quint64 normalBootAddress(const QString &chip) {
    return chip == "esp32" || chip == "esp32s2" ? 0x1000 : 0;
}

}

FlashPlanDialog::FlashPlanDialog(const QVector<Esptool::Partition> &partitions,
                                 QWidget *parent,
                                 const QString &detectedChip, quint64 detectedCapacity)
    : QDialog(parent) {
    setWindowTitle("Flash firmware");
    resize(960, 760);
    auto *layout = new QVBoxLayout(this);
    auto *notice = new QLabel(
        "The preview checks files and addresses only. The device chip and flash must be verified "
        "before writing. OTA boot selection is never changed automatically.");
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto *form = new QFormLayout;
    auto *chip = new QComboBox;
    chip->setObjectName("planChip");
    chip->addItem("Choose target chip", "");
    for (const auto &name : FlashPlan::supportedChips()) chip->addItem(name, name);
    if (!detectedChip.isEmpty()) chip->setCurrentIndex(chip->findData(detectedChip));
    auto *capacity = new QComboBox;
    capacity->setEditable(true);
    capacity->setInsertPolicy(QComboBox::NoInsert);
    capacity->setObjectName("planCapacity");
    capacity->addItem("Choose flash size (or enter bytes)", QVariant{});
    for (quint64 size = 0x100000; size <= 0x10000000; size *= 2)
        capacity->addItem(QString("%1 MiB (0x%2 bytes)").arg(size / 0x100000)
            .arg(size, 0, 16), QVariant::fromValue(size));
    if (detectedCapacity) setNumber(capacity, detectedCapacity);
    form->addRow("Target chip", chip);
    form->addRow("Flash size", capacity);
    auto *source = new QLabel(QString(
        "Defaults from the last Chip + flash info read in this diagnostics window: chip %1; flash %2. "
        "These are read snapshots, not proof of the current device. Both are checked again before writing.")
        .arg(detectedChip.isEmpty() ? "not read" : detectedChip,
             detectedCapacity ? QString("%1 bytes").arg(detectedCapacity) : "not read"));
    source->setObjectName("planDefaultsSource");
    source->setWordWrap(true);
    form->addRow(source);
    auto *appTarget = new QComboBox;
    appTarget->setObjectName("planAppTarget");
    appTarget->addItem("Custom address (read layout for partition choices)", -1);
    int ota0Count = 0, ota1Count = 0;
    for (int i = 0; i < partitions.size(); ++i) {
        const auto &partition = partitions[i];
        if (partition.type != 0) continue;
        if (partition.subtype == 0x10) ++ota0Count;
        if (partition.subtype == 0x11) ++ota1Count;
        appTarget->addItem(QString("%1 / %2: start 0x%3, %4 bytes (runtime state unknown)")
            .arg(partition.label, Esptool::partitionTypeName(partition.type, partition.subtype))
            .arg(partition.offset, 0, 16).arg(partition.size), i);
    }
    if (ota0Count == 1 && ota1Count == 1)
        appTarget->addItem("Write the same app BIN to OTA0 and OTA1", -2);
    form->addRow("App partition from read snapshot", appTarget);
    layout->addLayout(form);

    auto *table = new QTableWidget(0, 4);
    table->setObjectName("planInputs");
    table->setHorizontalHeaderLabels({"File (explicit)", "Role (explicit)", "Flash address",
                                      "Merged image header offset in file"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(table, 1);
    auto *buttons = new QHBoxLayout;
    auto *manifest = new QPushButton("Load flasher_args.json...");
    auto *add = new QPushButton("Add BIN...");
    auto *merged = new QPushButton("Add merged BIN...");
    merged->setObjectName("addMergedFile");
    auto *remove = new QPushButton("Remove row");
    auto *preview = new QPushButton("Inspect plan (no device access)");
    auto *recheck = new QPushButton("Recheck file fingerprints");
    auto *write = new QPushButton("Verify device and flash…");
    manifest->setObjectName("loadFlashManifest");
    add->setObjectName("addFlashFile");
    preview->setObjectName("inspectFlashPlan");
    recheck->setObjectName("recheckFlashPlan");
    write->setObjectName("writeFlashPlan");
    recheck->setEnabled(false);
    write->setEnabled(false);
    for (auto *button : {manifest, merged, add, remove}) buttons->addWidget(button);
    layout->addLayout(buttons);
    auto *actions = new QHBoxLayout;
    for (auto *button : {preview, recheck, write}) actions->addWidget(button);
    layout->addLayout(actions);
    auto *addressHelp = new QLabel(
        "Dropdowns offer common addresses, not guaranteed addresses for your package. "
        "Selecting an app partition fills its start address. Merged files usually start at 0x0; "
        "the image header inside the file is commonly at 0x1000 for ESP32/S2, or 0x0 for other chips. "
        "Check the package instructions; custom numeric addresses remain available.");
    addressHelp->setWordWrap(true);
    layout->addWidget(addressHelp);
    auto *report = new QPlainTextEdit;
    report->setObjectName("flashPlanReport");
    report->setReadOnly(true);
    report->setPlainText("No plan inspected. An app BIN is not a merged full-flash image. "
        "Partition-table changes require a complete, explicitly addressed package.");
    results = new QTabWidget;
    results->setObjectName("flashResults");
    results->addTab(report, "Plan preview");
    operationOutput = new QPlainTextEdit;
    operationOutput->setObjectName("flashOperationOutput");
    operationOutput->setReadOnly(true);
    results->addTab(operationOutput, "Live device log");
    layout->addWidget(results, 1);
    operationStatus = new QLabel("No device operation started.");
    operationStatus->setObjectName("flashOperationStatus");
    operationStatus->setTextFormat(Qt::PlainText);
    operationStatus->setWordWrap(true);
    layout->addWidget(operationStatus);
    auto *operationRow = new QHBoxLayout;
    operationProgress = new QProgressBar;
    operationProgress->setObjectName("flashOperationProgress");
    operationProgress->setRange(0, 100);
    operationProgress->setValue(0);
    operationRow->addWidget(operationProgress, 1);
    auto *cancel = new QPushButton("Cancel device operation");
    cancel->setObjectName("cancelFlashOperation");
    cancel->setEnabled(false);
    operationRow->addWidget(cancel);
    layout->addLayout(operationRow);
    connect(cancel, &QPushButton::clicked, this, &FlashPlanDialog::cancelRequested);
    connect(operationOutput, &QPlainTextEdit::textChanged, this, [this] {
        operationOutput->moveCursor(QTextCursor::End);
    });

    auto plan = std::make_shared<FlashPlan::Plan>();
    const QList<QWidget *> operationControls{
        chip, capacity, appTarget, table, manifest, merged, add, remove, preview};
    connect(this, &FlashPlanDialog::runningChanged, this,
        [plan, operationControls, cancel, recheck, write](bool running) {
            for (auto *control : operationControls) control->setEnabled(!running);
            cancel->setEnabled(running);
            recheck->setEnabled(!running && !plan->files.isEmpty());
            write->setEnabled(!running && !plan->files.isEmpty());
        });
    auto invalidate = [plan, report, recheck, write] {
        *plan = {};
        recheck->setEnabled(false);
        write->setEnabled(false);
        report->setPlainText("Inputs changed. Inspect the plan again before any write.");
    };
    auto append = [table, chip, appTarget, partitions, invalidate](const FlashPlan::Input &input) {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(input.path));
        auto *kind = new QComboBox;
        for (auto value : {FlashPlan::Kind::App, FlashPlan::Kind::Bootloader,
                          FlashPlan::Kind::PartitionTable, FlashPlan::Kind::Data,
                          FlashPlan::Kind::Merged})
            kind->addItem(FlashPlan::kindName(value), int(value));
        kind->setCurrentIndex(kind->findData(int(input.kind)));
        table->setCellWidget(row, 1, kind);
        auto *address = new QComboBox;
        auto *header = new QComboBox;
        for (auto *box : {address, header}) {
            box->setEditable(true);
            box->setInsertPolicy(QComboBox::NoInsert);
        }
        auto populateAddresses = [address, chip, partitions](FlashPlan::Kind role) {
            address->clear();
            if (role == FlashPlan::Kind::Merged)
                address->addItem("0x0 - full merged image", QVariant::fromValue(quint64(0)));
            else if (role == FlashPlan::Kind::Bootloader) {
                if (chip->currentData().toString().isEmpty()) {
                    address->addItem("0x0 - some chips", QVariant::fromValue(quint64(0)));
                    address->addItem("0x1000 - ESP32/S2", QVariant::fromValue(quint64(0x1000)));
                } else {
                    const auto boot = normalBootAddress(chip->currentData().toString());
                    address->addItem(QString("0x%1 - usual bootloader").arg(boot, 0, 16),
                                     QVariant::fromValue(boot));
                }
            } else if (role == FlashPlan::Kind::PartitionTable)
                address->addItem("0x8000 - usual partition table", QVariant::fromValue(quint64(0x8000)));
            else {
                for (const auto &partition : partitions) {
                    if ((role == FlashPlan::Kind::App) != (partition.type == 0)) continue;
                    address->addItem(QString("0x%1 - %2 (%3 bytes)").arg(partition.offset, 0, 16)
                        .arg(partition.label).arg(partition.size),
                        QVariant::fromValue(quint64(partition.offset)));
                }
                if (role == FlashPlan::Kind::App && address->count() == 0)
                    address->addItem("0x10000 - usual first app", QVariant::fromValue(quint64(0x10000)));
            }
        };
        populateAddresses(input.kind);
        for (quint64 offset : {quint64(0), quint64(0x1000), quint64(0x2000), quint64(0x10000)})
            header->addItem(QString("0x%1").arg(offset, 0, 16), QVariant::fromValue(offset));
        setNumber(address, input.address);
        setNumber(header, input.imageOffset);
        header->setEnabled(input.kind == FlashPlan::Kind::Merged);
        table->setCellWidget(row, 2, address);
        table->setCellWidget(row, 3, header);
        auto applyTarget = [kind, address, appTarget, partitions] {
            const int target = appTarget->currentData().toInt();
            const bool app = kind->currentData().toInt() == int(FlashPlan::Kind::App);
            address->setEnabled(!app || target == -1);
            if (app && target >= 0) setNumber(address, partitions[target].offset);
        };
        QObject::connect(appTarget, &QComboBox::currentIndexChanged, address, applyTarget);
        QObject::connect(kind, &QComboBox::currentIndexChanged, table,
            [kind, chip, address, header, populateAddresses, applyTarget, invalidate] {
                const auto role = FlashPlan::Kind(kind->currentData().toInt());
                quint64 previousAddress = 0;
                const bool hadAddress = selectedNumber(address, previousAddress);
                populateAddresses(role);
                header->setEnabled(role == FlashPlan::Kind::Merged);
                setNumber(header, role == FlashPlan::Kind::Merged
                    ? normalBootAddress(chip->currentData().toString()) : 0);
                if (role == FlashPlan::Kind::Merged) setNumber(address, 0);
                else if (role == FlashPlan::Kind::Bootloader)
                    setNumber(address, normalBootAddress(chip->currentData().toString()));
                else if (role == FlashPlan::Kind::PartitionTable) setNumber(address, 0x8000);
                else if (role == FlashPlan::Kind::App) {
                    if (address->count() > 0) address->setCurrentIndex(0);
                } else if (hadAddress) setNumber(address, previousAddress);
                applyTarget();
                invalidate();
            });
        for (auto *box : {address, header})
            QObject::connect(box, &QComboBox::currentTextChanged, table, invalidate);
        applyTarget();
        invalidate();
    };
    connect(table, &QTableWidget::cellChanged, this, invalidate);
    connect(chip, &QComboBox::currentIndexChanged, this, invalidate);
    connect(capacity, &QComboBox::currentTextChanged, this, invalidate);
    connect(appTarget, &QComboBox::currentIndexChanged, this, invalidate);
    connect(add, &QPushButton::clicked, this, [this, append] {
        const QString path = QFileDialog::getOpenFileName(this, "Select a binary file", {},
            "Binary files (*.bin);;All files (*)");
        if (!path.isEmpty()) append({path, 0, FlashPlan::Kind::Data, 0});
    });
    connect(merged, &QPushButton::clicked, this, [this, chip, append] {
        const QString path = QFileDialog::getOpenFileName(this, "Select a merged full-flash binary", {},
            "Binary files (*.bin);;All files (*)");
        if (!path.isEmpty()) append({path, 0, FlashPlan::Kind::Merged,
                                    normalBootAddress(chip->currentData().toString())});
    });
    connect(remove, &QPushButton::clicked, this, [table, invalidate] {
        if (table->currentRow() >= 0) {
            table->removeRow(table->currentRow());
            invalidate();
        }
    });
    connect(manifest, &QPushButton::clicked, this, [this, table, chip, report, append, invalidate] {
        const QString path = QFileDialog::getOpenFileName(this, "Choose explicit ESP-IDF manifest", {},
            "JSON files (*.json);;All files (*)");
        if (path.isEmpty()) return;
        invalidate();
        QVector<FlashPlan::Input> inputs;
        QString error;
        if (!FlashPlan::loadManifest(path, chip->currentData().toString(), inputs, error)) {
            report->setPlainText("MANIFEST REJECTED: " + error);
            return;
        }
        table->setRowCount(0);
        for (const auto &input : inputs) append(input);
    });
    connect(preview, &QPushButton::clicked, this,
        [table, chip, capacity, appTarget, partitions, plan, report, recheck, write] {
            *plan = {};
            recheck->setEnabled(false);
            write->setEnabled(false);
            QVector<FlashPlan::Input> inputs;
            quint64 flashSize = 0;
            if (!selectedNumber(capacity, flashSize)) {
                report->setPlainText("PLAN REJECTED: enter flash capacity in decimal or 0x hexadecimal bytes.");
                return;
            }
            for (int row = 0; row < table->rowCount(); ++row) {
                FlashPlan::Input input;
                input.path = table->item(row, 0)->text();
                auto *kind = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
                input.kind = FlashPlan::Kind(kind->currentData().toInt());
                if (!selectedNumber(qobject_cast<QComboBox *>(table->cellWidget(row, 2)), input.address)
                    || !selectedNumber(qobject_cast<QComboBox *>(table->cellWidget(row, 3)), input.imageOffset)) {
                    report->setPlainText("PLAN REJECTED: invalid address/header offset.");
                    return;
                }
                const int target = appTarget->currentData().toInt();
                if (input.kind == FlashPlan::Kind::App && target == -2) continue;
                if (input.kind == FlashPlan::Kind::App && target >= 0) {
                    if (input.address != partitions[target].offset) {
                        report->setPlainText("PLAN REJECTED: app address differs from selected partition. "
                            "Enter its shown start address explicitly.");
                        return;
                    }
                }
                inputs << input;
            }
            if (appTarget->currentData().toInt() == -2) {
                QVector<Esptool::Partition> ota;
                for (const auto &partition : partitions)
                    if (partition.type == 0 && (partition.subtype == 0x10 || partition.subtype == 0x11))
                        ota << partition;
                if (ota.size() != 2 || ota[0].subtype == ota[1].subtype) {
                    report->setPlainText("PLAN REJECTED: the loaded snapshot must contain exactly OTA0 and OTA1.");
                    return;
                }
                const auto appRows = [&] {
                    QVector<FlashPlan::Input> apps;
                    for (int row = 0; row < table->rowCount(); ++row) {
                        auto *kind = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
                        if (kind && kind->currentData().toInt() == int(FlashPlan::Kind::App)) {
                            FlashPlan::Input app;
                            app.path = table->item(row, 0)->text();
                            app.kind = FlashPlan::Kind::App;
                            apps << app;
                        }
                    }
                    return apps;
                }();
                if (appRows.size() != 1) {
                    report->setPlainText("PLAN REJECTED: choose exactly one App BIN to map to both OTA slots.");
                    return;
                }
                for (const auto &partition : ota) {
                    auto app = appRows.first();
                    app.address = partition.offset;
                    inputs << app;
                }
            }
            QString error;
            if (!FlashPlan::inspect(inputs, chip->currentData().toString(), flashSize, partitions, *plan, error)) {
                report->setPlainText("PLAN REJECTED: " + error);
                return;
            }
            report->setPlainText(plan->report);
            recheck->setEnabled(true);
            write->setEnabled(true);
        });
    connect(recheck, &QPushButton::clicked, this, [plan, report, recheck, write] {
        QString error;
        if (!FlashPlan::revalidate(*plan, error)) {
            report->setPlainText("PLAN INVALIDATED: " + error);
            *plan = {};
            recheck->setEnabled(false);
            write->setEnabled(false);
        } else {
            report->setPlainText(plan->report + "\n\nFile fingerprints unchanged at this check.");
        }
    });
    connect(write, &QPushButton::clicked, this, [this, plan, report, recheck, write] {
        QString error;
        if (!FlashPlan::revalidate(*plan, error)) {
            report->setPlainText("PLAN INVALIDATED: " + error);
            *plan = {};
            recheck->setEnabled(false);
            write->setEnabled(false);
            return;
        }
        emit flashRequested(*plan);
    });
}

void FlashPlanDialog::setOperationOutput(QTextDocument *document) {
    operationOutput->setDocument(document);
    operationOutput->moveCursor(QTextCursor::End);
}

void FlashPlanDialog::setOperationState(const QString &status, int value, int maximum, bool running) {
    operationStatus->setText(status);
    operationProgress->setRange(0, maximum);
    operationProgress->setValue(value);
    if (running && !operationRunning) results->setCurrentWidget(operationOutput);
    if (running != operationRunning) {
        operationRunning = running;
        emit runningChanged(running);
    }
}

void FlashPlanDialog::reject() {
    if (operationRunning) emit cancelRequested();
    else QDialog::reject();
}

void FlashPlanDialog::closeEvent(QCloseEvent *event) {
    if (operationRunning) {
        emit cancelRequested();
        event->ignore();
    } else QDialog::closeEvent(event);
}
