#include "EGIAmpWindow.h"
#include "ui_EGIAmpWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QVariantList>

#include <chrono>
#include <future>

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
    connect(ui->triggerButton, &QPushButton::clicked, this, &EGIAmpWindow::triggerBncOut);

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
    connect(this, &EGIAmpWindow::linkFinished, this, &EGIAmpWindow::onLinkFinished,
            Qt::QueuedConnection);
    connect(this, &EGIAmpWindow::ampDetectedRunning, this, &EGIAmpWindow::onAmpDetectedRunning,
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

    if (baseConfig_.remotePort) {
        try {
            remote_ = std::make_unique<egiamp::RemoteControlServer>(*this, baseConfig_.remotePort);
            ui->statusBox->appendPlainText(
                QString("Remote control on 127.0.0.1:%1").arg(baseConfig_.remotePort));
        } catch (const std::exception& e) {
            ui->statusBox->appendPlainText(
                QString("Cannot serve remote control on port %1: %2")
                    .arg(baseConfig_.remotePort).arg(e.what()));
        }
    }
}

EGIAmpWindow::~EGIAmpWindow() {
    closing_ = true;  // lets a remote command waiting on the UI thread give up
    remote_.reset();
    joinLinkThread();
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
        applyConfig(egiamp::AmpServerConfig::loadFromFile(filename));
    } catch (const egiamp::ConfigError& e) {
        QMessageBox::information(this, "Error",
            QString("Cannot read config file: %1").arg(e.what()), QMessageBox::Ok);
    }
}

void EGIAmpWindow::applyConfig(const egiamp::AmpServerConfig& config) {
    baseConfig_ = config;
    ui->serverAddress->setText(QString::fromStdString(config.serverAddress));
    ui->commandPort->setValue(config.commandPort);
    ui->notificationPort->setValue(config.notificationPort);
    ui->dataPort->setValue(config.dataPort);
    ui->sampleRateComboBox->setCurrentIndex(
        findSampleRateIndex(config.sampleRate, config.fastRecovery));
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
    egiamp::AmpServerConfig config = baseConfig_;
    // The GUI attaches to a running amp as-is and measures impedance on demand.
    config.forceSampleRate = false;
    config.impedance = false;
    config.serverAddress = ui->serverAddress->text().toStdString();
    config.commandPort = static_cast<uint16_t>(ui->commandPort->value());
    config.notificationPort = static_cast<uint16_t>(ui->notificationPort->value());
    config.dataPort = static_cast<uint16_t>(ui->dataPort->value());
    config.amplifierId = amplifierIdFromAddress(config.serverAddress);

    QVariantList rateData = ui->sampleRateComboBox->currentData().toList();
    config.sampleRate = rateData[0].toInt();
    config.fastRecovery = rateData[1].toBool();

    return config;
}

void EGIAmpWindow::closeEvent(QCloseEvent* ev) {
    if (client_->isStreaming()) {
        ev->ignore();
    } else {
        ev->accept();
    }
}

void EGIAmpWindow::joinLinkThread() {
    if (linkThread_ && linkThread_->joinable()) {
        linkThread_->join();
    }
    linkThread_.reset();
}

void EGIAmpWindow::linkAmpserver() {
    if (client_->isStreaming()) {
        // Unlink
        joinLinkThread();
        client_->stopStreaming();
        client_->disconnect();
        unlockUI();  // also re-enables Shutdown and disables impedance/trigger
        emit sensorLayoutUpdated("");
    } else {
        // Link — disable button immediately to prevent double-click
        ui->linkButton->setEnabled(false);

        auto config = getConfigFromUI();
        client_->setConfig(config);

        // Run the whole connect/query/start sequence off the UI thread: it
        // blocks on the network for seconds (NA500 cold start: 15+ s waiting
        // for power-on) and must not freeze the UI even if the AmpServer is
        // unresponsive. All UI updates flow back through queued signals.
        joinLinkThread();
        linkThread_ = std::make_unique<std::thread>([this]() {
            const QString err = runLink();
            emit linkFinished(err.isEmpty(), err);
        });
    }
}

QString EGIAmpWindow::runLink() {
    emit appendStatusMessage("Connecting to AmpServer...");
    if (!client_->connect()) {
        return "Could not connect to AmpServer. Please check network settings "
               "and ensure AmpServer is running.";
    }

    // Query amplifier state to detect if it's already running
    emit appendStatusMessage("Querying amplifier state...");
    client_->queryAmpState();
    if (client_->ampWasRunning()) {
        int detectedRate = client_->detectedSampleRate();
        if (detectedRate > 0) {
            emit appendStatusMessage(
                QString("Amplifier already running at %1 Hz").arg(detectedRate));
        } else {
            emit appendStatusMessage(
                "Amplifier already running (sample rate detection failed, "
                "using configured rate)");
        }
        emit ampDetectedRunning(detectedRate);
        emit sensorLayoutUpdated(
            QString::fromUtf8(egiamp::netCodeName(client_->detectedNetCode())));
    }

    if (!client_->startStreaming()) {
        client_->disconnect();
        return "Failed to start streaming";
    }
    return QString();
}

void EGIAmpWindow::onLinkFinished(bool ok, QString errorMessage) {
    joinLinkThread();
    if (ok) {
        lockUI();
    } else {
        ui->linkButton->setEnabled(true);
        displayError(errorMessage);
    }
}

void EGIAmpWindow::onAmpDetectedRunning(int detectedRate) {
    if (detectedRate > 0) {
        // Can't detect native vs decimated from the stream, default to decimated
        // for ambiguous rates
        ui->sampleRateComboBox->setCurrentIndex(
            findSampleRateIndex(detectedRate, /*native=*/false));
    }

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
    ui->triggerButton->setEnabled(false);
}

