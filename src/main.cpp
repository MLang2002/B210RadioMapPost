#include <QApplication>
#include <QCoreApplication>

#include "MainWindow.hpp"

#if defined(AUTO_VERSION) && AUTO_VERSION
#include "version.h"
#else
#pragma message("AUTO_VERSION is disabled, using default version 0.9.0")
#undef APP_VERSION
#define APP_VERSION "0.9.0"
#endif

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("B210RadioMap");
    QCoreApplication::setApplicationName("B210RadioMapPost");
    QCoreApplication::setApplicationVersion(APP_VERSION);
    b210post::MainWindow w;
    w.show();
    return app.exec();
}
