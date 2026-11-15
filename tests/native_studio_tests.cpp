// The path a person takes on a real PC, driven through the real window with Windows' own devices and no
// synthetic sources: click Add, wait for the lists of displays, windows and cameras, choose Start preview
// on the first display with microphone and desktop sound ticked, record both canvases, stop, and find the
// files. Every earlier studio test used synthetic sources, which is how a freeze in the Add dialog and a
// pipeline that fell behind reached people untested. Microphone, desktop sound and camera may be missing on
// the machine that runs this; the session must carry on and say so.
#include "studio_window.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace framecast;
using namespace framecast::ui;
namespace {
bool waitFor(const std::function<bool()> &predicate, int ms) {
    for (int n = 0; n < ms / 20; ++n) {
        QApplication::processEvents();
        if (predicate())
            return true;
        QTest::qWait(20);
    }
    return false;
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir files;
    if (!files.isValid())
        return 1;
    const auto logPath = QDir(files.path()).filePath("native-studio.log");
    setLogFile(logPath.toStdString());
    logLine(describeSystem());
    int failures = 0;
    auto check = [&](bool ok, const std::string &name) {
        std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
        if (!ok)
            ++failures;
        return ok;
    };
    {
        StudioWindow window({false, false, 1280, 720, false});
        window.setRecordDirectory(files.path());
        window.resize(1280, 720);
        window.show();
        QApplication::processEvents();

        // The Add dialog is modal, so it is operated from a timer running inside its event loop.
        int displays = -1, cameras = -1;
        bool started = false;
        QTimer operate;
        QObject::connect(&operate, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || started)
                return;
            const auto combos = dialog->findChildren<QComboBox *>();
            const auto boxes = dialog->findChildren<QCheckBox *>();
            if (combos.size() < 2)
                return;
            displays = combos[0]->count();
            cameras = combos[1]->count() - 1;
            for (auto *box : boxes)
                if (box->isEnabled())
                    box->setChecked(true);
            for (auto *button : dialog->findChildren<QPushButton *>())
                if (button->text() == "Start preview" && button->isEnabled()) {
                    started = true;
                    button->click();
                    return;
                }
            dialog->reject(); // nothing to capture on this machine; reported below
            started = true;
        });
        operate.start(100);
        QTimer::singleShot(0, &window, [&] { window.findChild<QPushButton *>("add-sources")->click(); });
        check(waitFor([&] { return started && !QApplication::activeModalWidget(); }, 30000),
              "Add opens, lists devices and closes without freezing the window");
        operate.stop();
        std::cout << "displays and windows offered: " << displays << ", cameras: " << cameras << std::endl;
        check(displays > 0, "at least one display is offered");

        const bool running = waitFor(
            [&] {
                const auto s = window.controller().live.state();
                return s.running && s.program.size() == 2 && s.sources.contains("screen") && s.sources.at("screen");
            },
            20000);
        const auto previewState = window.controller().live.state();
        check(running, "preview shows the captured display on both canvases" +
                           (previewState.error.empty() ? std::string() : ": " + previewState.error));
        for (const auto &warning : previewState.warnings)
            std::cout << "warning shown: " << warning << std::endl;
        check(std::none_of(previewState.warnings.begin(), previewState.warnings.end(),
                           [](const auto &w) { return w.rfind("Screen", 0) == 0; }),
              "the display itself captures without a warning");

        if (running) {
            window.findChild<QPushButton *>("record")->click();
            const bool recording = waitFor(
                [&] {
                    const auto s = window.controller().live.state();
                    return s.outputs.size() == 2 && s.outputs[0].frames > 60 && s.outputs[1].frames > 60;
                },
                30000);
            const auto recordState = window.controller().live.state();
            for (const auto &output : recordState.outputs)
                std::cout << output.id << ": " << output.state << " encoder=" << output.encoder << " frames="
                          << output.frames << " skipped=" << output.dropped << " convert_ms=" << output.convertMs
                          << " encode_ms=" << output.encodeMs << (output.error.empty() ? "" : " error=" + output.error)
                          << std::endl;
            check(recording, "Record writes both canvases from the real display");
            check(recordState.lateFrames * 5 <= uint64_t(std::max<int64_t>(recordState.frame, 1)),
                  "the engine keeps up with real time (" + std::to_string(recordState.lateFrames) + " late of " +
                      std::to_string(recordState.frame) + ")");
            window.findChild<QPushButton *>("record")->click();
            check(waitFor(
                      [&] {
                          const auto s = window.controller().live.state();
                          return std::all_of(s.outputs.begin(), s.outputs.end(),
                                             [](const auto &o) { return o.state == "stopped"; }) &&
                                 QDir(files.path()).entryList({"*.mkv"}, QDir::Files).size() == 2;
                      },
                      20000),
                  "stopping saves two finished recordings");
            for (const auto &name : QDir(files.path()).entryList({"*.mkv"}, QDir::Files))
                check(QFileInfo(QDir(files.path()).filePath(name)).size() > 20000,
                      "recording " + name.toStdString() + " has content");
            check(window.controller().live.state().running, "the preview keeps running after recording stops");
        }
        window.controller().stop();
        check(waitFor([&] { return !window.controller().busy(); }, 20000), "the session shuts down cleanly");
    }
    setLogFile("");
    std::ifstream file(logPath.toStdString());
    std::stringstream log;
    log << file.rdbuf();
    std::cout << "---- session log ----\n" << log.str() << std::endl;
    return failures ? 1 : 0;
}
