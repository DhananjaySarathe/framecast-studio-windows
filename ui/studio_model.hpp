#pragma once
#include "framecast/core.hpp"
#include <QImage>
#include <QString>
#include <thread>

namespace framecast::ui {
struct SceneEntry {
    QString id, name, group;
    Scene scene;
};
std::vector<SceneEntry> defaultScenes();
QImage imageFor(const std::shared_ptr<const Image> &image);

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
