#include "framecast/core.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <thread>
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
    // Opaque black. Filling whole pixels lets the compiler write wide words instead of one byte in four.
    bgra.resize(static_cast<size_t>(w) * h * 4);
    const uint8_t black[4] = {0, 0, 0, 255};
    uint32_t pixel; std::memcpy(&pixel, black, 4);
    auto* data = bgra.data();
    for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; ++i) std::memcpy(data + i*4, &pixel, 4);
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
namespace {
// A small persistent pool for splitting one frame's rows across cores. Creating threads per frame
// costs more than composing a band; one job runs at a time and the caller works on a share itself.
class RowPool {
public:
    static RowPool& shared() { static RowPool pool; return pool; }
    void run(int count, const std::function<void(int)>& task) {
        if (count <= 1 || workers_.empty()) { for (int n = 0; n < count; ++n) task(n); return; }
        std::lock_guard job(jobMutex_);
        {
            std::lock_guard guard(mutex_);
            task_ = &task; next_ = 0; count_ = count; pending_ = count; ++generation_;
        }
        wake_.notify_all();
        work();
        std::unique_lock lock(mutex_);
        done_.wait(lock, [&] { return pending_ == 0; });
        task_ = nullptr;
    }
    int threads() const { return static_cast<int>(workers_.size()) + 1; }
    ~RowPool() {
        { std::lock_guard guard(mutex_); stopping_ = true; }
        wake_.notify_all();
        for (auto& worker : workers_) worker.join();
    }
private:
    RowPool() {
        // Leave cores for capture, encoders and the UI; four bands already fit a 1080p frame in a few ms.
        const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
        const unsigned extra = std::min(3u, hardware > 2 ? hardware - 2 : 0u);
        for (unsigned n = 0; n < extra; ++n) workers_.emplace_back([this] { loop(); });
    }
    void loop() {
        uint64_t seen = 0;
        while (true) {
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return stopping_ || generation_ != seen; });
                if (stopping_) return;
                seen = generation_;
            }
            work();
        }
    }
    void work() {
        while (true) {
            int index; const std::function<void(int)>* task;
            {
                std::lock_guard guard(mutex_);
                if (!task_ || next_ >= count_) return;
                index = next_++; task = task_;
            }
            (*task)(index);
            std::lock_guard guard(mutex_);
            if (--pending_ == 0) done_.notify_all();
        }
    }
    std::vector<std::thread> workers_;
    std::mutex jobMutex_, mutex_;
    std::condition_variable wake_, done_;
    const std::function<void(int)>* task_ = nullptr;
    int next_ = 0, count_ = 0, pending_ = 0;
    uint64_t generation_ = 0;
    bool stopping_ = false;
};

// Everything about a layer that depends only on the column or only on the row, computed once per
// frame instead of once per pixel. The per-pixel arithmetic below is unchanged, so output is identical
// to the original per-pixel compositor (tests/compose_perf_tests.cpp checks this).
struct PreparedLayer {
    const Image* src = nullptr;
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    Mask mask = Mask::none;
    double limit = 0;                 // circle: 1, rounded: radius squared
    std::vector<int> column, row;     // source index, or -1 outside the picture
    std::vector<double> columnMask, rowMask;
    double alpha[256];                // source alpha byte * layer opacity
    bool opaque = false;              // opacity is exactly 1
};

