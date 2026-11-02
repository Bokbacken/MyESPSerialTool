#include "esptool_dialog.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {

bool parseUnsigned(const QString &text, quint64 &value) {
    QString input = text.trimmed();
    if (input.isEmpty() || input.startsWith('-') || input.startsWith('+')) return false;
    int base = 10;
    if (input.startsWith("0x", Qt::CaseInsensitive)) {
        base = 16;
        input.remove(0, 2);
    }
    if (input.isEmpty() || !QRegularExpression(base == 16 ? "^[0-9A-Fa-f]+$" : "^[0-9]+$")
        .match(input).hasMatch()) return false;
    bool ok = false;
    const quint64 parsed = input.toULongLong(&ok, base);
    if (!ok) return false;
    value = parsed;
    return true;
}

QString commandName(Esptool::Command command, int major) {
    if (major >= 5) {
        switch (command) {
        case Esptool::Command::ChipId: return "chip-id";
        case Esptool::Command::FlashId: return "flash-id";
        case Esptool::Command::ReadFlash: return "read-flash";
        }
    }
    switch (command) {
    case Esptool::Command::ChipId: return "chip_id";
    case Esptool::Command::FlashId: return "flash_id";
    case Esptool::Command::ReadFlash: return "read_flash";
    }
    return {};
}

}

EsptoolDialog::EsptoolDialog(const Profile &p, AcquirePort acquire,
                             ReleasePort release, QWidget *parent)
    : QDialog(parent), profile(p), acquirePort(std::move(acquire)),
      releasePort(std::move(release)), pathEdit(new QLineEdit(p.esptoolPath)),
      portEdit(new QLineEdit(p.port)), baudEdit(new QLineEdit(QString::number(p.baud))),
      partitionsBox(new QComboBox), output(new QPlainTextEdit), progress(new QProgressBar),
      status(new QLabel("No esptool operation started.")),
      chipButton(new QPushButton("Chip + flash info")),
      partitionButton(new QPushButton("Read partition table")),
      rawButton(new QPushButton("Read raw flash…")),
      partitionDumpButton(new QPushButton("Save selected partition…")),
      cancelButton(new QPushButton("Cancel operation")),
      versionButton(new QPushButton("Check version")),
      browseButton(new QPushButton("Browse…")) {
    setWindowTitle("ESP diagnostics — esptool");
    resize(820, 650);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    auto *pathRow = new QHBoxLayout;
    pathEdit->setObjectName("esptoolPath");
    pathRow->addWidget(pathEdit, 1);
    pathRow->addWidget(browseButton);
    form->addRow("Official esptool executable", pathRow);
    portEdit->setObjectName("esptoolPort");
    baudEdit->setObjectName("esptoolBaud");
    portEdit->setReadOnly(true);
    baudEdit->setReadOnly(true);
    form->addRow("Port (exclusive while running)", portEdit);
    form->addRow("Baud rate", baudEdit);
    layout->addLayout(form);

    auto *commands = new QHBoxLayout;
    versionButton->setObjectName("checkEsptoolVersion");
    chipButton->setObjectName("chipFlashInfo");
    partitionButton->setObjectName("readPartitionTable");
    rawButton->setObjectName("readRawFlash");
    partitionDumpButton->setObjectName("savePartition");
    cancelButton->setObjectName("cancelOperation");
    commands->addWidget(versionButton);
    commands->addWidget(chipButton);
    commands->addWidget(partitionButton);
    commands->addWidget(rawButton);
    commands->addStretch();
    layout->addLayout(commands);
    partitionsBox->setObjectName("esptoolPartitions");
    layout->addWidget(partitionsBox);
    auto *saveRow = new QHBoxLayout;
    saveRow->addWidget(partitionDumpButton);
    saveRow->addStretch();
    saveRow->addWidget(cancelButton);
    layout->addLayout(saveRow);
    status->setObjectName("esptoolStatus");
    layout->addWidget(status);
    progress->setObjectName("esptoolProgress");
    progress->setRange(0, 100);
    progress->setValue(0);
    layout->addWidget(progress);
    output->setObjectName("esptoolOutput");
    output->setReadOnly(true);
    layout->addWidget(output, 1);
    auto *warning = new QLabel(
        "Read-only diagnostics only: this interface has no flash erase or write commands. "
        "A flash read stores the returned bytes in the selected file.");
    warning->setWordWrap(true);
    layout->addWidget(warning);

    connect(browseButton, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, "Select official esptool executable",
            pathEdit->text(), "esptool executable (esptool.exe esptool);;All files (*)");
        if (!file.isEmpty()) pathEdit->setText(file);
    });
    connect(pathEdit, &QLineEdit::textChanged, this, [this](const QString &path) {
        profile.esptoolPath = path;
        emit pathChanged(path);
        updateControls();
    });
    connect(versionButton, &QPushButton::clicked, this, [this] { begin(Operation::Version); });
    connect(chipButton, &QPushButton::clicked, this, [this] { begin(Operation::ChipAndFlash); });
    connect(partitionButton, &QPushButton::clicked, this, [this] { begin(Operation::PartitionTable); });
    connect(rawButton, &QPushButton::clicked, this, [this] {
        if (chooseRawDump()) begin(Operation::RawDump);
    });
    connect(partitionDumpButton, &QPushButton::clicked, this, [this] {
        if (partitionsBox->currentIndex() < 0) return;
        const auto &partition = partitions[partitionsBox->currentIndex()];
        if (partition.size > 1073741824) {
            QMessageBox::warning(this, "Partition too large",
                "Partition dumps are limited to 1 GiB per operation.");
            return;
        }
        const QString suggested = partition.label.isEmpty() ? "partition.bin"
            : QFileInfo(partition.label + ".bin").fileName();
        destination = QFileDialog::getSaveFileName(this, "Save exact partition bytes", suggested,
            "Binary files (*.bin);;All files (*)");
        if (destination.isEmpty()) return;
        expectedBytes = partition.size;
        if (createTemporaryOutput()) begin(Operation::PartitionDump);
        else QMessageBox::warning(this, "Dump unavailable",
            "Could not create a temporary dump file in the selected destination folder.");
    });
    connect(cancelButton, &QPushButton::clicked, this, &EsptoolDialog::cancelCurrent);
    connect(&process, &QProcess::readyReadStandardOutput, this, &EsptoolDialog::readStdout);
    connect(&process, &QProcess::readyReadStandardError, this, &EsptoolDialog::readStderr);
    connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &EsptoolDialog::processFinished);
    connect(&process, &QProcess::errorOccurred, this, &EsptoolDialog::processError);
    updateControls();
}

