// Real-time check at the size people actually use: preview, then (as in the studio, a moment later) a landscape
// and a portrait recording at 1920x1080 / 1080x1920, 30 fps, for six seconds. The engine must keep up with the clock, every
// recorded frame must reach the encoder, and the studio monitors must receive monitor-sized renders.
// The older checks ran at 320x180-640x360, which hid a pipeline that fell behind on laptops.
#include "framecast/core.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>
extern "C" {
#include <libavutil/log.h>
}
using namespace framecast;
int main() {
    av_log_set_level(AV_LOG_ERROR);
    std::cout << "H.264 encoders in this FFmpeg build:";
    for (const auto& name : availableH264Encoders()) std::cout << ' ' << name;
    std::cout << '\n';
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / ("framecast-realtime-" + std::to_string(hostNow()));
    fs::create_directories(folder);
    LiveControl live;
    EngineConfig cfg;
    cfg.synthetic = true;
    cfg.width = 1920;
    cfg.height = 1080;
    constexpr double recordAfter = 1.5, recordFor = 6;
    cfg.seconds = recordAfter + recordFor;
    Scene scene = screenCameraScene();
    scene.horizontal.push_back({"camera", {.70, .05, .2, .2*16/9}, Fit::fill, Mask::circle});
    live.setScenes(scene, screenScene());
    std::map<std::string, OutputSpec> outputs;
    for (auto canvas : {Canvas::horizontal, Canvas::vertical}) {
        OutputSpec spec;
        spec.canvas = canvas;
        spec.width = canvas == Canvas::horizontal ? 1920 : 1080;
        spec.height = canvas == Canvas::horizontal ? 1080 : 1920;
        spec.destination = (folder / (canvas == Canvas::horizontal ? "landscape.mkv" : "portrait.mkv")).string();
        if (const char* encoder = std::getenv("FRAMECAST_TEST_ENCODER")) spec.encoder = encoder;
        outputs[canvas == Canvas::horizontal ? "landscape" : "portrait"] = spec;
    }
    std::atomic<bool> stop{false};
    std::atomic<int> result{2};
    const auto start = std::chrono::steady_clock::now();
    std::thread engine([&] {
        try { result = runEngine(cfg, stop, {}, &live); }
        catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    });
    int monitorWidth = 0;
    bool recording = false;
    while (!live.state().done) {
        if (!recording && std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= recordAfter) {
            live.setOutputs(outputs);
            recording = true;
        }
        auto state = live.state();
        if (state.program.contains(Canvas::horizontal)) monitorWidth = state.program.at(Canvas::horizontal)->width;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    engine.join();
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto state = live.state();
    const int64_t expected = static_cast<int64_t>(cfg.seconds * cfg.fps);
    const uint64_t expectedRecorded = static_cast<uint64_t>(recordFor * cfg.fps * .95);
    bool recorded = state.outputs.size() == 2;
    for (const auto& output : state.outputs) {
        std::cout << output.id << ": " << output.state << " encoder=" << output.encoder << " frames=" << output.frames
                  << " dropped=" << output.dropped << " convert_ms=" << output.convertMs << " encode_ms=" << output.encodeMs
                  << (output.error.empty() ? "" : " error=" + output.error) << '\n';
        recorded = recorded && output.state != "failed" && output.dropped == 0 && output.frames >= expectedRecorded;
    }
    // Wall time is the recording length plus playout latency and encoder shutdown.
    const bool onTime = elapsed < cfg.seconds + 3;
    // A few late frames during start-up are fine; a pipeline that cannot keep up repeats most of them.
    const bool keptUp = state.lateFrames * 10 <= uint64_t(expected);
    const bool small = monitorWidth > 0 && monitorWidth <= 960;
    std::cout << "elapsed=" << elapsed << "s late=" << state.lateFrames << "/" << expected
              << " monitor width=" << monitorWidth << '\n';
    std::cout << (recorded ? "PASS" : "FAIL") << " both 1080p recordings received every frame\n";
    std::cout << (keptUp ? "PASS" : "FAIL") << " engine kept up with real time\n";
    std::cout << (onTime ? "PASS" : "FAIL") << " session finished on schedule\n";
    std::cout << (small ? "PASS" : "FAIL") << " studio monitors receive monitor-sized renders\n";
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    return result == 0 && recorded && keptUp && onTime && small ? 0 : 1;
}
