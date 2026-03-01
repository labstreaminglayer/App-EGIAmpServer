#include <QApplication>
#include <QDir>
#include "EGIAmpWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    std::string configPath = argc > 1
        ? argv[1]
        : QDir(QCoreApplication::applicationDirPath())
              .filePath("ampserver_config.cfg").toStdString();
    EGIAmpWindow window(nullptr, configPath);
    window.show();
    return app.exec();
}
