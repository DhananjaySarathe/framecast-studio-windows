#include "framecast/core.hpp"
#include <chrono>
#include <iostream>
#include <thread>
using namespace framecast;
int main() {
    LiveControl live;
    EngineConfig cfg;
    cfg.synthetic = true;
    cfg.width = 320;
    cfg.height = 180;
    cfg.seconds = 2;
    auto camera = screenScene();
    camera.horizontal[0].source = "camera";
    live.setScenes(screenScene(), camera);
    live.setGains(0, 0, 0);
    std::atomic<bool> stop{false};
    std::atomic<int> result{2};
    std::thread engine([&] {
        try {
            result = runEngine(cfg, stop, {}, &live);
        } catch (const std::exception &e) {
            std::cerr << e.what() << '\n';
            result = 1;
        }
    });
    bool frames = false, preview = false, muted = false;
    for (int n = 0; n < 100; ++n) {
        auto state = live.state();
        if (state.frame > 4 && state.running) {
            frames = state.program.size() == 2;
            preview = state.preview.size() == 2;
            muted = state.master == 0;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    stop = true;
    engine.join();
    bool done = live.state().done && !live.state().running;
    std::cout << "idle preview=" << frames << " private preview=" << preview << " muted=" << muted
              << " finalized=" << done << '\n';
    return frames && preview && muted && done && result == 0 ? 0 : 1;
}
