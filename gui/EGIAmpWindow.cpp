#include "EGIAmpWindow.h"
#include "ui_EGIAmpWindow.h"

#include <QComboBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSpinBox>

EGIAmpWindow::EGIAmpWindow(QWidget* parent, const std::string& configFile)
    : QMainWindow(parent)
    , ui(new Ui::EGIAmpWindow)
    , client_(std::make_unique<egiamp::EGIAmpClient>())
{
    ui->setupUi(this);

    // Load initial config
    loadConfig(configFile);

    // Menu connections
    connect(ui->actionQuit, &QAction::triggered, this, &EGIAmpWindow::close);
    connect(ui->actionLoad_Configuration, &QAction::triggered,
            this, &EGIAmpWindow::loadConfigDialog);
    connect(ui->actionSave_Configuration, &QAction::triggered,
            this, &EGIAmpWindow::saveConfigDialog);

    // Link button
    connect(ui->linkButton, &QPushButton::clicked, this, &EGIAmpWindow::linkAmpserver);

    // Signal connections for cross-thread communication
    connect(this, &EGIAmpWindow::appendStatusMessage,
            ui->statusBox, &QPlainTextEdit::appendPlainText,
            Qt::QueuedConnection);
    connect(this, &EGIAmpWindow::error, this, &EGIAmpWindow::displayError,
            Qt::QueuedConnection);
    connect(this, &EGIAmpWindow::enableUI, this, &EGIAmpWindow::unlockUI,
            Qt::QueuedConnection);
    connect(this, &EGIAmpWindow::disableUI, this, &EGIAmpWindow::lockUI,
            Qt::QueuedConnection);

    // Field enable/disable connections
    connect(this, &EGIAmpWindow::fieldsEnabled, ui->sampleRateComboBox, &QComboBox::setEnabled);
    connect(this, &EGIAmpWindow::fieldsEnabled, ui->amplifierId, &QSpinBox::setEnabled);
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
        ui->amplifierId->setValue(config.amplifierId);
        ui->sampleRateComboBox->setCurrentText(QString::number(config.sampleRate));
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
    config.amplifierId = ui->amplifierId->value();
    config.sampleRate = ui->sampleRateComboBox->currentText().toInt();
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
        emit fieldsEnabled(true);
        emit setLinkButtonText("Link");
    } else {
        // Link
        auto config = getConfigFromUI();
        client_->setConfig(config);

        if (!client_->connect()) {
            emit error("Could not connect to AmpServer. Please check network settings "
                       "and ensure AmpServer is running.");
            return;
        }

        if (!client_->startStreaming()) {
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
    emit setLinkButtonText("Link");
    emit fieldsEnabled(true);
}

void EGIAmpWindow::lockUI() {
    emit setLinkButtonText("Unlink");
    emit fieldsEnabled(false);
}
