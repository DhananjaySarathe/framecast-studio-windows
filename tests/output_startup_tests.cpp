// What a recording does when its writer cannot keep up. Opening a FIFO for writing blocks until a reader
// connects, which stands in for a slow encoder or disk.
//  1. A short stall costs pictures, never the recording: frames beyond the eight waiting are kept as
//     audio only, the file is completed and the skipped pictures are counted.
//  2. A writer that never recovers (twenty seconds queued) fails, and that failure is not overwritten by
//     a later "live" or "stopped" status once the writer finally opens.
#include "framecast/core.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
extern "C" {
#include <libavutil/log.h>
}

using namespace framecast;
namespace {
OutputStatus record(const std::filesystem::path& path, int batches) {
    OutputWorker worker({Canvas::horizontal, path.string(), "auto", 640, 360, 30, 6000, false});
    auto image = std::make_shared<Image>(640, 360);
    auto audio = std::make_shared<std::vector<float>>(3200, 0.f);
    worker.submit({0, 0, image, audio});
    // Let the worker reach the blocked open, then pile frames up behind it.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    for (int n = 1; n < batches; ++n) worker.submit({n, n * 1600, image, audio});
    std::thread reader([&] {
        const int file = open(path.c_str(), O_RDONLY);
        char bytes[8192];
        while (read(file, bytes, sizeof(bytes)) > 0) {}
        close(file);
    });
    worker.finish();
    reader.join();
    return worker.status();
}
}
int main() {
    av_log_set_level(AV_LOG_ERROR);
    auto pattern = (std::filesystem::temp_directory_path() / "framecast-startup-XXXXXX").string();
    std::vector<char> directory(pattern.begin(), pattern.end());
    directory.push_back('\0');
    if (!mkdtemp(directory.data())) return 2;
    const auto stalled = std::filesystem::path(directory.data()) / "stalled.mkv";
    const auto stuck = std::filesystem::path(directory.data()) / "stuck.mkv";
    if (mkfifo(stalled.c_str(), 0600) || mkfifo(stuck.c_str(), 0600)) return 3;

    const auto brief = record(stalled, 60);
    const bool survived = brief.state == "stopped" && brief.error.empty() && brief.frames >= 8 && brief.dropped > 0 &&
                          brief.frames + brief.dropped == 60;
    std::cout << (survived ? "PASS " : "FAIL ") << "a stalled writer skips pictures and still completes the recording: "
              << brief.state << " frames=" << brief.frames << " skipped=" << brief.dropped << " error=" << brief.error << '\n';

    const auto dead = record(stuck, 30 * 20 + 10);
    const bool failed = dead.state == "failed" && dead.error.find("Recording cannot keep up") != std::string::npos;
    std::cout << (failed ? "PASS " : "FAIL ") << "a writer that never recovers fails and stays failed: "
              << dead.state << " frames=" << dead.frames << " error=" << dead.error << '\n';

    std::filesystem::remove_all(directory.data());
    return survived && failed ? 0 : 1;
}
