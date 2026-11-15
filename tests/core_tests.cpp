#include "framecast/core.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace framecast;
namespace {
int failures = 0;
void check(bool result, const char* name) {
    std::cout << (result ? "PASS " : "FAIL ") << name << '\n';
    if (!result) ++failures;
}
int red(const Image& image, int x, int y) { return image.bgra[(y*image.width+x)*4+2]; }
}
int main() {
    try {
        int64_t total = 0;
        for (int64_t n = 0; n < 100000; ++n) total += samplesAtFrame(n+1, 29)-samplesAtFrame(n, 29);
        check(total == 100000LL*sampleRate/29, "rational frame/sample clock has no accumulated rounding drift");
        auto source = std::make_shared<Image>(80, 40);
        for (size_t n = 0; n < source->bgra.size(); n += 4) source->bgra[n+2] = 255;
        Scene scene{{{"source", {}, Fit::fit}}, {{"source", {}, Fit::fill}}};
        auto fit = compose(scene, Canvas::horizontal, 40, 80, {{"source", source}});
        auto fill = compose(scene, Canvas::vertical, 40, 80, {{"source", source}});
        check(red(fit, 20, 0) == 0 && red(fit, 20, 40) == 255, "fit preserves source aspect and letterboxes");
        check(red(fill, 20, 0) == 255 && red(fill, 20, 79) == 255, "fill crops to portrait without bars");
        scene.horizontal[0].mask = Mask::circle;
        scene.horizontal[0].fit = Fit::stretch;
        auto circle = compose(scene, Canvas::horizontal, 40, 40, {{"source", source}});
        check(red(circle, 0, 0) == 0 && red(circle, 20, 20) == 255, "circle mask clips corners");
        scene.horizontal[0].mask = Mask::rounded; scene.horizontal[0].radius = .3;
        auto rounded = compose(scene, Canvas::horizontal, 40, 40, {{"source", source}});
        check(red(rounded, 0, 0) == 0 && red(rounded, 20, 0) == 255, "rounded mask keeps edge centre");
        scene.horizontal[0].opacity = .5;
        auto faded = compose(scene, Canvas::horizontal, 40, 40, {{"source", source}});
        check(red(faded, 20, 20) == 127, "layer opacity is applied");
        check(red(compose(scene, Canvas::horizontal, 40, 40, {}), 20, 20) == 0, "missing sources produce black rather than a stale frame");
        auto layout = screenCameraScene();
        check(layout.horizontal.size() == 2 && layout.vertical.size() == 2 && layout.vertical[1].rect.y == .44,
              "screen plus camera preserves separate Mac canvas geometry");
        {
            // A mapped capture buffer: rows are further apart than the picture is wide.
            const int w = 6, h = 4; const size_t pitch = 40;
            std::vector<uint8_t> mapped(pitch*h, 0xEE);
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) for (int c = 0; c < 4; ++c)
                mapped[y*pitch + x*4 + c] = static_cast<uint8_t>(y*10 + x + c);
            auto copy = copyCapturedFrame(mapped.data(), pitch, w, h);
            bool exact = copy->width == w && copy->height == h && copy->bgra.size() == size_t(w)*h*4;
            for (int y = 0; y < h && exact; ++y) for (int x = 0; x < w; ++x) for (int c = 0; c < 4; ++c)
                exact = exact && copy->bgra[(y*w+x)*4+c] == y*10 + x + c;
            check(exact, "captured frames are copied row by row without the row padding");
            // A 4K-class frame is averaged 2:1: a checkerboard of 0 and 200 becomes flat 100.
            const int bw = 3202, bh = 4;
            std::vector<uint8_t> big(size_t(bw)*4*bh);
            for (int y = 0; y < bh; ++y) for (int x = 0; x < bw; ++x) for (int c = 0; c < 4; ++c)
                big[(size_t(y)*bw + x)*4 + c] = (x+y)%2 ? 200 : 0;
            auto half = copyCapturedFrame(big.data(), size_t(bw)*4, bw, bh);
            bool averaged = half->width == bw/2 && half->height == bh/2;
            for (uint8_t value : half->bgra) averaged = averaged && value == 100;
            check(averaged, "4K-class captures are averaged down 2:1 while copying");
            bool rejected = false;
            try { copyCapturedFrame(mapped.data(), 8, w, h); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "a capture buffer narrower than its picture is rejected");
        }
        FrameHistory history;
        history.push({1000000000, source});
        check(!history.at(999999999), "capture history never selects a future frame");
        check(history.at(1000000000) == source, "capture history selects frame at deadline");
        check(!history.at(4000000000), "capture history expires a frozen source");
        check(history.at(4000000000, false) == source, "a healthy static screen remains visible without new WGC frames");
        history.push({500000000, std::make_shared<Image>(2, 2)});
        check(history.at(1000000000) == source, "out of order capture timestamps do not replace current frames");
        AudioTimeline mixer;
        std::vector<float> a(400, .25f), b(400, .5f);
        mixer.push("mic", 100, a.data(), 200); mixer.push("desktop", 100, b.data(), 200);
        check((*mixer.mix(100, 200))[0] == .75f, "capture sources mix on the common timeline");
        check((*mixer.mix(0, 50))[0] == 0, "uncaptured audio is silence");
        mixer.gain("desktop", 0);
        check((*mixer.mix(100, 200))[0] == .25f, "muting one source preserves the other");
        mixer.push("mic", 1000, a.data(), 200);
        check((*mixer.mix(500, 100))[0] == 0, "device gaps never replay old samples");
        mixer.push("mic", 500000, a.data(), 200);
        check((*mixer.mix(100, 1))[0] == 0, "ring wrap never mistakes old data for current samples");
        AudioTimeline clipping;
        clipping.push("a", 0, b.data(), 200); clipping.gain("a", 4);
        check((*clipping.mix(0, 1))[0] == 1, "master mix clamps clipping");
        AudioTimeline jitter;
        jitter.push("a", 0, a.data(), 200); jitter.push("a", 202, b.data(), 200);
        check((*jitter.mix(200, 1))[0] == .5f, "small capture timestamp jitter stays continuous");
        bool rejected = false;
        try { OutputWorker invalid({Canvas::horizontal, "invalid.mkv", "auto", 321, 180}); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "odd encoder dimensions fail before spawning an output thread");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; ++failures; }
    return failures ? 1 : 0;
}
