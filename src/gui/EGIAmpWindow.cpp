#include "EGIAmpWindow.h"
#include "ui_EGIAmpWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QVariantList>

namespace {
/// Derive amplifier ID from the server address: .52 → 1, everything else → 0.
int amplifierIdFromAddress(const std::string& address) {
    if (address.size() >= 3 && address.compare(address.size() - 3, 3, ".52") == 0)
        return 1;
    return 0;
}
} // namespace

EGIAmpWindow::EGIAmpWindow(QWidget* parent, const std::string& configFile)
    : QMainWindow(parent)
    , ui(new Ui::EGIAmpWindow)
    , client_(std::make_unique<egiamp::EGIAmpClient>())
{
    ui->setupUi(this);

    // Populate sample rate dropdown before loading config (which selects an item)
    populateSampleRateCombo();

    // Load initial config
    loadConfig(configFile);

    // Sample rate dropdown
    connect(ui->sampleRateComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EGIAmpWindow::onSampleRateChanged);
    // Sync checkbox state with the currently selected rate (connect was not yet
    // active when populateSampleRateCombo / loadConfig set the index)
    onSampleRateChanged(ui->sampleRateComboBox->currentIndex());

    // Menu connections
    connect(ui->actionQuit, &QAction::triggered, this, &EGIAmpWindow::close);
    connect(ui->actionLoad_Configuration, &QAction::triggered,
            this, &EGIAmpWindow::loadConfigDialog);
    connect(ui->actionSave_Configuration, &QAction::triggered,
            this, &EGIAmpWindow::saveConfigDialog);
    connect(ui->actionShutdown_Amp_Server, &QAction::triggered,
            this, &EGIAmpWindow::shutdownAmpServer);

    // Link button
    connect(ui->linkButton, &QPushButton::clicked, this, &EGIAmpWindow::linkAmpserver);

    // Impedance mode checkbox
    connect(ui->impedanceCheckBox, &QCheckBox::toggled, this, &EGIAmpWindow::toggleImpedanceMode);

    // Signal connections for cross-thread communication
    connect(this, &EGIAmpWindow::appendStatusMessage,
            ui->statusBox, &QPlainTextEdit::appendPlainText,
            Qt::QueuedConnection);
    connect(this, &EGIAmpWindow::sensorLayoutUpdated,
            ui->sensorLayout, &QLineEdit::setText,
            Qt::QueuedConnection);
    connect(this, &EGIAmpWindow::error, this, &EGIAmpWindow::displayError,
            Qt::QueuedConnection);
    connect(this, &EGIAmpWindow::enableUI, this, &EGIAmpWindow::unlockUI,
            Qt::QueuedConnection);
    connect(this, &EGIAmpWindow::disableUI, this, &EGIAmpWindow::lockUI,
            Qt::QueuedConnection);

    // Field enable/disable connections
    connect(this, &EGIAmpWindow::fieldsEnabled, ui->sampleRateComboBox, &QComboBox::setEnabled);
    connect(this, &EGIAmpWindow::fieldsEnabled, ui->serverAddress, &QLineEdit::setEnabled);
    connect(this, &EGIAmpWindow::fieldsEnabled, ui->commandPort, &QSpinBox::setEnabled);
    connect(this, &EGIAmpWindow::fieldsEnabled, ui->notificationPort, &QSpinBox::setEnabled);
    connect(this, &EGIAmpWindow::fieldsEnabled, ui->dataPort, &QSpinBox::setEnabled);
    connect(this, &EGIAmpWindow::setLinkButtonText, ui->linkButton, &QPushButton::setText);

    // Set up client callbacks
    client_->setStatusCallback([this](const std::string& msg) {
        emit appendStatusMessage(QString::fromStdString(msg));
    });

    client_->setErrorCallback([this](const std::string& msg) {
        emit error(QString::fromStdString(msg));
    });

    client_->setChannelCountCallback([this](int count) {
        emit channelCountUpdated(count);
    });

    client_->setSensorCallback([this](egiamp::NetCode code) {
        emit sensorLayoutUpdated(QString::fromUtf8(egiamp::netCodeName(code)));
    });
}

EGIAmpWindow::~EGIAmpWindow() {
    if (client_->isStreaming()) {
        client_->stopStreaming();
    }
    if (client_->isConnected()) {
        client_->disconnect();
    }
    delete ui;
}

void EGIAmpWindow::loadConfigDialog() {
    QString sel = QFileDialog::getOpenFileName(
        this, "Load Configuration File", "", "Configuration Files (*.cfg)");
    if (!sel.isEmpty()) {
        loadConfig(sel.toStdString());
    }
}

void EGIAmpWindow::saveConfigDialog() {
    QString sel = QFileDialog::getSaveFileName(
        this, "Save Configuration File", "", "Configuration Files (*.cfg)");
    if (!sel.isEmpty()) {
        saveConfig(sel.toStdString());
    }
}

