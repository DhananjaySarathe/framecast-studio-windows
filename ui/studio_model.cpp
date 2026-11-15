#include "studio_model.hpp"
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <cstring>
#include <algorithm>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

namespace framecast::ui {
QImage imageFor(const std::shared_ptr<const Image> &image) {
    if (!image)
        return {};
    // The copy owns the bytes after the engine publishes its next frame.
    return QImage(image->bgra.data(), image->width, image->height, image->width * 4, QImage::Format_ARGB32)
        .copy();
}
namespace {
std::shared_ptr<const Image> fromQImage(const QImage &source) {
    const QImage image = source.convertToFormat(QImage::Format_ARGB32); // BGRA bytes on little-endian
    auto out = std::make_shared<Image>(image.width(), image.height());
    for (int y = 0; y < image.height(); ++y)
        std::memcpy(out->bgra.data() + size_t(y) * image.width() * 4, image.constScanLine(y), size_t(image.width()) * 4);
    return out;
}
std::shared_ptr<const Image> screenPlaceholder() {
    QImage image(1600, 1000, QImage::Format_ARGB32);
    image.fill(QColor("#171B26"));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.fillRect(QRect(0, 0, 1600, 64), QColor("#222838"));
    const QColor lights[] = {QColor("#FF5F57"), QColor("#FEBC2E"), QColor("#28C840")};
    for (int n = 0; n < 3; ++n) { p.setBrush(lights[n]); p.drawEllipse(QPointF(40 + n * 34, 32), 11, 11); }
    p.setBrush(QColor("#2B3246"));
    p.drawRoundedRect(QRectF(560, 18, 480, 28), 14, 14);
    p.fillRect(QRect(0, 64, 300, 936), QColor("#1D2230"));
    p.setBrush(QColor("#2C3448"));
    for (int n = 0; n < 7; ++n) p.drawRoundedRect(QRectF(36, 116 + n * 70, n % 3 ? 170 : 220, 22), 11, 11);
    p.setBrush(QColor("#7C6CFF"));
    p.drawRoundedRect(QRectF(360, 120, 1180, 380), 26, 26);
    p.setBrush(QColor(255, 255, 255, 60));
    p.drawRoundedRect(QRectF(420, 190, 520, 44), 22, 22);
    p.drawRoundedRect(QRectF(420, 260, 360, 30), 15, 15);
    p.setBrush(QColor("#262D40"));
    for (int n = 0; n < 3; ++n) p.drawRoundedRect(QRectF(360 + n * 400, 548, 380, 380), 22, 22);
    p.setBrush(QColor("#323B53"));
    for (int n = 0; n < 3; ++n) {
        p.drawRoundedRect(QRectF(392 + n * 400, 590, 240, 26), 13, 13);
        p.drawRoundedRect(QRectF(392 + n * 400, 640, 300, 18), 9, 9);
        p.drawRoundedRect(QRectF(392 + n * 400, 676, 200, 18), 9, 9);
    }
    return fromQImage(image);
}
std::shared_ptr<const Image> cameraPlaceholder() {
    QImage image(1280, 720, QImage::Format_ARGB32);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    QRadialGradient glow(QPointF(640, 260), 760);
    glow.setColorAt(0, QColor("#3D3270"));
    glow.setColorAt(1, QColor("#141625"));
    p.fillRect(image.rect(), glow);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#6A5CB8"));
    p.drawEllipse(QPointF(640, 290), 118, 118);
    QPainterPath body;
    body.addRoundedRect(QRectF(380, 450, 520, 420), 250, 210);
    p.drawPath(body);
    return fromQImage(image);
}
}
const std::map<std::string, std::shared_ptr<const Image>> &placeholderSources() {
    static const std::map<std::string, std::shared_ptr<const Image>> sources{
        {"screen", screenPlaceholder()}, {"camera", cameraPlaceholder()}};
    return sources;
}
std::vector<SceneEntry> defaultScenes() {
    const double mh = .03, my = .03 * 16 / 9, mv = .04, mvy = .04 * 9 / 16;
    auto camera = [](Rect r = {}, Mask mask = Mask::none, double radius = 0) {
        return Layer{"camera", r, Fit::fill, mask, radius};
    };
    auto screen = [](Rect r = {}, Fit fit = Fit::fit) {
        return Layer{"screen", r, fit, Mask::rounded, .035};
    };
    std::vector<SceneEntry> out;
    out.push_back({"camera", "Camera", "Camera", {{camera()}, {camera()}}});
    out.push_back({"framed",
                   "Camera card",
                   "Camera",
                   {{camera({mh, my, 1 - 2 * mh, 1 - 2 * my}, Mask::rounded, .035)},
                    {camera({mv, .10, 1 - 2 * mv, .70}, Mask::rounded, .035)}}});
    out.push_back({"bubble",
                   "Camera bubble",
                   "Camera",
                   {{camera({.30, .12, .40, .40 * 16 / 9}, Mask::circle)},
                    {camera({.12, .28, .76, .76 * 9 / 16}, Mask::circle)}}});
    out.push_back({"screen-camera", "Screen + camera", "Screen share", screenCameraScene()});
    out.push_back(
        {"mini",
         "Screen + mini camera",
         "Screen share",
         {{screen({}), camera({1 - mh - .14, my, .14, .14 * 16 / 9}, Mask::rounded, .22)},
          {screen({0, .20, 1, .5625}), camera({1 - mv - .30, .06, .30, .30 * 9 / 16}, Mask::rounded, .22)}}});
    const double w = (1 - 2 * mh - .02) / 2, h = w * 16 / 9 / 1.6;
    out.push_back(
        {"split",
         "Split",
         "Screen share",
         {{screen({mh, (1 - h) / 2, w, h}), camera({mh + w + .02, (1 - h) / 2, w, h}, Mask::rounded, .035)},
          {screen({mv, .07, 1 - 2 * mv, .42}), camera({mv, .51, 1 - 2 * mv, .42}, Mask::rounded, .035)}}});
    out.push_back({"screen",
                   "Screen only",
                   "Screen share",
                   {{screen({})}, {screen({mv, mvy, 1 - 2 * mv, 1 - 2 * mvy})}}});
    out.push_back({"presenter",
                   "Presenter",
                   "Screen share",
                   {{camera({mh, my, .36, 1 - 2 * my}, Mask::rounded, .035),
                     screen({mh + .38, .17, 1 - 2 * mh - .38, .66})},
                    {camera({mv, mvy + .03, 1 - 2 * mv, .52}, Mask::rounded, .035),
                     screen({mv, .60, 1 - 2 * mv, .30})}}});
    return out;
}
QString logDirectory() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("logs");
}
QString startSessionLog(const QString &version) {
    QDir directory(logDirectory());
    directory.mkpath(".");
    const auto old = directory.entryList({"framecast-*.log"}, QDir::Files, QDir::Name);
    for (int n = 0; n + 9 < old.size(); ++n)
        directory.remove(old[n]);
    const auto path =
        directory.filePath("framecast-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".log");
    setLogFile(path.toStdString());
    logLine("Framecast Studio Windows " + version.toStdString());
    logLine(describeSystem());
    std::string encoders = "H.264 encoders in this build:";
    for (const auto &name : availableH264Encoders())
        encoders += " " + name;
    logLine(encoders);
    return path;
}
UiWatchdog::UiWatchdog() : lastBeat_(hostNow()) {
    thread_ = std::thread([this] {
        bool reported = false;
        while (!stop_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            const auto silent = hostNow() - lastBeat_.load();
            if (silent > 3000000000LL && !reported) {
                std::lock_guard lock(mutex_);
                logLine("warning: the interface has not responded for 3 seconds; last action: " + action_);
                reported = true;
            } else if (silent < 1000000000LL && reported) {
                logLine("the interface is responding again");
                reported = false;
            }
        }
    });
}
UiWatchdog::~UiWatchdog() {
    stop_ = true;
    if (thread_.joinable())
        thread_.join();
}
void UiWatchdog::beat() {
    lastBeat_ = hostNow();
}
void UiWatchdog::action(const QString &what) {
    logLine("ui: " + what.toStdString());
    std::lock_guard lock(mutex_);
    action_ = what.toStdString();
}
UiWatchdog &watchdog() {
    static UiWatchdog instance;
    return instance;
}
StudioController::~StudioController() {
    stop();
    if (worker_.joinable())
        worker_.join();
}
bool StudioController::start(const EngineConfig &config) {
    collect();
    if (worker_.joinable())
        return false;
    stop_ = false;
    finished_ = false;
    live.publish({});
    worker_ = std::thread([this, config] {
        bool initialized = false;
        av_log_set_level(AV_LOG_QUIET);
        avformat_network_init();
        try {
#ifdef _WIN32
            if (!config.synthetic) {
                initializeWindowsMedia();
                initialized = true;
            }
#endif
            runEngine(config, stop_, {}, &live);
        } catch (const std::exception &e) {
            logLine(std::string("error: the session stopped: ") + e.what());
            auto state = live.state();
            state.running = false;
            state.done = true;
            state.error = e.what();
            live.publish(std::move(state));
        }
#ifdef _WIN32
        if (initialized)
            shutdownWindowsMedia();
#else
        (void)initialized;
#endif
        avformat_network_deinit();
        finished_ = true;
    });
    return true;
}
void StudioController::stop() {
    stop_ = true;
}
void StudioController::collect() {
    if (worker_.joinable() && finished_)
        worker_.join();
}
} // namespace framecast::ui
