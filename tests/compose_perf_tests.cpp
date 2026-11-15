// Full-HD compositor checks: the fast compositor must produce exactly the pixels of the original
// per-pixel reference, and both canvases of a 1920x1080 scene must compose well inside one 30 fps frame.
// The original compositor took several hundred milliseconds per frame at this size, which made the
// studio fall behind real time on laptops while the 640x360 tests still passed.
#include "framecast/core.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>

using namespace framecast;
namespace {
int failures = 0;
void check(bool result, const std::string& name) {
    std::cout << (result ? "PASS " : "FAIL ") << name << '\n';
    if (!result) ++failures;
}

// The compositor as it was before the fast path, kept verbatim as the pixel reference.
Image reference(const Scene& scene, Canvas canvas, int width, int height,
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

std::shared_ptr<Image> noise(int width, int height, std::mt19937& rng, bool translucent) {
    auto image = std::make_shared<Image>(width, height);
    std::uniform_int_distribution<int> byte(0, 255);
    for (size_t n = 0; n < image->bgra.size(); n += 4) {
        image->bgra[n] = static_cast<uint8_t>(byte(rng));
        image->bgra[n+1] = static_cast<uint8_t>(byte(rng));
        image->bgra[n+2] = static_cast<uint8_t>(byte(rng));
        image->bgra[n+3] = translucent ? static_cast<uint8_t>(byte(rng)) : 255;
    }
    return image;
}

Scene laptopScene() {
    // Screen + camera with a circle bubble and a mirrored, half-transparent rounded tile: every compositor path.
    Scene scene = screenCameraScene();
    scene.horizontal.push_back({"camera", {.70, .05, .2, .2*16/9}, Fit::fill, Mask::circle});
    scene.vertical.push_back({"overlay", {.1, .05, .8, .2}, Fit::fit, Mask::rounded, .2, .6, true});
    scene.horizontal.push_back({"overlay", {-.05, .8, .5, .3}, Fit::stretch, Mask::rounded, .5, .6, true});
    return scene;
}
}

int main() {
    std::mt19937 rng(7);
    std::map<std::string, std::shared_ptr<const Image>> sources{
        {"screen", noise(2560, 1600, rng, false)},
        {"camera", noise(1920, 1080, rng, false)},
        {"overlay", noise(400, 120, rng, true)},
    };

    // Exactness on many random layers and sizes, including odd sizes and layers hanging off the canvas.
    std::uniform_real_distribution<double> pos(-.3, 1.1), size(.02, 1.2), unit(0, 1);
    const char* names[] = {"screen", "camera", "overlay"};
    bool identical = true;
    for (int trial = 0; trial < 60 && identical; ++trial) {
        Scene scene;
        for (int n = 0; n < 4; ++n) {
            Layer layer{names[rng()%3], {pos(rng), pos(rng), size(rng), size(rng)},
                        static_cast<Fit>(rng()%3), static_cast<Mask>(rng()%3), unit(rng)*.5,
                        n%2 ? unit(rng) : 1., static_cast<bool>(rng()%2)};
            scene.horizontal.push_back(layer);
            scene.vertical.push_back(layer);
        }
        const int w = 64 + static_cast<int>(rng()%400), h = 64 + static_cast<int>(rng()%400);
        for (auto canvas : {Canvas::horizontal, Canvas::vertical})
            identical = identical && compose(scene, canvas, w, h, sources).bgra == reference(scene, canvas, w, h, sources).bgra;
    }
    const Scene scene = laptopScene();
    identical = identical &&
        compose(scene, Canvas::horizontal, 1920, 1080, sources).bgra == reference(scene, Canvas::horizontal, 1920, 1080, sources).bgra &&
        compose(scene, Canvas::vertical, 1080, 1920, sources).bgra == reference(scene, Canvas::vertical, 1080, 1920, sources).bgra;
    check(identical, "fast compositor output is pixel-identical to the reference compositor");

    using Clock = std::chrono::steady_clock;
    auto timeIt = [&](auto&& composeFn, int iterations) {
        composeFn(scene, Canvas::horizontal, 1920, 1080, sources);
        const auto start = Clock::now();
        for (int n = 0; n < iterations; ++n) {
            composeFn(scene, Canvas::horizontal, 1920, 1080, sources);
            composeFn(scene, Canvas::vertical, 1080, 1920, sources);
        }
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count() / iterations;
    };
    const double before = timeIt(reference, 3);
    const double after = timeIt(compose, 30);
    std::cout << "1080p landscape + portrait: reference " << before << " ms, fast " << after << " ms per frame\n";
    // A 30 fps frame is 33 ms and it must also leave time for capture and preview. Override for slow machines.
    double budget = 16;
    if (const char* value = std::getenv("FRAMECAST_COMPOSE_BUDGET_MS")) budget = std::atof(value);
    check(after <= budget, "both 1080p canvases compose within " + std::to_string(budget) + " ms");
    check(after * 4 <= before, "fast compositor is at least four times faster than the reference");
    return failures ? 1 : 0;
}
