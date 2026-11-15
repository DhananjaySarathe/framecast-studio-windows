#include "studio_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

namespace {
#ifdef _WIN32
std::wstring crashFolder;
LONG WINAPI crashReport(EXCEPTION_POINTERS *exception) {
    // Leave something to look at: the exception in the log and a small dump next to it.
    char line[128];
    std::snprintf(line, sizeof(line), "crash: exception 0x%08lX at %p", exception->ExceptionRecord->ExceptionCode,
                  exception->ExceptionRecord->ExceptionAddress);
    framecast::logLine(line);
    const auto path = crashFolder + L"\\framecast-crash.dmp";
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &info, nullptr, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void installCrashReport(const QString &folder) {
    crashFolder = QDir::toNativeSeparators(folder).toStdWString();
    SetUnhandledExceptionFilter(crashReport);
}
#else
void installCrashReport(const QString &) {}
#endif
} // namespace

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
    // Screenshot runs are automated checks; a person's session gets a log they can send when something breaks.
    if (!parser.isSet("screenshot")) {
        const auto log = framecast::ui::startSessionLog(FRAMECAST_VERSION);
        installCrashReport(QFileInfo(log).absolutePath());
    }
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
