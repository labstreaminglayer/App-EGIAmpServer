#ifndef EGIAMPWINDOW_H
#define EGIAMPWINDOW_H

#include <egiamp/EGIAmpClient.h>
#include <egiamp/RemoteControl.h>

#include <QCloseEvent>
#include <QMainWindow>
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace Ui {
class EGIAmpWindow;
}

class EGIAmpWindow : public QMainWindow, public egiamp::RemoteControlTarget {
    Q_OBJECT

public:
    explicit EGIAmpWindow(QWidget* parent, const std::string& configFile);
    EGIAmpWindow(const EGIAmpWindow&) = delete;
    ~EGIAmpWindow() override;

    // Remote control (called on the remote control thread; they run their UI
    // work on the UI thread so the window always shows the current state).
    egiamp::EGIAmpClient& client() override { return *client_; }
    egiamp::AmpServerConfig config() override;
    void setConfig(const egiamp::AmpServerConfig& config) override;
    std::string link() override;
    std::string unlink() override;
    std::string setImpedance(bool on) override;

private slots:
    void loadConfigDialog();
    void saveConfigDialog();
    void linkAmpserver();
    void shutdownAmpServer();
    void toggleImpedanceMode(bool enabled);
    void triggerBncOut();
    void displayError(QString description);
    void unlockUI();
    void lockUI();
    void onLinkFinished(bool ok, QString errorMessage);
    void onAmpDetectedRunning(int detectedRate);

signals:
    void appendStatusMessage(QString message);
    void channelCountUpdated(int channelCount);
    void sensorLayoutUpdated(QString sensorName);
    void fieldsEnabled(bool enabled);
    void setLinkButtonText(QString text);
    void error(QString description);
    void enableUI();
    void disableUI();
    // Emitted from the link worker thread (see linkAmpserver)
    void linkFinished(bool ok, QString errorMessage);
    void ampDetectedRunning(int detectedRate);

protected:
    void closeEvent(QCloseEvent* ev) override;

private:
    void loadConfig(const std::string& filename);
    void applyConfig(const egiamp::AmpServerConfig& config);
    QString runLink();  // connect/query/start; returns the error, empty on success
    template <typename F> auto onUiThread(F f);
    void saveConfig(const std::string& filename);
    void updateUIFromConfig();
    egiamp::AmpServerConfig getConfigFromUI() const;
    void populateSampleRateCombo();
    int defaultSampleRateIndex() const;
    int findSampleRateIndex(int rate, bool native) const;

    void joinLinkThread();

    Ui::EGIAmpWindow* ui;
    std::unique_ptr<egiamp::EGIAmpClient> client_;
    // Runs the connect/query/start sequence off the UI thread — an NA500 cold
    // start legitimately takes 15+ s (power-on wait), and a hung AmpServer
    // must never freeze the UI. Progress arrives via queued signals.
    std::unique_ptr<std::thread> linkThread_;
    // Settings the window has no field for, kept from the loaded config.
    egiamp::AmpServerConfig baseConfig_;
    std::atomic<bool> closing_{false};
    std::unique_ptr<egiamp::RemoteControlServer> remote_;
};

#endif // EGIAMPWINDOW_H
