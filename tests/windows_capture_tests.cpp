#include "framecast/core.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QPainter>
#include <QTimer>
#include <QWidget>
#include <chrono>
#include <iostream>
#include <thread>

class TestWindow final : public QWidget {
  public:
    int colour = 0;
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), colour == 0 ? Qt::red : colour == 1 ? Qt::green : Qt::blue);
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    TestWindow window;
    window.setWindowTitle("Framecast capture verification");
    window.resize(640, 360);
    window.show();
    const auto handle = static_cast<uintptr_t>(window.winId());
    std::atomic<bool> done{false};
    bool red = false, green = false, blue = false;
    std::string error;
    std::thread worker([&] {
        bool initialized = false;
        try {
            framecast::initializeWindowsMedia();
            initialized = true;
            auto source = framecast::captureScreen(0, handle);
            const auto deadline = framecast::hostNow() + 10000000000LL;
            while (framecast::hostNow() < deadline && !(red && green && blue)) {
                source->poll();
                if (auto image = source->at(framecast::hostNow())) {
                    auto p = (static_cast<size_t>(image->height / 2) * image->width + image->width / 2) * 4;
                    red |= image->bgra[p + 2] > 200 && image->bgra[p + 1] < 50 && image->bgra[p] < 50;
                    green |= image->bgra[p + 1] > 200 && image->bgra[p + 2] < 50 && image->bgra[p] < 50;
                    blue |= image->bgra[p] > 200 && image->bgra[p + 1] < 50 && image->bgra[p + 2] < 50;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception &e) {
            error = e.what();
        }
        if (initialized)
            framecast::shutdownWindowsMedia();
        done = true;
    });
    QTimer colour;
    QObject::connect(&colour, &QTimer::timeout, &window, [&] {
        window.colour = (window.colour + 1) % 3;
        window.update();
    });
    colour.start(350);
    while (!done) {
        app.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    worker.join();
    if (!error.empty()) {
        std::cerr << "Native window capture: " << error << '\n';
        // A hosted VM may not expose the Windows capture service. Never call a skipped capture a pass.
        return error.find("unavailable") != std::string::npos ? 77 : 1;
    }
    std::cout << "Native Windows Graphics Capture: red=" << red << " green=" << green << " blue=" << blue
              << '\n';
    return red && green && blue ? 0 : 1;
}
