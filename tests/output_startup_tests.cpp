#include "framecast/core.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace framecast;
int main() {
    auto pattern = (std::filesystem::temp_directory_path() / "framecast-startup-XXXXXX").string();
    std::vector<char> directory(pattern.begin(), pattern.end());
    directory.push_back('\0');
    if (!mkdtemp(directory.data())) return 2;
    const auto path = std::filesystem::path(directory.data()) / "blocked.mkv";
    if (mkfifo(path.c_str(), 0600)) return 3;
    bool passed = false;
    {
        OutputWorker worker({Canvas::horizontal, path.string(), "auto", 640, 360, 30, 6000, false});
        auto image = std::make_shared<Image>(640, 360);
        auto audio = std::make_shared<std::vector<float>>(3200, 0.f);
        worker.submit({0, 0, image, audio});
        // Opening the FIFO for writing blocks until the reader below connects.
        // Fill the bounded recording queue while the output is still opening.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        for (int n = 1; n < 20; ++n) worker.submit({n, n * 1600, image, audio});
        std::thread reader([&] {
            const int file = open(path.c_str(), O_RDONLY);
            char bytes[8192];
            while (read(file, bytes, sizeof(bytes)) > 0) {}
            close(file);
        });
        worker.finish();
        reader.join();
        const auto status = worker.status();
        passed = status.state == "failed" && status.error.find("Recording cannot keep up") != std::string::npos;
        std::cout << (passed ? "PASS " : "FAIL ") << "recording startup preserves a queue failure: "
                  << status.state << " frames=" << status.frames << " error=" << status.error << '\n';
    }
    std::filesystem::remove_all(directory.data());
    return passed ? 0 : 1;
}