void EGIAmpWindow::loadConfig(const std::string& filename) {
    try {
        auto config = egiamp::AmpServerConfig::loadFromFile(filename);
        ui->serverAddress->setText(QString::fromStdString(config.serverAddress));
        ui->commandPort->setValue(config.commandPort);
        ui->notificationPort->setValue(config.notificationPort);
        ui->dataPort->setValue(config.dataPort);
        ui->sampleRateComboBox->setCurrentIndex(
            findSampleRateIndex(config.sampleRate, config.fastRecovery));
        ui->alignTimestampsCheckBox->setChecked(config.alignTimestamps);
    } catch (const egiamp::ConfigError& e) {
        QMessageBox::information(this, "Error",
            QString("Cannot read config file: %1").arg(e.what()), QMessageBox::Ok);
    }
}

void EGIAmpWindow::saveConfig(const std::string& filename) {
    try {
        auto config = getConfigFromUI();
        config.saveToFile(filename);
    } catch (const egiamp::ConfigError& e) {
        QMessageBox::critical(this, "Error",
            QString("Could not save config file: %1").arg(e.what()), QMessageBox::Ok);
    }
}

egiamp::AmpServerConfig EGIAmpWindow::getConfigFromUI() const {
    egiamp::AmpServerConfig config;
    config.serverAddress = ui->serverAddress->text().toStdString();
    config.commandPort = static_cast<uint16_t>(ui->commandPort->value());
    config.notificationPort = static_cast<uint16_t>(ui->notificationPort->value());
    config.dataPort = static_cast<uint16_t>(ui->dataPort->value());
    config.amplifierId = amplifierIdFromAddress(config.serverAddress);

    QVariantList rateData = ui->sampleRateComboBox->currentData().toList();
    config.sampleRate = rateData[0].toInt();
    config.fastRecovery = rateData[1].toBool();
    config.alignTimestamps = ui->alignTimestampsCheckBox->isChecked();

    return config;
}

void EGIAmpWindow::closeEvent(QCloseEvent* ev) {
    if (client_->isStreaming()) {
        ev->ignore();
    } else {
        ev->accept();
    }
}

void EGIAmpWindow::linkAmpserver() {
    if (client_->isStreaming()) {
        // Unlink
        client_->stopStreaming();
        client_->disconnect();
        ui->statusBar->clearMessage();
        ui->statusBar->setStyleSheet("");
        emit fieldsEnabled(true);
        emit setLinkButtonText("Link");
        emit sensorLayoutUpdated("");
    } else {
        // Link — disable button immediately to prevent double-click
        ui->linkButton->setEnabled(false);

        auto config = getConfigFromUI();
        client_->setConfig(config);

        emit appendStatusMessage("Connecting to AmpServer...");
        if (!client_->connect()) {
            ui->linkButton->setEnabled(true);
            emit error("Could not connect to AmpServer. Please check network settings "
                       "and ensure AmpServer is running.");
            return;
        }

        // Query amplifier state to detect if it's already running
        emit appendStatusMessage("Querying amplifier state...");
        client_->queryAmpState();
        if (client_->ampWasRunning()) {
            int detectedRate = client_->detectedSampleRate();
            if (detectedRate > 0) {
                // Can't detect native vs decimated from the stream, default to decimated
                // for ambiguous rates (500, 1000)
                ui->sampleRateComboBox->setCurrentIndex(
                    findSampleRateIndex(detectedRate, /*native=*/false));
                emit appendStatusMessage(QString("Amplifier already running at %1 Hz").arg(detectedRate));
            } else {
                emit appendStatusMessage("Amplifier already running (sample rate detection failed, using configured rate)");
            }
            // Update sensor layout from detected net code
            egiamp::NetCode netCode = client_->detectedNetCode();
            emit sensorLayoutUpdated(QString::fromUtf8(egiamp::netCodeName(netCode)));

            // Show persistent warning in status bar for ambiguous rates where we
            // cannot detect whether the FPGA anti-alias filter is active
            if (detectedRate == 500 || detectedRate == 1000) {
                ui->statusBar->setStyleSheet("QStatusBar { color: #b35900; }");
                ui->statusBar->showMessage(
                    QString("Warning: Amp started externally at %1 Hz — "
                            "filter mode unknown, timestamp alignment may be inaccurate")
                        .arg(detectedRate));
            }
        }

        if (!client_->startStreaming()) {
            ui->linkButton->setEnabled(true);
            emit error("Failed to start streaming");
            client_->disconnect();
            return;
        }

        emit disableUI();
    }
}

void EGIAmpWindow::displayError(QString description) {
    QMessageBox::critical(this, "Error", description, QMessageBox::Ok);
}

