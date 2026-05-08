// =============================================================================
// main.cpp — Application entry point.
// Launches the Qt GUI.
// =============================================================================

#define NOMINMAX
#include <windows.h>

#include <QApplication>
#include "gui/MainWindow.hpp"

int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);

    QApplication app(argc, argv);
    app.setApplicationName("Equalizer");
    app.setApplicationVersion("0.1.0");

    eq::MainWindow window;
    window.show();

    return app.exec();
}