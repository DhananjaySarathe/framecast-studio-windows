#include "framecast/core.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace framecast {
namespace {
// A video source that is allowed to fail. It opens in the background, is dropped and reopened after an
// error, and never ends the session: the show carries on without it and the studio shows a warning.
// (OBS and Sunshine both treat devices this way; a webcam that is busy, a closed window or a graphics
// driver reset must not stop a recording.)
class VideoSlot {
public:
    VideoSlot(std::string name, std::function<std::unique_ptr<VideoCapture>()> open)
        : name_(std::move(name)), open_(std::move(open)) {}
    ~VideoSlot() {
        if (opening_.valid()) { try { (void)opening_.get(); } catch (...) {} }
    }
    // Opens at once and lets the error out: used for the screen at start, so a wrong choice is reported
    // immediately instead of as a warning over an empty picture.
    void openNow() { source_ = open_(); logLine(name_ + " capture started"); }
    void tick(Nanoseconds now) {
        if (opening_.valid()) {
            if (opening_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            try {
                source_ = opening_.get();
                logLine(name_ + (problem_.empty() ? " capture started" : " capture restored"));
                problem_.clear();
            } catch (const std::exception& e) { fail(name_ + " unavailable: " + e.what(), now); }
            return;
        }
        if (!source_ && now >= retryAt_) opening_ = std::async(std::launch::async, open_);
    }
    void poll(Nanoseconds now) {
        if (!source_) return;
        try { source_->poll(); }
        catch (const std::exception& e) { fail(name_ + " stopped: " + e.what(), now); }
    }
    std::shared_ptr<const Image> at(Nanoseconds time, Nanoseconds now) {
        if (!source_) return nullptr;
        try { return source_->at(time); }
        catch (const std::exception& e) { fail(name_ + " stopped: " + e.what(), now); return nullptr; }
    }
    // The failure, or else the open source's notice (a minimised window, a camera without pictures).
    std::string problem() const {
        if (!problem_.empty() || !source_) return problem_;
        try { return source_->notice(); } catch (...) { return {}; }
    }
    void close() { source_.reset(); }
private:
    void fail(const std::string& what, Nanoseconds now) {
        if (what != problem_) logLine("warning: " + what);
        problem_ = what;
        source_.reset();
        retryAt_ = now + 3000000000LL;
    }
    std::string name_, problem_;
    std::function<std::unique_ptr<VideoCapture>()> open_;
    std::unique_ptr<VideoCapture> source_;
    std::future<std::unique_ptr<VideoCapture>> opening_;
    Nanoseconds retryAt_ = 0;
};

CaptureFactory platformCapture() {
#ifdef _WIN32
    return {
        [](const EngineConfig& cfg) { return captureScreen(cfg.monitor, cfg.windowHandle, cfg.fps); },
        [](const EngineConfig& cfg) { return captureCamera(cfg.cameraIndex); },
        [](AudioTimeline& mixer, Nanoseconds origin, bool loopback) { return captureAudio(mixer, origin, loopback); },
    };
#else
    throw std::runtime_error("Native capture requires Windows. Use --synthetic for engine verification here.");
#endif
}
}

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
    logLine("engine start: " + std::to_string(cfg.width) + "x" + std::to_string(cfg.height) + " @" + std::to_string(cfg.fps) +
            (cfg.synthetic ? " synthetic" : cfg.windowHandle ? " window" : " monitor " + std::to_string(cfg.monitor)) +
            (cfg.camera ? " camera " + std::to_string(cfg.cameraIndex) : "") + (cfg.microphone ? " mic" : "") +
            (cfg.desktopAudio ? " desktop-audio" : "") + (live ? " studio" : " cli"));
    // Precise frame timing for the whole session, also while the studio is in the background.
    RealtimeScope realtime;
    AudioTimeline mixer;
    const Nanoseconds origin = hostNow();
    const CaptureFactory capture = cfg.synthetic ? CaptureFactory{} : cfg.capture ? *cfg.capture : platformCapture();
    VideoSlot screen("Screen", [&] { return capture.screen(cfg); });
    VideoSlot camera("Camera", [&] { return capture.camera(cfg); });
    std::vector<std::unique_ptr<AudioCapture>> audioInputs;
    std::vector<std::string> audioSetupProblems;
    if (!cfg.synthetic) {
        screen.openNow();
        auto openAudio = [&](bool loopback) {
            // Audio capture retries its own device; only an unexpected setup failure lands here.
            try { audioInputs.push_back(capture.audio(mixer, origin, loopback)); }
            catch (const std::exception& e) {
                audioSetupProblems.push_back(std::string(loopback ? "Desktop sound" : "Microphone") + " unavailable: " + e.what());
                logLine("warning: " + audioSetupProblems.back());
            }
        };
        if (cfg.microphone) openAudio(false);
        if (cfg.desktopAudio) openAudio(true);
        if (cfg.microphone && cfg.desktopAudio) { mixer.gain("microphone", .7f); mixer.gain("desktop", .7f); }
    }
    struct ActiveOutput { OutputSpec spec; std::unique_ptr<OutputWorker> worker; bool closing = false; };
    std::map<std::string, ActiveOutput> outputs;
    std::vector<OutputStatus> completed;
    std::map<std::string, OutputSpec> initial;
    for (size_t n = 0; n < cfg.outputs.size(); ++n) initial["cli-"+std::to_string(n)] = cfg.outputs[n];
    auto describe = [](const OutputStatus& s) {
        return s.id + " " + s.state + " encoder=" + s.encoder + " frames=" + std::to_string(s.frames) +
               " skipped=" + std::to_string(s.dropped) + " reconnects=" + std::to_string(s.reconnects) +
               " convert_ms=" + std::to_string(s.convertMs) + " encode_ms=" + std::to_string(s.encodeMs) +
               (s.error.empty() ? "" : " error=" + s.error);
    };
    auto reconcile = [&](const std::map<std::string, OutputSpec>& desired) {
        for (auto it = outputs.begin(); it != outputs.end();) {
            auto& [id, entry] = *it;
            if (!desired.contains(id) && !entry.closing) { entry.closing = true; entry.worker->requestFinish(); }
            if (entry.closing && entry.worker->finished()) {
                auto status = entry.worker->status(); status.id = id; status.streaming = entry.spec.streaming;
                logLine("output finished: " + describe(status));
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
            // Stream addresses carry keys; the log only says which kind of output started.
            logLine("output start: " + id + (spec.streaming ? " stream " : " recording ") +
                    std::to_string(spec.width) + "x" + std::to_string(spec.height));
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
    // Settle the encoder choice (a real test encode per candidate) before the first frame is due: in the
    // studio while the preview runs, on the command line before the recording clock starts.
    std::thread warm;
    struct JoinWarm { std::thread& t; ~JoinWarm() { if (t.joinable()) t.join(); } } joinWarm{warm};
    if (live) warm = std::thread([&cfg] { warmEncoders(cfg.width, cfg.height, cfg.fps); });
    else if (std::any_of(cfg.outputs.begin(), cfg.outputs.end(), [](const auto& o) { return o.encoder == "auto"; }))
        warmEncoders(cfg.width, cfg.height, cfg.fps);
    LiveState state;
    state.running = true;
    if (live) live->publish(state);
    // The clock starts after setup, so slow device or encoder start-up is not charged to the first frames.
    const Nanoseconds start = hostNow();
    reconcile(live ? live->commands().outputs : initial);
    // Delay playout so audio packets and captured video can arrive before their shared deadline.
    constexpr Nanoseconds latency = 120000000;
    const Nanoseconds frameTime = 1000000000LL/cfg.fps;
    // The studio's monitors show small renders; full-size frames are only composed for outputs.
    const double previewScale = std::min(1., 960./cfg.width);
    const int previewWidth = std::max(2, static_cast<int>(std::lround(cfg.width*previewScale/2))*2);
    const int previewHeight = std::max(2, static_cast<int>(std::lround(cfg.height*previewScale/2))*2);
    auto previewOf = [&](const Scene& scene, Canvas canvas, const std::map<std::string, std::shared_ptr<const Image>>& images) {
        const bool landscape = canvas == Canvas::horizontal;
        return std::make_shared<const Image>(compose(scene, canvas, landscape ? previewWidth : previewHeight,
                                                     landscape ? previewHeight : previewWidth, images));
    };
    std::map<Canvas, std::shared_ptr<const Image>> lastRendered;
    int64_t lastPublished = -1000;
    double composeMs = 0; int64_t composed = 0;
    uint64_t lateAtLastLog = 0;
    const int64_t count = static_cast<int64_t>(std::ceil(cfg.seconds*cfg.fps));
    for (int64_t frame = 0; frame < count && !stop; ++frame) {
        // Samples are counted from the session origin (audio devices stamp against it); frames from start.
        const Nanoseconds time = start + frame*frameTime;
        // Sleep towards the absolute deadline, never a fixed interval, so timing errors do not accumulate.
        for (Nanoseconds now = hostNow(); !stop && now < time+latency; now = hostNow()) {
            screen.poll(now);
            const Nanoseconds left = time+latency-now;
            std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<Nanoseconds>(left, 2000000)));
        }
        if (stop) break;
        Nanoseconds now = hostNow();
        // More than two frames behind real time: repeat the last picture instead of composing a new one,
        // so audio and output timing stay continuous and the engine catches up instead of drifting further
        // behind (which used to starve the capture history and overflow the recording queue).
        const bool late = now > time+latency+2*frameTime;
        if (late) ++state.lateFrames;
        const int64_t sample = samplesAtFrame(frame, cfg.fps) + (start-origin)/1000000000LL*sampleRate +
                               (start-origin)%1000000000LL*sampleRate/1000000000LL;
        const int64_t outputSample = samplesAtFrame(frame, cfg.fps);
        const int samples = static_cast<int>(samplesAtFrame(frame+1, cfg.fps)-outputSample);
        std::map<std::string, std::shared_ptr<const Image>> images;
        if (cfg.synthetic) {
            images["screen"] = testPattern(frame, cfg.fps);
            images["camera"] = images["screen"];
            std::vector<float> tone(static_cast<size_t>(samples)*2);
            for (int64_t n = outputSample; n < outputSample+samples; ++n) {
                const double value = n%sampleRate < sampleRate/10 ? .4*std::sin(2*3.141592653589793*1000*n/sampleRate) : 0;
                tone[(n-outputSample)*2] = tone[(n-outputSample)*2+1] = static_cast<float>(value);
            }
            mixer.push("tone", sample, tone.data(), samples);
        } else {
            screen.poll(now);
            screen.tick(now);
            images["screen"] = screen.at(time, now);
            if (cfg.camera) { camera.tick(now); images["camera"] = camera.at(time, now); }
        }
        if (frame%10 == 0 && !cfg.synthetic) {
            std::vector<std::string> warnings = audioSetupProblems;
            for (const auto* slot : {&screen, &camera}) {
                if (slot == &camera && !cfg.camera) continue;
                auto problem = slot->problem();
                if (!problem.empty()) warnings.push_back(std::move(problem));
            }
            for (auto& input : audioInputs) { auto problem = input->problem(); if (!problem.empty()) warnings.push_back(std::move(problem)); }
            if (warnings != state.warnings) {
                if (!live) for (const auto& warning : warnings) std::cerr << "warning: " << warning << '\n';
                if (warnings.empty()) logLine("all sources working");
                for (const auto& warning : warnings)
                    if (std::find(state.warnings.begin(), state.warnings.end(), warning) == state.warnings.end())
                        logLine("source notice: " + warning);
                state.warnings = std::move(warnings);
            }
        }
        auto commands = live ? live->commands() : LiveCommands{};
        if (live) {
            mixer.gain("microphone", commands.microphone); mixer.gain("desktop", commands.desktop);
            mixer.gain("tone", commands.microphone);
            reconcile(commands.outputs);
        }
        auto audio = mixer.mix(sample, samples);
        if (live && commands.master != 1) {
            auto adjusted = std::make_shared<std::vector<float>>(*audio);
            for (auto& value : *adjusted) value = std::clamp(value*commands.master, -1.f, 1.f);
            audio = adjusted;
        }
        const Nanoseconds composeStart = hostNow();
        std::map<Canvas, std::shared_ptr<const Image>> rendered;
        const Scene& program = live ? commands.program : cfg.scene;
        auto renderCanvas = [&](Canvas canvas) {
            auto& image = rendered[canvas];
            if (!image) {
                const auto previous = lastRendered.find(canvas);
                if (late && previous != lastRendered.end()) image = previous->second;
                else image = lastRendered[canvas] = std::make_shared<const Image>(compose(program, canvas,
                    canvas == Canvas::horizontal ? cfg.width : cfg.height,
                    canvas == Canvas::horizontal ? cfg.height : cfg.width, images));
            }
            return image;
        };
        for (auto& [id, output] : outputs) {
            (void)id;
            if (!output.closing) output.worker->submit({frame, outputSample, renderCanvas(output.spec.canvas), audio});
        }
        // Drop canvases no output uses any more, so a later late frame never repeats a stale picture.
        for (auto it = lastRendered.begin(); it != lastRendered.end();)
            it = rendered.contains(it->first) ? std::next(it) : lastRendered.erase(it);
        // About 15 monitor updates per second; when behind, only every tenth frame so the UI still moves.
        if (live && frame%2 == 0 && (!late || frame-lastPublished >= 10)) {
            lastPublished = frame;
            state.program.clear(); state.preview.clear(); state.sources = images;
            for (auto canvas : {Canvas::horizontal, Canvas::vertical}) {
                state.program[canvas] = previewOf(program, canvas, images);
                if (commands.preview) state.preview[canvas] = previewOf(*commands.preview, canvas, images);
            }
            state.frame = frame;
            state.microphone = mixer.level(cfg.synthetic ? "tone" : "microphone", sample, samples);
            state.desktop = mixer.level("desktop", sample, samples);
            double square = 0; for (float value : *audio) square += value*value;
            state.master = static_cast<float>(std::sqrt(square/audio->size()));
            state.outputs = report(); live->publish(state);
        } else if (!live && frame%cfg.fps == 0) {
            auto statuses = report();
            if (!statuses.empty() && std::all_of(statuses.begin(), statuses.end(), [](const auto& s) { return s.state == "failed"; })) break;
        }
        composeMs += (hostNow()-composeStart)/1e6; ++composed;
        if (frame > 0 && frame%(cfg.fps*10) == 0) {
            std::string line = "stats: frame " + std::to_string(frame) + " late +" + std::to_string(state.lateFrames-lateAtLastLog) +
                               " render_ms " + std::to_string(composeMs/composed);
            for (auto& [id, output] : outputs) { auto status = output.worker->status(); status.id = id; line += " | " + describe(status); }
            logLine(line);
            lateAtLastLog = state.lateFrames; composeMs = 0; composed = 0;
        }
    }
    audioInputs.clear(); camera.close(); screen.close();
    for (auto& [id, output] : outputs) { (void)id; output.worker->requestFinish(); }
    for (auto& [id, output] : outputs) { (void)id; output.worker->finish(); }
    int result = 0;
    state.outputs = report(); state.running = false; state.done = true;
    for (const auto& s : state.outputs) {
        if (std::none_of(completed.begin(), completed.end(), [&](const auto& c) { return c.id == s.id; }))
            logLine("output finished: " + describe(s));
        if (s.state == "failed" || s.frames == 0) result = 1;
    }
    logLine("engine stop: late frames " + std::to_string(state.lateFrames));
    if (live) live->publish(state);
    return result;
}
}
