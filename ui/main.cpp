#include "studio_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTimer>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Framecast Studio");
    app.setOrganizationName("Framecast");
    QCommandLineParser parser;
    parser.setApplicationDescription("Framecast Studio Windows");
    parser.addHelpOption();
    parser.addOption({"synthetic", "Use synthetic sources without camera, microphone or screen access."});
    parser.addOption({"screenshot", "Save UI screenshots to this directory, then exit.", "directory"});
    parser.addOption({"size", "Window dimensions for screenshot verification.", "WIDTHxHEIGHT", "1440x900"});
    parser.addOption({"empty", "Show the startup state without starting synthetic preview."});
    parser.process(app);
    framecast::ui::UiOptions options;
    options.synthetic = parser.isSet("synthetic");
    options.persistent = !parser.isSet("screenshot");
    options.autoStart = !parser.isSet("empty");
    if (parser.isSet("screenshot")) {
        if (!options.synthetic)
            return 2;
        options.width = 640;
        options.height = 360;
    }
    framecast::ui::StudioWindow window(options);
    auto size = parser.value("size").split('x');
    if (size.size() == 2 && (parser.isSet("size") || parser.isSet("screenshot")))
        window.resize(size[0].toInt(), size[1].toInt());
    window.show();
    if (parser.isSet("screenshot"))
        QTimer::singleShot(1800, &window, [&] {
            const auto directory = parser.value("screenshot");
            window.saveScreenshots(directory);
            if (parser.isSet("empty")) {
                window.controller().stop();
                QTimer::singleShot(500, &app, &QApplication::quit);
                return;
            }
            window.setRecordDirectory(QDir(directory).filePath("recordings"));
            window.toggleRecord();
            window.grab().save(QDir(directory).filePath("starting.png"));
            QTimer::singleShot(1000, &window, [&, directory] {
                window.grab().save(QDir(directory).filePath("recording.png"));
                window.toggleRecord();
                window.controller().stop();
                QTimer::singleShot(500, &app, &QApplication::quit);
            });
        });
    return app.exec();
}
