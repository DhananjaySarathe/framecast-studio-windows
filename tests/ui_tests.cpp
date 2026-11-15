#include "studio_window.hpp"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <algorithm>
#include <iostream>

using namespace framecast;
using namespace framecast::ui;
bool waitFor(const std::function<bool()> &predicate, int ms = 6000) {
    for (int n = 0; n < ms / 20; ++n) {
        QApplication::processEvents();
        if (predicate())
            return true;
        QTest::qWait(20);
    }
    return false;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir files;
    if (!files.isValid())
        return 1;
    StudioWindow window({true, false, 640, 360});
    window.setRecordDirectory(files.path());
    window.resize(1280, 640);
    window.show();
    auto require = [](bool ok, const char *name) {
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
        if (!ok)
            throw std::runtime_error(name);
    };
    try {
        require(window.height() <= 640 &&
                    window.findChild<QPushButton *>("record")->mapTo(&window, QPoint(0, 40)).y() <=
                        window.height(),
                "transport controls fit a scaled laptop work area");
        require(waitFor([&] {
                    auto s = window.controller().live.state();
                    return s.running && s.frame > 4 && s.program.size() == 2;
                }),
                "UI receives both engine previews without recording");
        auto *preview = window.findChild<QPushButton *>("preview-toggle");
        preview->click();
        window.findChild<QPushButton *>("scene-0")->click();
        require(window.programName() == "Screen + camera", "selecting a preview scene leaves program on air");
        require(waitFor([&] { return window.controller().live.state().preview.size() == 2; }),
                "private preview is rendered by the engine");
        window.findChild<QPushButton *>("take-preview")->click();
        require(window.programName() == "Camera", "Put on air commits the candidate scene");
        preview->click();
        window.findChild<QToolButton *>("stage-2")->click();
        window.findChild<QPushButton *>("scene-3")->click();
        window.setPanel(7);
        auto *inspectorScroll = window.findChild<QScrollArea *>("inspector-scroll-7");
        require(inspectorScroll && inspectorScroll->verticalScrollBar()->maximum() > 0,
                "layer inspector scrolls in a scaled laptop work area");
        inspectorScroll->ensureWidgetVisible(window.findChild<QPushButton *>("layer-mirror"));
        QApplication::processEvents();
        require(inspectorScroll->viewport()->rect().contains(
                    window.findChild<QPushButton *>("layer-mirror")
                        ->mapTo(inspectorScroll->viewport(), QPoint(1, 1))),
                "the bottom layer control remains reachable by scrolling");
        auto *x = window.findChild<QDoubleSpinBox *>("layer-0");
        x->setValue(12);
        require(std::abs(window.controller().live.commands().program.horizontal[0].rect.x - .12) < .001,
                "layer edits reach the engine scene model");
        window.findChild<QPushButton *>("mute-0")->click();
        require(window.controller().live.commands().microphone == 0, "mute reaches the engine mixer");
        window.findChild<QPushButton *>("mute-0")->click();
        window.findChild<QToolButton *>("stage-0")->click();
        window.selectScene(0);
        require(window.controller().live.commands().program.horizontal[0].source == "camera" &&
                    window.controller().live.commands().program.vertical[0].source == "camera",
                "single-canvas scene selection switches both orientations like the Mac");
        preview->click();
        window.selectScene(6);
        require(window.controller().live.commands().program.vertical[0].source == "camera" &&
                    window.controller().live.commands().preview->vertical[0].source == "screen",
                "single-canvas preview stays private in both orientations");
        window.takePreview();
        require(window.controller().live.commands().program.horizontal[0].source == "screen" &&
                    window.controller().live.commands().program.vertical[0].source == "screen",
                "single-canvas Put on air commits both orientations");
        require(waitFor([&] {
                    auto s = window.controller().live.state();
                    return s.program.contains(Canvas::vertical) &&
                           s.program.at(Canvas::vertical)->bgra[2] == 0;
                }),
                "engine portrait output reflects the committed screen scene");
        window.selectScene(0);
        preview->click();
        require(window.controller().live.commands().program.vertical[0].source == "screen",
                "leaving Preview does not take a different candidate scene");
        window.findChild<QToolButton *>("stage-2")->click();
        window.selectScene(3);
        require(window.controller().live.commands().program.horizontal.size() == 2 &&
                    window.controller().live.commands().program.vertical.size() == 1,
                "Both changes only the editing canvas");
        window.findChild<QPushButton *>("record")->click();
        auto recordingState = window.controller().live.state();
        require(!window.findChild<QLabel *>("record-status")->text().startsWith("Recording") ||
                    std::any_of(recordingState.outputs.begin(), recordingState.outputs.end(),
                                [](auto &o) { return o.state == "live"; }),
                "record status waits for engine confirmation");
        require(waitFor([&] {
                    auto s = window.controller().live.state();
                    return s.outputs.size() == 2 && s.outputs[0].frames > 10 && s.outputs[1].frames > 10;
                }),
                "Record starts two real engine outputs");
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>();
            if (!dialog)
                return;
            dialog->findChild<QLineEdit *>("destination-name")->setText("Loopback test");
            dialog->findChild<QLineEdit *>("destination-server")->setText("rtmp://127.0.0.1:1/live");
            dialog->findChild<QLineEdit *>("destination-key")->setText("synthetic");
            dialog->findChild<QPushButton *>("destination-add")->click();
            dialog->accept();
        });
        window.findChild<QPushButton *>("destinations")->click();
        window.findChild<QPushButton *>("broadcast")->click();
        require(waitFor([&] {
                    auto s = window.controller().live.state();
                    return std::any_of(s.outputs.begin(), s.outputs.end(),
                                       [](auto &o) { return o.streaming && o.state == "reconnecting"; });
                }),
                "broadcast form starts a real engine connection and displays reconnect state");
        window.findChild<QPushButton *>("broadcast")->click();
        require(waitFor([&] {
                    auto s = window.controller().live.state();
                    return std::any_of(s.outputs.begin(), s.outputs.end(),
                                       [](auto &o) { return o.streaming && o.state == "stopped"; }) &&
                           std::all_of(s.outputs.begin(), s.outputs.end(),
                                       [](auto &o) { return o.streaming || o.state == "live"; });
                }),
                "ending broadcast leaves both recordings live");
        window.findChild<QPushButton *>("scene-6")->click();
        window.findChild<QPushButton *>("record")->click();
        require(waitFor([&] {
                    auto s = window.controller().live.state();
                    return s.outputs.size() == 3 && std::all_of(s.outputs.begin(), s.outputs.end(),
                                                                [](auto &o) { return o.state == "stopped"; });
                }),
                "Stop recording asynchronously finalizes both files");
        require(QDir(files.path()).entryList({"*.mkv"}, QDir::Files).size() == 2, "both canvas files exist");
        require(window.controller().live.state().running, "stopping recording leaves preview running");
        window.saveScreenshots(files.path());
        require(QFileInfo(files.path() + "/studio.png").size() > 1000, "studio render is captured");
        window.controller().stop();
        require(waitFor([&] { return window.controller().live.state().done; }),
                "studio shuts down the capture thread");
        // Owner report on a real laptop: Record pressed with no preview opened Add and never recorded.
        window.findChild<QPushButton *>("record")->click();
        const bool queued = waitFor(
            [&] {
                auto s = window.controller().live.state();
                return s.running && std::count_if(s.outputs.begin(), s.outputs.end(), [](auto &o) {
                                        return !o.streaming && o.state == "live" && o.frames > 10;
                                    }) == 2;
            },
            12000);
        if (!queued) {
            auto s = window.controller().live.state();
            std::cout << "running=" << s.running << " done=" << s.done << " frame=" << s.frame
                      << " busy=" << window.controller().busy() << " error=" << s.error << " record button='"
                      << window.findChild<QPushButton *>("record")->text().toStdString() << "'\n";
            for (auto &o : s.outputs)
                std::cout << "output " << o.id << ' ' << o.state << " frames=" << o.frames << ' ' << o.error << '\n';
        }
        require(queued, "Record with the preview stopped starts the preview and records both canvases");
        window.findChild<QPushButton *>("record")->click();
        const bool finalized = waitFor(
            [&] {
                auto s = window.controller().live.state();
                return QDir(files.path()).entryList({"*.mkv"}, QDir::Files).size() == 4 &&
                       std::all_of(s.outputs.begin(), s.outputs.end(),
                                   [](auto &o) { return o.state == "stopped"; });
            },
            12000);
        if (!finalized) {
            std::cout << "mkv files: " << QDir(files.path()).entryList({"*.mkv"}, QDir::Files).join(", ").toStdString()
                      << '\n';
            for (auto &o : window.controller().live.state().outputs)
                std::cout << "output " << o.id << ' ' << o.state << " frames=" << o.frames << ' ' << o.error << '\n';
        }
        require(finalized, "the queued recording finalizes both files");
        window.controller().stop();
        require(waitFor([&] { return window.controller().live.state().done; }),
                "studio shuts down after the queued recording");
        window.close();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        window.controller().stop();
        return 1;
    }
    return 0;
}
