#ifndef EGIAMPWINDOW_H
#define EGIAMPWINDOW_H

#include <egiamp/EGIAmpClient.h>

#include <QCloseEvent>
#include <QMainWindow>
#include <memory>
#include <string>

namespace Ui {
class EGIAmpWindow;
}

class EGIAmpWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit EGIAmpWindow(QWidget* parent, const std::string& configFile);
    EGIAmpWindow(const EGIAmpWindow&) = delete;
    ~EGIAmpWindow() override;

private slots:
    void loadConfigDialog();
    void saveConfigDialog();
    void linkAmpserver();
    void shutdownAmpServer();
    void displayError(QString description);
    void unlockUI();
    void lockUI();

signals:
    void appendStatusMessage(QString message);
    void channelCountUpdated(int channelCount);
    void sensorLayoutUpdated(QString sensorName);
    void fieldsEnabled(bool enabled);
    void setLinkButtonText(QString text);
    void error(QString description);
    void enableUI();
    void disableUI();

protected:
    void closeEvent(QCloseEvent* ev) override;

private:
    void loadConfig(const std::string& filename);
    void saveConfig(const std::string& filename);
    void updateUIFromConfig();
    egiamp::AmpServerConfig getConfigFromUI() const;

    Ui::EGIAmpWindow* ui;
    std::unique_ptr<egiamp::EGIAmpClient> client_;
};

#endif // EGIAMPWINDOW_H
