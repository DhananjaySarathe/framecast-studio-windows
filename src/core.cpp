#include "framecast/core.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace framecast {
Nanoseconds hostNow() {
#ifdef _WIN32
    static const int64_t frequency = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart; }();
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    return (q.QuadPart / frequency) * 1000000000LL + (q.QuadPart % frequency) * 1000000000LL / frequency;
#else
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}
int64_t samplesAtFrame(int64_t frame, int fps) {
    if (fps < 1 || fps > 120 || frame < 0) throw std::invalid_argument("Invalid frame clock");
    return frame * sampleRate / fps;
}
Image::Image(int w, int h) : width(w), height(h) {
    if (w < 1 || h < 1 || w > 8192 || h > 8192) throw std::invalid_argument("Invalid image size");
    bgra.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 3; i < bgra.size(); i += 4) bgra[i] = 255;
}
void FrameHistory::push(VideoFrame frame) {
    std::lock_guard guard(mutex_);
    if (!frame.image) return;
    if (!frames_.empty() && frame.timestamp < frames_.back().timestamp) return;
    frames_.push_back(std::move(frame));
    while (frames_.size() > 32) frames_.pop_front();
}
std::shared_ptr<const Image> FrameHistory::at(Nanoseconds timestamp, bool expire) const {
    std::lock_guard guard(mutex_);
    for (auto i = frames_.rbegin(); i != frames_.rend(); ++i)
        if (i->timestamp <= timestamp) return !expire || timestamp - i->timestamp <= 2000000000LL ? i->image : nullptr;
    return nullptr;
}
Scene screenScene() { return {{{"screen", {}, Fit::fit}}, {{"screen", {}, Fit::fit}}}; }
Scene screenCameraScene() {
    // Geometry and masks match LayoutTemplates.screenCam in the Mac app.
    const double mH = .03, mHy = .03 * 16 / 9, mV = .04, mVy = .04 * 9 / 16;
    return {
        {{"screen", {mH, mHy, 1-2*mH, 1-2*mHy}, Fit::fit, Mask::rounded, .035},
         {"camera", {1-mH-.17-.02, 1-mHy-.17*16/9-.035, .17, .17*16/9}, Fit::fill, Mask::rounded, .22}},
        {{"screen", {mV, mVy+.03, 1-2*mV, .36}, Fit::fit, Mask::rounded, .035},
         {"camera", {mV, .44, 1-2*mV, .46}, Fit::fill, Mask::rounded, .035}}
    };
}
Image compose(const Scene& scene, Canvas canvas, int width, int height,
              const std::map<std::string, std::shared_ptr<const Image>>& images) {
    Image out(width, height);
    for (const auto& l : canvas == Canvas::horizontal ? scene.horizontal : scene.vertical) {
        const auto found = images.find(l.source);
        if (found == images.end() || !found->second) continue;
        const Image& src = *found->second;
        if (src.width < 1 || src.height < 1 || src.bgra.size() != static_cast<size_t>(src.width)*src.height*4) continue;
        const double fw = l.rect.width * width, fh = l.rect.height * height;
        if (fw <= 0 || fh <= 0 || !std::isfinite(fw) || !std::isfinite(fh) ||
            !std::isfinite(l.rect.x) || !std::isfinite(l.rect.y)) continue;
        double sx = fw / src.width, sy = fh / src.height;
        if (l.fit != Fit::stretch) sx = sy = l.fit == Fit::fill ? std::max(sx, sy) : std::min(sx, sy);
        const double left = l.rect.x * width, top = l.rect.y * height;
        const double ox = (fw - src.width*sx)/2, oy = (fh - src.height*sy)/2;
        // Clamp in floating point before converting untrusted scene coordinates to integers.
        const int x0 = static_cast<int>(std::clamp(left, 0., double(width)));
        const int y0 = static_cast<int>(std::clamp(top, 0., double(height)));
        const int x1 = static_cast<int>(std::clamp(std::ceil(left+fw), 0., double(width)));
        const int y1 = static_cast<int>(std::clamp(std::ceil(top+fh), 0., double(height)));
        const double radius = std::clamp(l.radius, 0., .5) * std::min(fw, fh);
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
            const double lx = x+.5-left, ly = y+.5-top;
            if (l.mask == Mask::circle && std::pow((lx-fw/2)/(fw/2), 2)+std::pow((ly-fh/2)/(fh/2), 2) > 1) continue;
            if (l.mask == Mask::rounded && radius > 0) {
                const double dx = std::max({radius-lx, lx-(fw-radius), 0.});
                const double dy = std::max({radius-ly, ly-(fh-radius), 0.});
                if (dx*dx+dy*dy > radius*radius) continue;
            }
            double u = (lx-ox)/sx, v = (ly-oy)/sy;
            if (u < 0 || v < 0 || u >= src.width || v >= src.height) continue;
            int ix = static_cast<int>(u), iy = static_cast<int>(v);
            if (l.mirrored) ix = src.width-1-ix;
            const size_t a = (static_cast<size_t>(iy)*src.width+ix)*4, b = (static_cast<size_t>(y)*width+x)*4;
            const double alpha = src.bgra[a+3]/255. * std::clamp(l.opacity, 0., 1.);
            for (int c = 0; c < 3; ++c) out.bgra[b+c] = static_cast<uint8_t>(src.bgra[a+c]*alpha + out.bgra[b+c]*(1-alpha));
        }
    }
    return out;
}
std::shared_ptr<const Image> testPattern(int64_t frame, int fps, int width, int height) {
    auto image = std::make_shared<Image>(width, height);
    bool flash = frame % fps == 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        size_t p = (static_cast<size_t>(y)*width+x)*4;
        image->bgra[p] = flash ? 255 : 20;
        image->bgra[p+1] = flash ? 255 : static_cast<uint8_t>(30 + (x+frame*3)%90);
        image->bgra[p+2] = flash ? 255 : static_cast<uint8_t>(30+y%60);
    }
    return image;
}
void AudioTimeline::push(const std::string& source, int64_t start, const float* samples, int frames) {
    if (frames < 0 || (frames && !samples)) throw std::invalid_argument("Invalid audio block");
    std::lock_guard guard(mutex_);
    auto& ring = sources_[source];
    // Small device timestamp jitter must not leave holes in an otherwise continuous stream.
    if (ring.end != INT64_MIN && std::abs(start-ring.end) <= 96) start = ring.end;
    const int64_t capacity = static_cast<int64_t>(ring.tags.size());
    for (int n = 0; n < frames; ++n) {
        int64_t index = start+n, slot = (index%capacity+capacity)%capacity;
        ring.tags[slot] = index;
        ring.data[slot*2] = samples[n*2]; ring.data[slot*2+1] = samples[n*2+1];
    }
    ring.end = start+frames;
}
void AudioTimeline::gain(const std::string& source, float value) {
    if (!std::isfinite(value) || value < 0 || value > 4) throw std::invalid_argument("Invalid audio gain");
    std::lock_guard guard(mutex_); sources_[source].gain = value;
}
std::shared_ptr<const std::vector<float>> AudioTimeline::mix(int64_t start, int frames) {
    if (frames < 0 || frames > sampleRate*4) throw std::invalid_argument("Invalid audio mix size");
    auto out = std::make_shared<std::vector<float>>(static_cast<size_t>(frames)*2, 0);
    std::lock_guard guard(mutex_);
    for (const auto& [id, ring] : sources_) {
        (void)id;
        const int64_t capacity = static_cast<int64_t>(ring.tags.size());
        for (int n = 0; n < frames; ++n) {
            const int64_t index = start+n, slot = (index%capacity+capacity)%capacity;
            if (ring.tags[slot] != index || index < ring.end-capacity) continue;
            (*out)[n*2] += ring.data[slot*2]*ring.gain; (*out)[n*2+1] += ring.data[slot*2+1]*ring.gain;
        }
    }
    for (float& sample : *out) sample = std::isfinite(sample) ? std::clamp(sample, -1.f, 1.f) : 0;
    return out;
}
float AudioTimeline::level(const std::string& source, int64_t start, int frames) {
    if (frames <= 0) return 0;
    std::lock_guard guard(mutex_);
    const auto found = sources_.find(source);
    if (found == sources_.end()) return 0;
    const auto& r = found->second;
    const int64_t capacity = static_cast<int64_t>(r.tags.size());
    double sum = 0;
    for (int n = 0; n < frames; ++n) {
        const auto index = start+n, slot = (index%capacity+capacity)%capacity;
        if (r.tags[slot] != index || index < r.end-capacity) continue;
        sum += (r.data[slot*2]*r.data[slot*2]+r.data[slot*2+1]*r.data[slot*2+1])*.5*r.gain*r.gain;
    }
    return static_cast<float>(std::sqrt(sum/frames));
}
void LiveControl::setScenes(Scene program, std::optional<Scene> preview) {
    std::lock_guard guard(mutex_); commands_.program = std::move(program); commands_.preview = std::move(preview);
}
void LiveControl::setOutputs(std::map<std::string, OutputSpec> outputs) {
    std::lock_guard guard(mutex_); commands_.outputs = std::move(outputs);
}
void LiveControl::setGains(float microphone, float desktop, float master) {
    for (auto value : {microphone, desktop, master})
        if (!std::isfinite(value) || value < 0 || value > 4) throw std::invalid_argument("Invalid live audio gain");
    std::lock_guard guard(mutex_); commands_.microphone = microphone; commands_.desktop = desktop; commands_.master = master;
}
LiveCommands LiveControl::commands() const { std::lock_guard guard(mutex_); return commands_; }
LiveState LiveControl::state() const { std::lock_guard guard(mutex_); return state_; }
void LiveControl::publish(LiveState state) { std::lock_guard guard(mutex_); state_ = std::move(state); }
}