EsptoolDialog::~EsptoolDialog() {
    cancelAndWait();
}

QString EsptoolDialog::configuredPath() const {
    return pathEdit->text().trimmed();
}

void EsptoolDialog::begin(Operation requested) {
    if (busy) return;
    const QString executable = configuredPath();
    const QFileInfo file(executable);
    if (executable.isEmpty() || !file.exists() || !file.isFile()) {
        QMessageBox::warning(this, "esptool not configured",
            "Choose the official esptool executable file before running diagnostics.");
        return;
    }
    if (requested != Operation::Version && (portEdit->text().isEmpty()
        || baudEdit->text().toInt() <= 0)) {
        QMessageBox::warning(this, "Port required", "Select and connect a serial port first.");
        return;
    }
    operation = requested;
    busy = true;
    status->setText("Checking esptool version…");
    updateControls();
    launch(Esptool::versionArguments(), true);
}

void EsptoolDialog::launch(const QStringList &args, bool probe) {
    probePhase = probe;
    handlingResult = false;
    stdoutBytes.clear();
    stderrBytes.clear();
    progressTail.clear();
    output->appendPlainText(QString("Command: %1 %2")
        .arg(QFileInfo(configuredPath()).fileName(), args.join(" ")));
    output->appendPlainText("stdout:");
    output->moveCursor(QTextCursor::End);
    process.setProgram(configuredPath());
    process.setArguments(args);
    progress->setRange(0, 0);
    process.start();
}

void EsptoolDialog::readStdout() {
    const QByteArray bytes = process.readAllStandardOutput();
    stdoutBytes += bytes;
    output->moveCursor(QTextCursor::End);
    output->insertPlainText(QString::fromUtf8(bytes));
    output->moveCursor(QTextCursor::End);
    updateProgress(bytes);
}

void EsptoolDialog::readStderr() {
    const QByteArray bytes = process.readAllStandardError();
    stderrBytes += bytes;
    output->moveCursor(QTextCursor::End);
    output->insertPlainText("\nstderr:\n");
    output->insertPlainText(QString::fromUtf8(bytes));
    output->moveCursor(QTextCursor::End);
    updateProgress(bytes);
}

void EsptoolDialog::updateProgress(const QByteArray &bytes) {
    const QByteArray combined = progressTail + bytes;
    progressTail = combined.right(16);
    auto matches = QRegularExpression("(\\d{1,3})\\s*%").globalMatch(QString::fromLatin1(combined));
    int lastPercent = -1;
    while (matches.hasNext()) {
        bool ok = false;
        const int percent = matches.next().captured(1).toInt(&ok);
        if (ok && percent >= 0 && percent <= 100) lastPercent = percent;
    }
    if (lastPercent >= 0) {
        progress->setRange(0, 100);
        progress->setValue(lastPercent);
    }
}