std::vector<PreparedLayer> prepare(const std::vector<Layer>& layers, int width, int height,
                                   const std::map<std::string, std::shared_ptr<const Image>>& images) {
    std::vector<PreparedLayer> out;
    out.reserve(layers.size());
    for (const auto& l : layers) {
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
        PreparedLayer p;
        p.src = &src;
        p.x0 = static_cast<int>(std::clamp(left, 0., double(width)));
        p.y0 = static_cast<int>(std::clamp(top, 0., double(height)));
        p.x1 = static_cast<int>(std::clamp(std::ceil(left+fw), 0., double(width)));
        p.y1 = static_cast<int>(std::clamp(std::ceil(top+fh), 0., double(height)));
        if (p.x1 <= p.x0 || p.y1 <= p.y0) continue;
        const double radius = std::clamp(l.radius, 0., .5) * std::min(fw, fh);
        p.mask = l.mask == Mask::rounded && radius <= 0 ? Mask::none : l.mask;
        p.limit = p.mask == Mask::circle ? 1 : radius*radius;
        p.column.resize(p.x1 - p.x0); p.columnMask.resize(p.x1 - p.x0);
        for (int x = p.x0; x < p.x1; ++x) {
            const double lx = x+.5-left;
            const double u = (lx-ox)/sx;
            int ix = -1;
            if (u >= 0 && u < src.width) { ix = static_cast<int>(u); if (l.mirrored) ix = src.width-1-ix; }
            p.column[x - p.x0] = ix;
            if (p.mask == Mask::circle) p.columnMask[x - p.x0] = std::pow((lx-fw/2)/(fw/2), 2);
            else if (p.mask == Mask::rounded) { const double dx = std::max({radius-lx, lx-(fw-radius), 0.}); p.columnMask[x - p.x0] = dx*dx; }
        }
        p.row.resize(p.y1 - p.y0); p.rowMask.resize(p.y1 - p.y0);
        for (int y = p.y0; y < p.y1; ++y) {
            const double ly = y+.5-top;
            const double v = (ly-oy)/sy;
            p.row[y - p.y0] = v >= 0 && v < src.height ? static_cast<int>(v) : -1;
            if (p.mask == Mask::circle) p.rowMask[y - p.y0] = std::pow((ly-fh/2)/(fh/2), 2);
            else if (p.mask == Mask::rounded) { const double dy = std::max({radius-ly, ly-(fh-radius), 0.}); p.rowMask[y - p.y0] = dy*dy; }
        }
        const double opacity = std::clamp(l.opacity, 0., 1.);
        p.opaque = opacity == 1;
        for (int a = 0; a < 256; ++a) p.alpha[a] = a/255. * opacity;
        out.push_back(std::move(p));
    }
    return out;
}

void drawRows(const std::vector<PreparedLayer>& layers, Image& out, int rowBegin, int rowEnd) {
    for (const auto& p : layers) {
        const int begin = std::max(rowBegin, p.y0), end = std::min(rowEnd, p.y1);
        const int sourceWidth = p.src->width;
        for (int y = begin; y < end; ++y) {
            const int iy = p.row[y - p.y0];
            if (iy < 0) continue;
            const double rowMask = p.mask == Mask::none ? 0 : p.rowMask[y - p.y0];
            const uint8_t* source = p.src->bgra.data() + static_cast<size_t>(iy)*sourceWidth*4;
            uint8_t* target = out.bgra.data() + static_cast<size_t>(y)*out.width*4;
            for (int x = p.x0; x < p.x1; ++x) {
                const int column = x - p.x0;
                if (p.mask != Mask::none && p.columnMask[column]+rowMask > p.limit) continue;
                const int ix = p.column[column];
                if (ix < 0) continue;
                const uint8_t* s = source + static_cast<size_t>(ix)*4;
                uint8_t* d = target + static_cast<size_t>(x)*4;
                if (p.opaque && s[3] == 255) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; continue; }
                const double alpha = p.alpha[s[3]];
                if (alpha == 0) continue;
                for (int c = 0; c < 3; ++c) d[c] = static_cast<uint8_t>(s[c]*alpha + d[c]*(1-alpha));
            }
        }
    }
}
}

Image compose(const Scene& scene, Canvas canvas, int width, int height,
              const std::map<std::string, std::shared_ptr<const Image>>& images) {
    Image out(width, height);
    const auto layers = prepare(canvas == Canvas::horizontal ? scene.horizontal : scene.vertical, width, height, images);
    if (layers.empty()) return out;
    // Small canvases (thumbnails, previews) are cheaper inline than handing them to other threads.
    auto& pool = RowPool::shared();
    const int bands = static_cast<int>(width)*height < 300000 ? 1 : pool.threads();
    const int step = (height + bands - 1) / bands;
    pool.run(bands, [&](int band) { drawRows(layers, out, band*step, std::min(height, (band+1)*step)); });
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