void EGIAmpWindow::lockUI() {
    ui->linkButton->setEnabled(true);
    emit setLinkButtonText("Unlink");
    emit fieldsEnabled(false);
    ui->actionShutdown_Amp_Server->setEnabled(false);
    ui->impedanceCheckBox->setEnabled(true);
    ui->triggerButton->setEnabled(
        client_->amplifierDetails().amplifierType == egiamp::AmplifierType::NA500);
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
    // Each item stores {sampleRate, isNative} as user data.
    // Union of NA400 and NA500 capabilities; the client validates the
    // selection against the actual amplifier model at link time
    // (supportsSampleRate in AmpServerProtocol.h) and refuses with a clear
    // error if unsupported. Unqualified labels select decimated mode where
    // the model offers it (NA500 at 2000 Hz; NA400 runs 2000 Hz native as
    // that is its only option).
    struct RateEntry { const char* label; int rate; bool native; };
    static constexpr RateEntry entries[] = {
        {"250 Hz",                                        250,   false},
        {"250 Hz \xe2\x80\x94 Low Latency (NA500 only)",  250,   true},
        {"500 Hz",                                        500,   false},
        {"500 Hz \xe2\x80\x94 Low Latency",               500,   true},
        {"1000 Hz",                                       1000,  false},
        {"1000 Hz \xe2\x80\x94 Low Latency",              1000,  true},
        {"2000 Hz",                                       2000,  false},
        {"2000 Hz \xe2\x80\x94 Low Latency",              2000,  true},
        {"4000 Hz",                                       4000,  true},
        {"8000 Hz",                                       8000,  true},
    };

    ui->sampleRateComboBox->clear();
    for (const auto& e : entries) {
        ui->sampleRateComboBox->addItem(
            QString::fromUtf8(e.label),
            QVariantList{e.rate, e.native});
    }
    ui->sampleRateComboBox->setCurrentIndex(defaultSampleRateIndex());
}

int EGIAmpWindow::defaultSampleRateIndex() const {
    // "1000 Hz" (decimated) is the default
    for (int i = 0; i < ui->sampleRateComboBox->count(); ++i) {
        QVariantList data = ui->sampleRateComboBox->itemData(i).toList();
        if (data[0].toInt() == 1000 && !data[1].toBool()) {
            return i;
        }
    }
    return 0;
}

int EGIAmpWindow::findSampleRateIndex(int rate, bool native) const {
    for (int i = 0; i < ui->sampleRateComboBox->count(); ++i) {
        QVariantList data = ui->sampleRateComboBox->itemData(i).toList();
        if (data[0].toInt() == rate && data[1].toBool() == native) {
            return i;
        }
    }
    // A non-native lookup for a rate that only has native entries just means
    // we couldn't detect the mode — find the rate regardless of mode flag.
    for (int i = 0; i < ui->sampleRateComboBox->count(); ++i) {
        QVariantList data = ui->sampleRateComboBox->itemData(i).toList();
        if (data[0].toInt() == rate) {
            return i;
        }
    }
    return defaultSampleRateIndex(); // fallback to "1000 Hz"
}

void EGIAmpWindow::triggerBncOut() {
    // Failures are reported through the client's error callback.
    if (client_->triggerBncOut(egiamp::EGIAmpClient::BNC_OUT_DEFAULT_PULSE_US)) {
        emit appendStatusMessage("BNC trigger sent.");
    }
}

// Runs f on the UI thread and returns its result; called from the remote
// control thread. Gives up (throws) once the window starts closing, so the
// destructor can join the remote control thread without deadlocking.
template <typename F>
auto EGIAmpWindow::onUiThread(F f) {
    auto done = std::make_shared<std::promise<decltype(f())>>();
    auto result = done->get_future();
    // `this` as context: Qt drops the call if the window is destroyed first.
    QMetaObject::invokeMethod(this, [done, f]() mutable { done->set_value(f()); },
                              Qt::QueuedConnection);
    while (result.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
        if (closing_) throw std::runtime_error("the application is closing");
    }
    return result.get();
}

egiamp::AmpServerConfig EGIAmpWindow::config() {
    return onUiThread([this] { return getConfigFromUI(); });
}

void EGIAmpWindow::setConfig(const egiamp::AmpServerConfig& config) {
    onUiThread([this, &config] { applyConfig(config); return true; });
}

std::string EGIAmpWindow::link() {
    const bool started = onUiThread([this] {
        if (!ui->linkButton->isEnabled() || client_->isStreaming()) return false;
        ui->linkButton->setEnabled(false);
        client_->setConfig(getConfigFromUI());
        return true;
    });
    if (!started) return "busy (already linking)";
    const QString err = runLink();
    emit linkFinished(err.isEmpty(), err);  // updates the window, like a button-started link
    return err.toStdString();
}

std::string EGIAmpWindow::unlink() {
    return onUiThread([this]() -> std::string {
        if (!ui->linkButton->isEnabled()) return "busy (linking)";
        if (!client_->isStreaming()) return "not linked";
        linkAmpserver();
        return "";
    });
}

std::string EGIAmpWindow::setImpedance(bool on) {
    return onUiThread([this, on]() -> std::string {
        if (!ui->impedanceCheckBox->isEnabled()) return "not linked";
        ui->impedanceCheckBox->setChecked(on);  // runs toggleImpedanceMode (unchecks on failure)
        return client_->isImpedanceModeActive() == on ? "" : "impedance mode change failed";
    });
}