void EsptoolDialog::processFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (handlingResult) return;
    handlingResult = true;
    readStdout();
    readStderr();
    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
    output->appendPlainText(QString("Exit status: %1 (code %2)")
        .arg(exitStatus == QProcess::NormalExit ? "normal" : "crashed").arg(exitCode));

    if (probePhase) {
        if (!success) {
            failOperation("esptool version check failed; no serial command was started.");
            return;
        }
        QString error;
        if (!Esptool::parseVersion(QString::fromUtf8(stdoutBytes + '\n' + stderrBytes), version, error)) {
            failOperation(error);
            return;
        }
        if (operation == Operation::Version) {
            finishOperation("Detected " + version.text + ".");
            return;
        }
        prepareOperation();
        return;
    }

    if (!success) {
        failOperation(QString("esptool command failed (exit code %1).").arg(exitCode));
        return;
    }
    if (operation == Operation::PartitionTable && steps.isEmpty()) {
        if (!parsePartitionOutput()) {
            failOperation("Partition-table read completed, but parsing failed.");
            return;
        }
        finishOperation("Partition table read and parsed. No flash contents were modified.");
        return;
    }
    if ((operation == Operation::RawDump || operation == Operation::PartitionDump)
        && steps.isEmpty()) {
        if (!commitTemporaryOutput()) {
            failOperation("Raw data was not committed to the destination file.");
            return;
        }
        finishOperation(QString("Saved %1 exact bytes to %2.").arg(expectedBytes).arg(destination));
        return;
    }
    runNextStep();
}

void EsptoolDialog::processError(QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart || handlingResult || !busy) return;
    handlingResult = true;
    failOperation("Could not start esptool: " + process.errorString());
}

void EsptoolDialog::prepareOperation() {
    QString error;
    if (!acquirePort || !acquirePort(error)) {
        failOperation(error.isEmpty() ? "Could not acquire the serial port for esptool." : error);
        return;
    }
    portLoaned = true;
    steps.clear();
    if (operation == Operation::ChipAndFlash) {
        steps << Step{Esptool::Command::ChipId, {}}
              << Step{Esptool::Command::FlashId, {}};
    } else if (operation == Operation::PartitionTable) {
        destination.clear();
        expectedBytes = 0xC00;
        if (!createTemporaryOutput()) {
            failOperation("Could not prepare a temporary file for the partition table.");
            return;
        }
        steps << Step{Esptool::Command::ReadFlash,
            {"0x8000", "0xC00", tempFile->fileName()}};
    } else if (operation == Operation::RawDump) {
        quint64 address = 0, size = 0;
        // Values are validated before the operation starts.
        if (!parseUnsigned(rawAddress, address) || !parseUnsigned(rawSize, size)) {
            failOperation("Invalid raw-read address or size.");
            return;
        }
        steps << Step{Esptool::Command::ReadFlash,
            {QString("0x%1").arg(address, 0, 16), QString("0x%1").arg(size, 0, 16),
             tempFile->fileName()}};
    } else if (operation == Operation::PartitionDump) {
        const auto &partition = partitions[partitionsBox->currentIndex()];
        steps << Step{Esptool::Command::ReadFlash,
            {QString("0x%1").arg(partition.offset, 0, 16),
             QString("0x%1").arg(partition.size, 0, 16), tempFile->fileName()}};
    }
    runNextStep();
}

void EsptoolDialog::runNextStep() {
    if (steps.isEmpty()) {
        finishOperation();
        return;
    }
    const Step step = steps.takeFirst();
    const QStringList args = Esptool::arguments(version, portEdit->text(),
        baudEdit->text().toInt(), step.command, step.extra);
    status->setText(QString("Running %1…").arg(commandName(step.command, version.major)));
    launch(args, false);
}

void EsptoolDialog::finishOperation(const QString &message) {
    if (portLoaned) {
        portLoaned = false;
        if (releasePort) releasePort();
    }
    busy = false;
    progress->setRange(0, 100);
    progress->setValue(0);
    status->setText(message.isEmpty() ? "Operation finished." : message);
    tempFile.reset();
    updateControls();
    if (closeAfterFinish) {
        closeAfterFinish = false;
        QDialog::accept();
    }
}

void EsptoolDialog::failOperation(const QString &message) {
    output->appendPlainText(message);
    finishOperation(message);
}

