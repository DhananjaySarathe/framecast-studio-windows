#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace framecast {
constexpr int sampleRate = 48000;
using Nanoseconds = int64_t;
Nanoseconds hostNow();
int64_t samplesAtFrame(int64_t frame, int fps);

enum class Canvas { horizontal, vertical };
enum class Fit { fill, fit, stretch };
enum class Mask { none, circle, rounded };
struct Rect { double x = 0, y = 0, width = 1, height = 1; };
struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> bgra;
    Image() = default;
    Image(int w, int h);
};
struct VideoFrame { Nanoseconds timestamp = 0; std::shared_ptr<const Image> image; };

// Source buffers are bounded; never expose a future frame or a stale frozen picture.
class FrameHistory {
public:
    void push(VideoFrame frame);
    std::shared_ptr<const Image> at(Nanoseconds timestamp, bool expire = true) const;
private:
    mutable std::mutex mutex_;
    std::deque<VideoFrame> frames_;
};

struct Layer {
    std::string source;
    Rect rect;
    Fit fit = Fit::fill;
    Mask mask = Mask::none;
    double radius = 0, opacity = 1;
    bool mirrored = false;
};
struct Scene { std::vector<Layer> horizontal, vertical; };
Scene screenCameraScene();
Scene screenScene();
Image compose(const Scene& scene, Canvas canvas, int width, int height,
              const std::map<std::string, std::shared_ptr<const Image>>& images);
std::shared_ptr<const Image> testPattern(int64_t frame, int fps, int width = 640, int height = 360);

// Each capture device writes on the same sample timeline. Missing samples are silence.
class AudioTimeline {
public:
    void push(const std::string& source, int64_t start, const float* stereo, int frames);
    void gain(const std::string& source, float gain);
    std::shared_ptr<const std::vector<float>> mix(int64_t start, int frames);
    float level(const std::string& source, int64_t start, int frames);
private:
    struct Ring {
        std::vector<float> data = std::vector<float>(sampleRate * 4 * 2);
        std::vector<int64_t> tags = std::vector<int64_t>(sampleRate * 4, INT64_MIN);
        float gain = 1;
        int64_t end = INT64_MIN;
    };
    std::mutex mutex_;
    std::map<std::string, Ring> sources_;
};

class VideoCapture {
public:
    virtual ~VideoCapture() = default;
    virtual void poll() = 0;
    virtual std::shared_ptr<const Image> at(Nanoseconds timestamp) = 0;
};
class AudioCapture { public: virtual ~AudioCapture() = default; virtual void check() = 0; };
#ifdef _WIN32
struct CaptureChoice { uintptr_t id; std::string name; };
std::vector<CaptureChoice> listMonitors();
std::vector<CaptureChoice> listWindows();
std::vector<CaptureChoice> listCameras();
// monitorIndex is zero based. windowHandle overrides it when nonzero.
// fps is the output frame rate; screen frames are read back from the GPU at most about 1.5 times that often.
std::unique_ptr<VideoCapture> captureScreen(int monitorIndex, uintptr_t windowHandle = 0, int fps = 30);
std::unique_ptr<VideoCapture> captureCamera(int deviceIndex);
std::unique_ptr<AudioCapture> captureAudio(AudioTimeline& mixer, Nanoseconds origin, bool loopback);
void initializeWindowsMedia();
void shutdownWindowsMedia();
#endif

struct OutputSpec {
    Canvas canvas = Canvas::horizontal;
    std::string destination;
    std::string encoder = "auto";
    int width = 1920, height = 1080, fps = 30, videoKbps = 6000;
    bool streaming = false;
};
struct OutputBatch {
    int64_t frame = 0, sample = 0;
    std::shared_ptr<const Image> video;
    std::shared_ptr<const std::vector<float>> audio;
};
struct OutputStatus {
    std::string state = "starting", error, encoder;
    uint64_t frames = 0, dropped = 0, reconnects = 0;
    std::string id;
    bool streaming = false;
};
class OutputWorker {
public:
    explicit OutputWorker(OutputSpec spec);
    ~OutputWorker();
    OutputWorker(const OutputWorker&) = delete;
    OutputWorker& operator=(const OutputWorker&) = delete;
    void submit(OutputBatch batch);
    void requestFinish();
    bool finished() const;
    void finish();
    OutputStatus status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct EngineConfig {
    int fps = 30, width = 1920, height = 1080;
    double seconds = 10;
    bool synthetic = false, camera = false, microphone = false, desktopAudio = false;
    int monitor = 0, cameraIndex = 0;
    uintptr_t windowHandle = 0;
    Scene scene = screenScene();
    std::vector<OutputSpec> outputs;
};
struct LiveCommands {
    Scene program = screenScene();
    std::optional<Scene> preview;
    std::map<std::string, OutputSpec> outputs;
    float microphone = 1, desktop = 1, master = 1;
};
struct LiveState {
    bool running = false, done = false;
    std::string error;
    int64_t frame = 0;
    uint64_t lateFrames = 0; // frames that repeated the previous picture because the engine was behind
    float microphone = 0, desktop = 0, master = 0;
    // Monitor-sized renders (at most 960 wide), not the full output canvases.
    std::map<Canvas, std::shared_ptr<const Image>> program, preview;
    std::map<std::string, std::shared_ptr<const Image>> sources;
    std::vector<OutputStatus> outputs;
};
// Latest-value mailbox, not an unbounded stream of UI events. The capture thread owns
// media objects; the UI only publishes commands and reads immutable frame snapshots.
class LiveControl {
public:
    void setScenes(Scene program, std::optional<Scene> preview = std::nullopt);
    void setOutputs(std::map<std::string, OutputSpec> outputs);
    void setGains(float microphone, float desktop, float master);
    LiveCommands commands() const;
    LiveState state() const;
    void publish(LiveState state);
private:
    mutable std::mutex mutex_;
    LiveCommands commands_;
    LiveState state_;
};
// The UI will drive this engine through the same scene/layer model, without owning media threads.
int runEngine(const EngineConfig&, std::atomic<bool>& stop,
              const std::function<void(const std::vector<OutputStatus>&)>& onStatus = {}, LiveControl* live = nullptr);
}
