#include <QApplication>
#include "EGIAmpWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    EGIAmpWindow window(nullptr, argc > 1 ? argv[1] : "ampserver_config.cfg");
    window.show();
    return app.exec();
}
