#include "framecast/core.hpp"
#include <cmath>
#include <stdexcept>
#include <thread>
#include <algorithm>

namespace framecast {
int runEngine(const EngineConfig& cfg, std::atomic<bool>& stop,
              const std::function<void(const std::vector<OutputStatus>&)>& onStatus, LiveControl* live) {
    if (cfg.fps < 1 || cfg.fps > 120 || cfg.width < 2 || cfg.height < 2 || cfg.width > 4096 || cfg.height > 4096 ||
        cfg.width%2 || cfg.height%2 || !std::isfinite(cfg.seconds) || cfg.seconds <= 0 || cfg.seconds > 86400 || (!live && cfg.outputs.empty()))
        throw std::invalid_argument("Invalid engine configuration");
    for (const auto& spec : cfg.outputs) {
        const int w = spec.canvas == Canvas::horizontal ? cfg.width : cfg.height;
        const int h = spec.canvas == Canvas::horizontal ? cfg.height : cfg.width;
        if (spec.width != w || spec.height != h || spec.fps != cfg.fps) throw std::invalid_argument("Output does not match its canvas");
    }
    AudioTimeline mixer;
    std::unique_ptr<VideoCapture> screen, camera;
    std::vector<std::unique_ptr<AudioCapture>> audioInputs;
    const Nanoseconds origin = hostNow();
    if (!cfg.synthetic) {
#ifdef _WIN32
        screen = captureScreen(cfg.monitor, cfg.windowHandle);
        if (cfg.camera) camera = captureCamera(cfg.cameraIndex);
        if (cfg.microphone) audioInputs.push_back(captureAudio(mixer, origin, false));
        if (cfg.desktopAudio) audioInputs.push_back(captureAudio(mixer, origin, true));
        if (cfg.microphone && cfg.desktopAudio) { mixer.gain("microphone", .7f); mixer.gain("desktop", .7f); }
#else
        throw std::runtime_error("Native capture requires Windows. Use --synthetic for engine verification here.");
#endif
    }
    struct ActiveOutput { OutputSpec spec; std::unique_ptr<OutputWorker> worker; bool closing = false; };
    std::map<std::string, ActiveOutput> outputs;
    std::vector<OutputStatus> completed;
    std::map<std::string, OutputSpec> initial;
    for (size_t n = 0; n < cfg.outputs.size(); ++n) initial["cli-"+std::to_string(n)] = cfg.outputs[n];
    auto reconcile = [&](const std::map<std::string, OutputSpec>& desired) {
        for (auto it = outputs.begin(); it != outputs.end();) {
            auto& [id, entry] = *it;
            if (!desired.contains(id) && !entry.closing) { entry.closing = true; entry.worker->requestFinish(); }
            if (entry.closing && entry.worker->finished()) {
                auto status = entry.worker->status(); status.id = id; status.streaming = entry.spec.streaming;
                completed.push_back(status);
                if (completed.size() > 32) completed.erase(completed.begin());
                it = outputs.erase(it);
            } else ++it;
        }
        if (desired.size() > 16) throw std::invalid_argument("Too many outputs");
        for (const auto& [id, spec] : desired) if (!outputs.contains(id)) {
            const int width = spec.canvas == Canvas::horizontal ? cfg.width : cfg.height;
            const int height = spec.canvas == Canvas::horizontal ? cfg.height : cfg.width;
            if (spec.width != width || spec.height != height || spec.fps != cfg.fps)
                throw std::invalid_argument("Output does not match its canvas");
            outputs.emplace(id, ActiveOutput{spec, std::make_unique<OutputWorker>(spec)});
        }
    };
    auto report = [&] {
        std::vector<OutputStatus> statuses = completed;
        for (auto& [id, output] : outputs) {
            auto status = output.worker->status(); status.id = id; status.streaming = output.spec.streaming;
            if (output.closing && !output.worker->finished()) status.state = "stopping";
            statuses.push_back(std::move(status));
        }
        if (onStatus) onStatus(statuses);
        return statuses;
    };
    LiveState state;
    state.running = true;
    if (live) live->publish(state);
    reconcile(live ? live->commands().outputs : initial);
    // Delay playout so audio packets and captured video can arrive before their shared deadline.
    constexpr Nanoseconds latency = 120000000;
    const int64_t count = static_cast<int64_t>(std::ceil(cfg.seconds*cfg.fps));
    for (int64_t frame = 0; frame < count && !stop; ++frame) {
        const Nanoseconds time = origin + frame*1000000000LL/cfg.fps;
        while (!stop && hostNow() < time+latency) {
            if (screen) screen->poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (stop) break;
        if (screen) screen->poll();
        for (auto& input : audioInputs) input->check();
        const int64_t sample = samplesAtFrame(frame, cfg.fps), next = samplesAtFrame(frame+1, cfg.fps);
        std::map<std::string, std::shared_ptr<const Image>> images;
        if (cfg.synthetic) {
            images["screen"] = testPattern(frame, cfg.fps);
            images["camera"] = images["screen"];
            std::vector<float> tone(static_cast<size_t>(next-sample)*2);
            for (int64_t n = sample; n < next; ++n) {
                const double value = n%sampleRate < sampleRate/10 ? .4*std::sin(2*3.141592653589793*1000*n/sampleRate) : 0;
                tone[(n-sample)*2] = tone[(n-sample)*2+1] = static_cast<float>(value);
            }
            mixer.push("tone", sample, tone.data(), static_cast<int>(next-sample));
        } else {
            images["screen"] = screen->at(time);
            if (camera) images["camera"] = camera->at(time);
        }
        auto commands = live ? live->commands() : LiveCommands{};
        if (live) {
            mixer.gain("microphone", commands.microphone); mixer.gain("desktop", commands.desktop);
            mixer.gain("tone", commands.microphone);
            reconcile(commands.outputs);
        }
        auto audio = mixer.mix(sample, static_cast<int>(next-sample));
        if (live && commands.master != 1) {
            auto adjusted = std::make_shared<std::vector<float>>(*audio);
            for (auto& value : *adjusted) value = std::clamp(value*commands.master, -1.f, 1.f);
            audio = adjusted;
        }
        std::map<Canvas, std::shared_ptr<const Image>> rendered;
        auto renderCanvas = [&](Canvas canvas) {
            auto& image = rendered[canvas];
            if (!image) image = std::make_shared<Image>(compose(live ? commands.program : cfg.scene, canvas,
                canvas == Canvas::horizontal ? cfg.width : cfg.height,
                canvas == Canvas::horizontal ? cfg.height : cfg.width, images));
            return image;
        };
        for (auto& [id, output] : outputs) {
            (void)id;
            if (!output.closing) output.worker->submit({frame, sample, renderCanvas(output.spec.canvas), audio});
        }
        if (live && frame%2 == 0) {
            renderCanvas(Canvas::horizontal); renderCanvas(Canvas::vertical);
            state.program = rendered; state.preview.clear(); state.sources = images;
            if (commands.preview) for (auto canvas : {Canvas::horizontal, Canvas::vertical})
                state.preview[canvas] = std::make_shared<Image>(compose(*commands.preview, canvas,
                    canvas == Canvas::horizontal ? cfg.width : cfg.height,
                    canvas == Canvas::horizontal ? cfg.height : cfg.width, images));
            state.frame = frame;
            state.microphone = mixer.level(cfg.synthetic ? "tone" : "microphone", sample, static_cast<int>(next-sample));
            state.desktop = mixer.level("desktop", sample, static_cast<int>(next-sample));
            double square = 0; for (float value : *audio) square += value*value;
            state.master = static_cast<float>(std::sqrt(square/audio->size()));
            state.outputs = report(); live->publish(state);
        } else if (!live && frame%cfg.fps == 0) {
            auto statuses = report();
            if (!statuses.empty() && std::all_of(statuses.begin(), statuses.end(), [](const auto& s) { return s.state == "failed"; })) break;
        }
    }
    audioInputs.clear(); camera.reset(); screen.reset();
    for (auto& [id, output] : outputs) { (void)id; output.worker->requestFinish(); }
    for (auto& [id, output] : outputs) { (void)id; output.worker->finish(); }
    int result = 0;
    state.outputs = report(); state.running = false; state.done = true;
    if (live) live->publish(state);
    for (const auto& s : state.outputs) if (s.state == "failed" || s.frames == 0) result = 1;
    return result;
}
}