bool EsptoolDialog::chooseRawDump() {
    bool ok = false;
    const QString addressText = QInputDialog::getText(this, "Raw flash read",
        "Flash address (decimal or 0x-prefixed hexadecimal):", QLineEdit::Normal,
        "0x0", &ok);
    if (!ok) return false;
    const QString sizeText = QInputDialog::getText(this, "Raw flash read",
        "Byte count (1..1073741824):", QLineEdit::Normal, "0x1000", &ok);
    if (!ok) return false;
    quint64 address = 0, size = 0;
    if (!parseUnsigned(addressText, address) || !parseUnsigned(sizeText, size)
        || size == 0 || size > 1073741824 || address > 0xffffffffULL
        || address + size > 0x100000000ULL) {
        QMessageBox::warning(this, "Invalid flash range",
            "Enter a valid address and a size from 1 byte to 1 GiB without overflowing the 32-bit address space.");
        return false;
    }
    const QString path = QFileDialog::getSaveFileName(this, "Save exact raw flash bytes", "flash-dump.bin",
        "Binary files (*.bin);;All files (*)");
    if (path.isEmpty()) return false;
    destination = path;
    expectedBytes = size;
    rawAddress = addressText;
    rawSize = sizeText;
    if (!createTemporaryOutput()) {
        QMessageBox::warning(this, "Dump unavailable",
            "Could not create a temporary dump file in the selected destination folder.");
        return false;
    }
    return true;
}

bool EsptoolDialog::createTemporaryOutput() {
    if (destination.isEmpty()) {
        tempFile = std::make_unique<QTemporaryFile>();
    } else {
        const QFileInfo destinationInfo(destination);
        const QString pattern = destinationInfo.absolutePath() + "/."
            + destinationInfo.fileName() + ".XXXXXX";
        tempFile = std::make_unique<QTemporaryFile>(pattern);
    }
    if (!tempFile->open()) {
        tempFile.reset();
        return false;
    }
    tempFile->close();
    return true;
}

bool EsptoolDialog::commitTemporaryOutput() {
    if (!tempFile || destination.isEmpty()) return false;
    QFile input(tempFile->fileName());
    if (!input.open(QIODevice::ReadOnly) || quint64(input.size()) != expectedBytes) return false;
    QSaveFile outputFile(destination);
    outputFile.setDirectWriteFallback(false);
    if (!outputFile.open(QIODevice::WriteOnly)) return false;
    while (!input.atEnd()) {
        const QByteArray bytes = input.read(1024 * 1024);
        if (bytes.isEmpty() && input.error() != QFileDevice::NoError) {
            outputFile.cancelWriting();
            return false;
        }
        if (outputFile.write(bytes) != bytes.size()) {
            outputFile.cancelWriting();
            return false;
        }
    }
    if (quint64(outputFile.pos()) != expectedBytes || !outputFile.commit()) return false;
    return true;
}

bool EsptoolDialog::parsePartitionOutput() {
    if (!tempFile) return false;
    QFile file(tempFile->fileName());
    if (!file.open(QIODevice::ReadOnly)) return false;
    QString error;
    if (!Esptool::parsePartitionTable(file.readAll(), partitions, error)) {
        output->appendPlainText(error);
        return false;
    }
    partitionsBox->clear();
    for (const auto &partition : partitions) {
        const QString text = QString("%1 — %2, offset 0x%3, size 0x%4 (%5 bytes)")
            .arg(partition.label.isEmpty() ? "(unnamed)" : partition.label,
                 Esptool::partitionTypeName(partition.type, partition.subtype))
            .arg(partition.offset, 0, 16).arg(partition.size, 0, 16).arg(partition.size);
        partitionsBox->addItem(text);
    }
    updateControls();
    return true;
}

void EsptoolDialog::cancelCurrent() {
    if (process.state() == QProcess::NotRunning) return;
    status->setText("Cancelling esptool operation…");
    process.kill();
}

void EsptoolDialog::cancelAndWait() {
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(-1);
    }
    if (busy && !handlingResult) finishOperation("Operation cancelled.");
    else if (portLoaned) finishOperation("Operation cancelled.");
}

void EsptoolDialog::closeEvent(QCloseEvent *event) {
    if (process.state() != QProcess::NotRunning) {
        closeAfterFinish = true;
        cancelCurrent();
        event->ignore();
        return;
    }
    if (portLoaned) {
        portLoaned = false;
        if (releasePort) releasePort();
    }
    event->accept();
}

void EsptoolDialog::updateControls() {
    const bool idle = !busy;
    const bool configured = !configuredPath().isEmpty();
    pathEdit->setEnabled(idle);
    browseButton->setEnabled(idle);
    versionButton->setEnabled(idle && configured);
    for (auto *button : {chipButton, partitionButton, rawButton})
        button->setEnabled(idle && configured && !portEdit->text().isEmpty());
    partitionDumpButton->setEnabled(idle && configured && !portEdit->text().isEmpty()
                                    && !partitions.isEmpty());
    cancelButton->setEnabled(busy);
    partitionsBox->setEnabled(idle && !partitions.isEmpty());
}
