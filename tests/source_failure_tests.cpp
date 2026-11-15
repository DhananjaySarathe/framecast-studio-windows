// A show must survive its devices. With stand-in sources that fail the way real ones do, the engine has
// to keep recording, say what is wrong, bring each source back when it recovers, and leave the story in
// the log:
//  - a camera that is busy when the session starts and frees up later,
//  - a screen capture that breaks mid-session (graphics driver reset, closed window),
//  - a microphone that is unavailable for the first seconds,
//  - desktop sound whose set-up fails outright.
#include "framecast/core.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
extern "C" {
#include <libavutil/log.h>
}
using namespace framecast;
namespace {
struct FakeVideo final : VideoCapture {
    int calls = 0, failAt;
    explicit FakeVideo(int failAtCall) : failAt(failAtCall) {}
    void poll() override {}
    std::shared_ptr<const Image> at(Nanoseconds) override {
        if (++calls == failAt) throw std::runtime_error("the graphics device was reset");
        return testPattern(calls, 30);
    }
};
struct FakeAudio final : AudioCapture {
    Nanoseconds healthyFrom;
    explicit FakeAudio(Nanoseconds from) : healthyFrom(from) {}
    std::string problem() override { return hostNow() < healthyFrom ? "Microphone unavailable: no device found" : ""; }
};
bool contains(const std::vector<std::string>& list, const std::string& text) {
    for (const auto& item : list) if (item.find(text) != std::string::npos) return true;
    return false;
}
}
int main() {
    av_log_set_level(AV_LOG_ERROR);
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / ("framecast-sources-" + std::to_string(hostNow()));
    fs::create_directories(folder);
    setLogFile((folder / "session.log").string());

    std::atomic<int> screenOpens{0}, cameraOpens{0};
    auto factory = std::make_shared<CaptureFactory>();
    factory->screen = [&](const EngineConfig&) -> std::unique_ptr<VideoCapture> {
        // The first screen capture breaks after about a second; its replacement keeps working.
        return std::make_unique<FakeVideo>(++screenOpens == 1 ? 30 : 0);
    };
    factory->camera = [&](const EngineConfig&) -> std::unique_ptr<VideoCapture> {
        if (++cameraOpens == 1) throw std::runtime_error("the device is in use by another app");
        return std::make_unique<FakeVideo>(0);
    };
    factory->audio = [&](AudioTimeline&, Nanoseconds, bool loopback) -> std::unique_ptr<AudioCapture> {
        if (loopback) throw std::runtime_error("no device found");
        return std::make_unique<FakeAudio>(hostNow() + 2000000000LL);
    };

    LiveControl live;
    EngineConfig cfg;
    cfg.width = 320; cfg.height = 180; cfg.seconds = 8;
    cfg.camera = cfg.microphone = cfg.desktopAudio = true;
    cfg.capture = factory;
    live.setScenes(screenCameraScene());
    OutputSpec spec;
    spec.width = 320; spec.height = 180;
    spec.destination = (folder / "show.mkv").string();
    live.setOutputs({{"recording", spec}});

    std::atomic<bool> stop{false};
    std::atomic<int> result{2};
    std::thread engine([&] {
        try { result = runEngine(cfg, stop, {}, &live); }
        catch (const std::exception& e) { std::cerr << "engine stopped: " << e.what() << '\n'; result = 1; }
    });
    bool sawCamera = false, sawScreen = false, sawMicrophone = false, sawDesktop = false, cameraPicture = false;
    std::vector<std::string> finalWarnings;
    while (!live.state().done) {
        const auto state = live.state();
        sawCamera |= contains(state.warnings, "Camera unavailable: the device is in use");
        sawScreen |= contains(state.warnings, "Screen stopped: the graphics device was reset");
        sawMicrophone |= contains(state.warnings, "Microphone unavailable");
        sawDesktop |= contains(state.warnings, "Desktop sound unavailable: no device found");
        if (state.sources.contains("camera") && state.sources.at("camera")) cameraPicture = true;
        if (state.running) finalWarnings = state.warnings;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    engine.join();
    setLogFile("");
    const auto state = live.state();
    const bool recorded = state.outputs.size() == 1 && state.outputs[0].state == "stopped" &&
                          state.outputs[0].frames + state.outputs[0].dropped >= 230;
    // Only the source that can never come back is still reported at the end.
    const bool recovered = finalWarnings.size() == 1 && contains(finalWarnings, "Desktop sound") &&
                           screenOpens == 2 && cameraOpens >= 2 && cameraPicture;
    std::ifstream file(folder / "session.log");
    std::stringstream text; text << file.rdbuf();
    const std::string log = text.str();
    const bool logged = log.find("engine start: 320x180") != std::string::npos &&
                        log.find("warning: Camera unavailable") != std::string::npos &&
                        log.find("Camera capture restored") != std::string::npos &&
                        log.find("warning: Screen stopped") != std::string::npos &&
                        log.find("Screen capture restored") != std::string::npos &&
                        log.find("output finished: recording stopped") != std::string::npos;

    auto check = [](bool ok, const char* name) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; return ok; };
    bool ok = check(result == 0 && recorded, "the recording runs to the end through four device failures");
    ok &= check(sawCamera && sawScreen && sawMicrophone && sawDesktop, "each failure is reported as a warning while it lasts");
    ok &= check(recovered, "screen, camera and microphone come back on their own; warnings clear");
    ok &= check(logged, "the log file tells the same story");
    if (!ok) {
        for (const auto& output : state.outputs)
            std::cout << output.id << ' ' << output.state << " frames=" << output.frames << " skipped=" << output.dropped << ' ' << output.error << '\n';
        std::cout << "screen opens=" << screenOpens << " camera opens=" << cameraOpens << " final warnings=" << finalWarnings.size() << '\n' << log;
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    return ok ? 0 : 1;
}