void EGIAmpWindow::unlockUI() {
    ui->linkButton->setEnabled(true);
    ui->statusBar->clearMessage();
    ui->statusBar->setStyleSheet("");
    emit setLinkButtonText("Link");
    emit fieldsEnabled(true);
    ui->actionShutdown_Amp_Server->setEnabled(true);
    ui->impedanceCheckBox->setEnabled(false);
    ui->impedanceCheckBox->setChecked(false);
    // Restore align-timestamps state based on current rate selection
    onSampleRateChanged(ui->sampleRateComboBox->currentIndex());
}

void EGIAmpWindow::lockUI() {
    ui->linkButton->setEnabled(true);
    emit setLinkButtonText("Unlink");
    emit fieldsEnabled(false);
    ui->actionShutdown_Amp_Server->setEnabled(false);
    ui->impedanceCheckBox->setEnabled(true);
    ui->alignTimestampsCheckBox->setEnabled(false);
}

void EGIAmpWindow::shutdownAmpServer() {
    // Show warning dialog
    QMessageBox::StandardButton reply = QMessageBox::warning(
        this,
        "Shutdown Amp Server",
        "WARNING: This will terminate the Amp Server process.\n\n"
        "All clients connected to the amplifier will be disconnected, "
        "which may disrupt ongoing recordings or other applications.\n\n"
        "Are you sure you want to proceed?",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (reply != QMessageBox::Yes) {
        return;
    }

    // Connect to send the shutdown command
    auto config = getConfigFromUI();
    client_->setConfig(config);
    if (!client_->connect()) {
        QMessageBox::critical(this, "Error",
            "Could not connect to AmpServer. Please check network settings.",
            QMessageBox::Ok);
        return;
    }

    // Send shutdown command
    emit appendStatusMessage("Sending shutdown command to Amp Server...");
    bool success = client_->shutdownAmpServer();

    // Disconnect (connection will be lost anyway after shutdown)
    client_->disconnect();

    if (success) {
        QMessageBox::information(this, "Amp Server Shutdown",
            "Shutdown command sent successfully.\n"
            "The Amp Server process should now be terminated.",
            QMessageBox::Ok);
    } else {
        QMessageBox::warning(this, "Amp Server Shutdown",
            "Failed to send shutdown command.\n"
            "The Amp Server may already be stopped or unreachable.",
            QMessageBox::Ok);
    }
}

void EGIAmpWindow::toggleImpedanceMode(bool enabled) {
    if (!client_->isStreaming()) {
        return;
    }

    if (enabled) {
        emit appendStatusMessage("Starting impedance mode...");
        if (!client_->startImpedanceMode()) {
            // Failed to start - uncheck the box
            ui->impedanceCheckBox->setChecked(false);
        }
    } else {
        emit appendStatusMessage("Stopping impedance mode...");
        client_->stopImpedanceMode();
    }
}

void EGIAmpWindow::populateSampleRateCombo() {
    // Each item stores {sampleRate, isNative} as user data
    struct RateEntry { const char* label; int rate; bool native; };
    static constexpr RateEntry entries[] = {
        {"250 Hz",                    250,  false},
        {"500 Hz",                    500,  false},
        {"500 Hz \xe2\x80\x94 Low Latency",  500,  true},
        {"1000 Hz",                   1000, false},
        {"1000 Hz \xe2\x80\x94 Low Latency", 1000, true},
        {"2000 Hz",                   2000, true},
        {"4000 Hz",                   4000, true},
        {"8000 Hz",                   8000, true},
    };

    ui->sampleRateComboBox->clear();
    for (const auto& e : entries) {
        ui->sampleRateComboBox->addItem(
            QString::fromUtf8(e.label),
            QVariantList{e.rate, e.native});
    }
    ui->sampleRateComboBox->setCurrentIndex(3); // "1000 Hz" default
}

void EGIAmpWindow::onSampleRateChanged(int index) {
    if (index < 0) return;
    QVariantList data = ui->sampleRateComboBox->itemData(index).toList();
    bool isNative = data[1].toBool();
    if (isNative) {
        ui->alignTimestampsCheckBox->setChecked(false);
        ui->alignTimestampsCheckBox->setEnabled(false);
    } else {
        ui->alignTimestampsCheckBox->setChecked(true);
        ui->alignTimestampsCheckBox->setEnabled(true);
    }
}

int EGIAmpWindow::findSampleRateIndex(int rate, bool native) const {
    for (int i = 0; i < ui->sampleRateComboBox->count(); ++i) {
        QVariantList data = ui->sampleRateComboBox->itemData(i).toList();
        if (data[0].toInt() == rate && data[1].toBool() == native) {
            return i;
        }
    }
    // Rates ≥ 2000 are always native, so a non-native lookup just means
    // we couldn't detect the mode — find the rate regardless of mode flag.
    for (int i = 0; i < ui->sampleRateComboBox->count(); ++i) {
        QVariantList data = ui->sampleRateComboBox->itemData(i).toList();
        if (data[0].toInt() == rate) {
            return i;
        }
    }
    return 3; // fallback to "1000 Hz"
}
