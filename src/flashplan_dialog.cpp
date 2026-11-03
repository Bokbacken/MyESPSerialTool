#include "flashplan_dialog.h"
#include "flashplan.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <memory>

FlashPlanDialog::FlashPlanDialog(const QVector<Esptool::Partition> &partitions,
                                 FlashCallback flash, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Flash plan");
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
    auto *capacity = new QLineEdit;
    capacity->setObjectName("planCapacity");
    capacity->setPlaceholderText("Flash capacity in bytes, e.g. 0x800000 (8 MiB)");
    form->addRow("Explicit target chip", chip);
    form->addRow("Explicit flash capacity", capacity);
    auto *appTarget = new QComboBox;
    appTarget->setObjectName("planAppTarget");
    appTarget->addItem("Explicit address (no automatic inactive selection)", -1);
    for (int i = 0; i < partitions.size(); ++i) {
        const auto &partition = partitions[i];
        if (partition.type != 0) continue;
        appTarget->addItem(QString("%1 / %2: start 0x%3, %4 bytes (runtime state unknown)")
            .arg(partition.label, Esptool::partitionTypeName(partition.type, partition.subtype))
            .arg(partition.offset, 0, 16).arg(partition.size), i);
    }
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
    for (auto *button : {manifest, add, remove, preview, recheck, write}) buttons->addWidget(button);
    layout->addLayout(buttons);
    auto *report = new QPlainTextEdit;
    report->setObjectName("flashPlanReport");
    report->setReadOnly(true);
    report->setPlainText("No plan inspected. An app BIN is not a merged full-flash image. "
        "Partition-table changes require a complete, explicitly addressed package.");
    layout->addWidget(report, 1);

    auto plan = std::make_shared<FlashPlan::Plan>();
    auto invalidate = [plan, report, recheck, write] {
        *plan = {};
        recheck->setEnabled(false);
        write->setEnabled(false);
        report->setPlainText("Inputs changed. Inspect the plan again before any write.");
    };
    auto append = [table, invalidate](const FlashPlan::Input &input) {
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
        table->setItem(row, 2, new QTableWidgetItem(QString("0x%1").arg(input.address, 0, 16)));
        table->setItem(row, 3, new QTableWidgetItem(QString("0x%1").arg(input.imageOffset, 0, 16)));
        QObject::connect(kind, &QComboBox::currentIndexChanged, table, invalidate);
        invalidate();
    };
    connect(table, &QTableWidget::cellChanged, this, invalidate);
    connect(chip, &QComboBox::currentIndexChanged, this, invalidate);
    connect(capacity, &QLineEdit::textChanged, this, invalidate);
    connect(appTarget, &QComboBox::currentIndexChanged, this, invalidate);
    connect(add, &QPushButton::clicked, this, [this, append] {
        const QString path = QFileDialog::getOpenFileName(this, "Select a binary file", {},
            "Binary files (*.bin);;All files (*)");
        if (!path.isEmpty()) append({path, 0, FlashPlan::Kind::Data, 0});
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
            if (!FlashPlan::parseNumber(capacity->text(), flashSize)) {
                report->setPlainText("PLAN REJECTED: enter flash capacity in decimal or 0x hexadecimal bytes.");
                return;
            }
            for (int row = 0; row < table->rowCount(); ++row) {
                FlashPlan::Input input;
                input.path = table->item(row, 0)->text();
                auto *kind = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
                input.kind = FlashPlan::Kind(kind->currentData().toInt());
                if (!FlashPlan::parseNumber(table->item(row, 2)->text(), input.address)
                    || !FlashPlan::parseNumber(table->item(row, 3)->text(), input.imageOffset)) {
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
    connect(write, &QPushButton::clicked, this, [this, plan, report, recheck, write, flash] {
        QString error;
        if (!FlashPlan::revalidate(*plan, error)) {
            report->setPlainText("PLAN INVALIDATED: " + error);
            *plan = {};
            recheck->setEnabled(false);
            write->setEnabled(false);
            return;
        }
        if (flash) flash(*plan);
    });
}
