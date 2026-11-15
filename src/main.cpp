#include "framecast/core.hpp"
#include <csignal>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <set>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" {
#include <libavutil/log.h>
#include <libavformat/avformat.h>
}

namespace {
std::atomic<bool> stopped{false};
void onSignal(int) { stopped = true; }
void usage() {
    std::cout << "Framecast Studio Windows — core engine preview\n"
        "  framecast-engine --synthetic --record DIR --seconds 10\n"
        "  framecast-engine --monitor 0 --camera 0 --mic --desktop-audio --record DIR\n"
        "  framecast-engine --window HWND --record DIR --stream-horizontal rtmp://127.0.0.1/live/test\n"
        "Options:\n"
        "  --seconds N             Duration (default 10; Ctrl+C stops early)\n"
        "  --size WIDTHxHEIGHT     Landscape canvas (default 1920x1080); portrait swaps dimensions\n"
        "  --fps N                 1–120, default 30\n"
        "  --container mp4|mov|mkv  Recording container, default mkv\n"
        "  --encoder NAME          FFmpeg H.264 encoder, default auto\n"
        "  --bitrate KBPS          Per output, default 6000\n"
        "  --stream-horizontal URL Repeat for multiple RTMP/RTMPS destinations\n"
        "  --stream-vertical URL   Repeat for multiple RTMP/RTMPS destinations\n"
        "Stream URLs may also be supplied using FRAMECAST_STREAM_HORIZONTAL / _VERTICAL.\n"
        "Native device indices are zero based. Synthetic mode opens no capture devices.\n";
}
}
int engineMain(int argc, char** argv) {
    using namespace framecast;
    av_log_set_level(AV_LOG_QUIET); // FFmpeg diagnostics can otherwise print destination secrets.
    avformat_network_init();
    std::signal(SIGINT, onSignal); std::signal(SIGTERM, onSignal);
    bool mediaInitialized = false;
    int result = 1;
    try {
        EngineConfig cfg;
        std::string record, container = "mkv", encoder = "auto";
        int bitrate = 6000;
        std::vector<std::pair<Canvas, std::string>> streams;
        auto integer = [](const std::string& text) { size_t end; int n = std::stoi(text, &end); if (end != text.size()) throw std::invalid_argument("Invalid integer"); return n; };
        for (int n = 1; n < argc; ++n) {
            std::string option = argv[n];
            auto value = [&]() -> std::string { if (++n >= argc) throw std::invalid_argument("Missing value for "+option); return argv[n]; };
            if (option == "--help" || option == "-h") { usage(); avformat_network_deinit(); return 0; }
            else if (option == "--synthetic") cfg.synthetic = true;
            else if (option == "--record") record = value();
            else if (option == "--container") container = value();
            else if (option == "--encoder") encoder = value();
            else if (option == "--bitrate") bitrate = integer(value());
            else if (option == "--fps") cfg.fps = integer(value());
            else if (option == "--monitor") cfg.monitor = integer(value());
            else if (option == "--camera") { cfg.camera = true; cfg.cameraIndex = integer(value()); cfg.scene = screenCameraScene(); }
            else if (option == "--window") { auto text = value(); size_t end; cfg.windowHandle = std::stoull(text, &end, 0); if (!cfg.windowHandle || end != text.size()) throw std::invalid_argument("Invalid window handle"); }
            else if (option == "--mic") cfg.microphone = true;
            else if (option == "--desktop-audio") cfg.desktopAudio = true;
            else if (option == "--seconds") { auto text = value(); size_t end; cfg.seconds = std::stod(text, &end); if (end != text.size()) throw std::invalid_argument("Invalid duration"); }
            else if (option == "--size") {
                auto size = value(); auto x = size.find('x');
                if (x == std::string::npos) throw std::invalid_argument("Use --size WIDTHxHEIGHT");
                cfg.width = integer(size.substr(0, x)); cfg.height = integer(size.substr(x+1));
            } else if (option == "--stream-horizontal") streams.emplace_back(Canvas::horizontal, value());
            else if (option == "--stream-vertical") streams.emplace_back(Canvas::vertical, value());
            else throw std::invalid_argument("Unknown option: "+option);
        }
        if (const char* url = std::getenv("FRAMECAST_STREAM_HORIZONTAL"); url && *url) streams.emplace_back(Canvas::horizontal, url);
        if (const char* url = std::getenv("FRAMECAST_STREAM_VERTICAL"); url && *url) streams.emplace_back(Canvas::vertical, url);
        if (container != "mkv" && container != "mp4" && container != "mov") throw std::invalid_argument("Use mp4, mov or mkv");
        if (cfg.monitor < 0 || cfg.cameraIndex < 0) throw std::invalid_argument("Device indices must be nonnegative");
        auto spec = [&](Canvas canvas, std::string destination, bool streaming) {
            return OutputSpec{canvas, std::move(destination), encoder,
                canvas == Canvas::horizontal ? cfg.width : cfg.height,
                canvas == Canvas::horizontal ? cfg.height : cfg.width, cfg.fps, bitrate, streaming};
        };
        if (!record.empty()) {
            const auto directory = std::filesystem::path(std::u8string(record.begin(), record.end()));
            std::filesystem::create_directories(directory);
            // Do not overwrite another session or the user's files.
            const auto session = "Framecast-"+std::to_string(hostNow());
            for (auto canvas : {Canvas::horizontal, Canvas::vertical}) {
                auto path = directory/(session+(canvas == Canvas::horizontal ? "-16x9." : "-9x16.")+container);
                const auto utf8 = path.u8string();
                cfg.outputs.push_back(spec(canvas, std::string(utf8.begin(), utf8.end()), false));
            }
        }
        for (auto& [canvas, url] : streams) {
            if (!(url.starts_with("rtmp://") || url.starts_with("rtmps://"))) throw std::invalid_argument("Streaming requires an RTMP or RTMPS URL");
            cfg.outputs.push_back(spec(canvas, url, true));
        }
        if (cfg.outputs.empty()) throw std::invalid_argument("Choose --record DIR or a stream destination. See --help.");
        if (!cfg.synthetic) {
#ifdef _WIN32
            initializeWindowsMedia(); mediaInitialized = true;
#endif
        }
        result = runEngine(cfg, stopped, [](const auto& statuses) {
            for (size_t n = 0; n < statuses.size(); ++n) {
                const auto& s = statuses[n];
                std::cerr << "output " << n+1 << ": " << s.state << " frames=" << s.frames << " dropped=" << s.dropped
                          << " reconnects=" << s.reconnects << " encoder=" << s.encoder;
                if (!s.error.empty()) std::cerr << " (" << s.error << ")";
                std::cerr << '\n';
            }
        });
    } catch (const std::exception& e) { std::cerr << "Framecast: " << e.what() << '\n'; }
#ifdef _WIN32
    if (mediaInitialized) shutdownWindowsMedia();
#else
    (void)mediaInitialized;
#endif
    avformat_network_deinit();
    return result;
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> utf8;
    std::vector<char*> arguments;
    for (int n = 0; n < argc; ++n) {
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[n], -1, nullptr, 0, nullptr, nullptr);
        if (size <= 0) return 1;
        std::string value(static_cast<size_t>(size), '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[n], -1, value.data(), size, nullptr, nullptr)) return 1;
        value.pop_back(); utf8.push_back(std::move(value));
    }
    for (auto& value : utf8) arguments.push_back(value.data());
    return engineMain(argc, arguments.data());
}
#else
int main(int argc, char** argv) { return engineMain(argc, argv); }
#endif
