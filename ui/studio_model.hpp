#pragma once
#include "framecast/core.hpp"
#include <QImage>
#include <QString>
#include <atomic>
#include <mutex>
#include <thread>

namespace framecast::ui {
struct SceneEntry {
    QString id, name, group;
    Scene scene;
};
std::vector<SceneEntry> defaultScenes();
QImage imageFor(const std::shared_ptr<const Image> &image);
// Drawn stand-ins for "screen" and "camera", so scene thumbnails show their layout before capture starts
// (or when no camera is selected).
const std::map<std::string, std::shared_ptr<const Image>> &placeholderSources();

// Where session logs are kept: the app's local data folder, "logs".
QString logDirectory();
// Starts this session's log there (keeping the newest ten) and writes the app version, the computer's
// description and the available encoders. Returns the file's path.
QString startSessionLog(const QString &version);

// Notes what the interface is doing and, from its own thread, writes to the log when the interface stops
// responding and for how long: a frozen window can then be traced to the action that froze it.
class UiWatchdog {
  public:
    UiWatchdog();
    ~UiWatchdog();
    void beat();
    void action(const QString &what);

  private:
    std::atomic<int64_t> lastBeat_;
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::string action_ = "starting";
    std::thread thread_;
};
UiWatchdog &watchdog();

// Lives on the interface thread; the engine and its Windows COM objects live on worker_.
class StudioController {
  public:
    ~StudioController();
    bool start(const EngineConfig &config);
    void stop();
    void collect();
    bool busy() const {
        return worker_.joinable() && !finished_;
    }
    LiveControl live;

  private:
    std::atomic<bool> stop_{false}, finished_{true};
    std::thread worker_;
};
} // namespace framecast::ui
