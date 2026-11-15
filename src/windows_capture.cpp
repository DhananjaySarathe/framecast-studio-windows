#include "framecast/core.hpp"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <avrt.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl/client.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <iterator>
#include <thread>

namespace framecast {
using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
namespace {
void hr(HRESULT result, const char* operation) {
    if (!FAILED(result)) return;
    const auto code = static_cast<uint32_t>(result);
    const char* hint = "";
    if (code == 0x80070005) hint = ": access denied. Allow desktop apps under Windows Settings > Privacy & security";
    else if (code == 0x80070490) hint = ": no device found";
    else if (code == 0x88890004) hint = ": the device was disconnected";
    else if (code == 0x80070020 || code == 0xC00D3EA3 || code == 0xC00D3704) hint = ": the device is in use by another app";
    else if (code == 0x887A0005 || code == 0x887A0007) hint = ": the graphics device was reset";
    char hex[16]; std::snprintf(hex, sizeof(hex), "0x%08X", code);
    throw std::runtime_error(std::string(operation) + hint + " (" + hex + ")");
}
BOOL CALLBACK monitorCallback(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor); return TRUE;
}
class ScreenCapture final : public VideoCapture {
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D> staging_;
    IDirect3DDevice winDevice_{nullptr};
    GraphicsCaptureItem item_{nullptr};
    Direct3D11CaptureFramePool pool_{nullptr};
    GraphicsCaptureSession session_{nullptr};
    FrameHistory history_;
    std::shared_ptr<std::atomic<bool>> closed_ = std::make_shared<std::atomic<bool>>(false);
    winrt::event_token closedToken_{};
    winrt::Windows::Graphics::SizeInt32 size_{};
    Direct3D11CaptureFrame pending_{nullptr};
    Nanoseconds minInterval_ = 0, lastReadback_ = INT64_MIN/2;
    UINT stagingWidth_ = 0, stagingHeight_ = 0;
public:
    ScreenCapture(int index, uintptr_t window, int fps) : minInterval_(1000000000LL*2/(3*std::max(1, fps))) {
        if (!GraphicsCaptureSession::IsSupported()) throw std::runtime_error("Windows Graphics Capture is unavailable");
        auto result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                            nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &context_);
        if (FAILED(result)) result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &context_);
        hr(result, "Create capture device");
        ComPtr<IDXGIDevice> dxgi; hr(device_.As(&dxgi), "Get DXGI device");
        winrt::com_ptr<IInspectable> inspectable;
        hr(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()), "Create WinRT capture device");
        winDevice_ = inspectable.as<IDirect3DDevice>();
        auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        if (window) {
            HWND hwnd = reinterpret_cast<HWND>(window);
            if (!IsWindow(hwnd)) throw std::runtime_error("Capture window does not exist");
            hr(interop->CreateForWindow(hwnd, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item_)), "Capture selected window");
        } else {
            std::vector<HMONITOR> monitors;
            EnumDisplayMonitors(nullptr, nullptr, monitorCallback, reinterpret_cast<LPARAM>(&monitors));
            if (index < 0 || static_cast<size_t>(index) >= monitors.size()) throw std::runtime_error("Monitor index not found");
            hr(interop->CreateForMonitor(monitors[index], winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item_)), "Capture selected monitor");
        }
        size_ = item_.Size();
        closedToken_ = item_.Closed([closed = closed_](auto&&, auto&&) { *closed = true; });
        pool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(winDevice_, DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, size_);
        session_ = pool_.CreateCaptureSession(item_);
        // Windows 11 can drop the yellow capture border. The property only exists on newer builds, and the
        // request may be refused; neither is a reason to fail capture.
        try {
            if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
                    L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
                GraphicsCaptureAccess::RequestAccessAsync(GraphicsCaptureAccessKind::Borderless).get();
                session_.IsBorderRequired(false);
            }
        } catch (...) {}
        session_.StartCapture();
        logLine(std::string("screen capture: ") + (window ? "window " : "monitor ") + std::to_string(size_.Width) + "x" + std::to_string(size_.Height));
    }
    ~ScreenCapture() override { try { pending_ = nullptr; if (item_) item_.Closed(closedToken_); if (session_) session_.Close(); if (pool_) pool_.Close(); } catch (...) {} }
    void poll() override {
        try { captureFrame(); }
        catch (const winrt::hresult_error& e) { hr(e.code(), "Windows screen capture failed"); }
    }
    void captureFrame() {
        if (*closed_) throw std::runtime_error("the captured window or display was closed");
        // Keep only the newest frame, rather than accumulating capture latency. Read it back at most about
        // 1.5x the output rate: a 120-144 Hz laptop panel otherwise costs a full-screen GPU copy, a pipeline
        // stall and a memcpy on the engine thread every few milliseconds. The newest frame is held, not
        // dropped, because Windows stops delivering frames once the desktop stops changing.
        while (auto newer = pool_.TryGetNextFrame()) pending_ = std::move(newer);
        if (!pending_) return;
        const Nanoseconds now = hostNow();
        if (now - lastReadback_ < minInterval_) return;
        auto frame = std::move(pending_);
        pending_ = nullptr;
        lastReadback_ = now;
        auto size = frame.ContentSize();
        if (size.Width <= 0 || size.Height <= 0) return;
        if (size.Width != size_.Width || size.Height != size_.Height) {
            frame.Close(); size_ = size; staging_.Reset();
            pool_.Recreate(winDevice_, DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, size_); return;
        }
        auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> texture;
        hr(access->GetInterface(IID_PPV_ARGS(&texture)), "Get captured texture");
        D3D11_TEXTURE2D_DESC desc; texture->GetDesc(&desc);
        if (!staging_ || stagingWidth_ != desc.Width || stagingHeight_ != desc.Height) {
            staging_.Reset();
            stagingWidth_ = desc.Width; stagingHeight_ = desc.Height;
            desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            hr(device_->CreateTexture2D(&desc, nullptr, &staging_), "Allocate capture readback");
        }
        context_->CopyResource(staging_.Get(), texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read captured frame");
        // The content size can disagree with the texture for a frame or two around a resize; never read
        // past the texture that was actually mapped.
        std::shared_ptr<Image> image;
        try {
            image = copyCapturedFrame(static_cast<const uint8_t*>(mapped.pData), mapped.RowPitch,
                                      std::min<int>(size.Width, static_cast<int>(stagingWidth_)),
                                      std::min<int>(size.Height, static_cast<int>(stagingHeight_)));
        } catch (...) { context_->Unmap(staging_.Get(), 0); throw; }
        context_->Unmap(staging_.Get(), 0);
        history_.push({frame.SystemRelativeTime().count()*100, std::move(image)});
    }
    std::shared_ptr<const Image> at(Nanoseconds time) override {
        // WGC can stop delivering frames on an unchanged desktop. That is not source loss.
        return history_.at(time, false);
    }
};

struct CameraState {
    FrameHistory history;
    std::mutex mutex;
    std::string error;
    bool stopping = false;
    int width = 0, height = 0;
    LONG stride = 0;
    ComPtr<IMFSourceReader> reader;
};
class CameraCallback final : public IMFSourceReaderCallback {
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<CameraState> state_;
public:
    explicit CameraCallback(std::shared_ptr<CameraState> state) : state_(std::move(state)) {}
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IMFSourceReaderCallback)) { *out = static_cast<IMFSourceReaderCallback*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override { auto n = --refs_; if (!n) delete this; return n; }
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }
    STDMETHODIMP OnFlush(DWORD) override { return S_OK; }
    STDMETHODIMP OnReadSample(HRESULT status, DWORD, DWORD flags, LONGLONG, IMFSample* sample) override {
        auto& s = *state_;
        std::lock_guard lock(s.mutex);
        if (s.stopping) return S_OK;
        try {
            hr(status, "Read camera sample");
            if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) throw std::runtime_error("Camera stopped sending frames");
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) throw std::runtime_error("Camera format changed; restart capture");
            if (sample) {
                const auto timestamp = hostNow();
                ComPtr<IMFMediaBuffer> buffer; hr(sample->ConvertToContiguousBuffer(&buffer), "Read camera buffer");
                auto image = std::make_shared<Image>(s.width, s.height);
                ComPtr<IMF2DBuffer> buffer2d;
                if (SUCCEEDED(buffer.As(&buffer2d))) {
                    BYTE* first = nullptr; LONG pitch = 0;
                    hr(buffer2d->Lock2D(&first, &pitch), "Lock camera image");
                    for (int row = 0; row < s.height; ++row)
                        std::memcpy(image->bgra.data()+static_cast<size_t>(row)*s.width*4, first+ptrdiff_t(row)*pitch, static_cast<size_t>(s.width)*4);
                    buffer2d->Unlock2D();
                } else {
                    BYTE* data = nullptr; DWORD length = 0;
                    hr(buffer->Lock(&data, nullptr, &length), "Lock camera bytes");
                    const LONG stride = s.stride;
                    if (length < static_cast<uint64_t>(std::abs(stride))*s.height) { buffer->Unlock(); throw std::runtime_error("Camera buffer is too short"); }
                    for (int row = 0; row < s.height; ++row) {
                        const int sourceRow = stride < 0 ? s.height-1-row : row;
                        std::memcpy(image->bgra.data()+static_cast<size_t>(row)*s.width*4,
                                    data+static_cast<size_t>(sourceRow)*std::abs(stride), static_cast<size_t>(s.width)*4);
                    }
                    buffer->Unlock();
                }
                // Media Foundation RGB32 is BGRX, not premultiplied alpha.
                for (size_t p = 3; p < image->bgra.size(); p += 4) image->bgra[p] = 255;
                s.history.push({timestamp, std::move(image)});
            }
            hr(s.reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr), "Request camera frame");
        } catch (const std::exception& e) { s.error = e.what(); }
        return S_OK;
    }
};
class CameraCapture final : public VideoCapture {
    std::shared_ptr<CameraState> state_ = std::make_shared<CameraState>();
    ComPtr<IMFMediaSource> source_;
public:
    explicit CameraCapture(int index) {
        ComPtr<IMFAttributes> attributes; hr(MFCreateAttributes(&attributes, 2), "Create camera attributes");
        hr(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID), "Select video devices");
        IMFActivate** devices = nullptr; UINT32 count = 0;
        hr(MFEnumDeviceSources(attributes.Get(), &devices, &count), "Enumerate cameras");
        HRESULT activation = E_INVALIDARG;
        if (index >= 0 && static_cast<UINT32>(index) < count) activation = devices[index]->ActivateObject(IID_PPV_ARGS(&source_));
        for (UINT32 n = 0; n < count; ++n) devices[n]->Release();
        CoTaskMemFree(devices); hr(activation, "Open selected camera");
        try {
            ComPtr<IMFSourceReaderCallback> callback; callback.Attach(new CameraCallback(state_));
            hr(MFCreateAttributes(&attributes, 2), "Create camera reader attributes");
            hr(attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.Get()), "Set camera callback");
            hr(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE), "Enable camera colour conversion");
            hr(MFCreateSourceReaderFromMediaSource(source_.Get(), attributes.Get(), &state_->reader), "Create camera reader");
            // Choose the camera's own mode before asking for RGB32. Otherwise Media Foundation keeps the device's
            // current mode, which on many webcams is full resolution at a few frames per second, or a size that
            // makes the software colour conversion too slow. Prefer 30 fps, then the size nearest 1280x720,
            // never above 1920x1080 unless nothing else exists.
            ComPtr<IMFMediaType> best; double bestScore = -1e18;
            for (DWORD n = 0; n < 512; ++n) {
                ComPtr<IMFMediaType> native;
                if (FAILED(state_->reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, n, &native))) break;
                UINT32 w = 0, h = 0, numerator = 0, denominator = 0;
                if (FAILED(MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE, &w, &h)) || !w || !h) continue;
                if (FAILED(MFGetAttributeRatio(native.Get(), MF_MT_FRAME_RATE, &numerator, &denominator)) || !denominator) continue;
                double score = std::min(double(numerator)/denominator, 30.)*1000;
                if (w > 1920 || h > 1080) score -= 20000;
                score -= std::abs(double(w)*h - 1280.*720)/1000;
                if (score > bestScore) { bestScore = score; best = native; }
            }
            if (best) hr(state_->reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, best.Get()), "Choose camera mode");
            else logLine("camera: no native mode list; using the device default");
            ComPtr<IMFMediaType> type; hr(MFCreateMediaType(&type), "Create camera format");
            hr(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set camera media type");
            hr(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "Set camera pixel format");
            hr(state_->reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.Get()), "Choose camera format");
            hr(state_->reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type), "Read camera format");
            UINT32 w, h; hr(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h), "Read camera dimensions");
            state_->width = static_cast<int>(w); state_->height = static_cast<int>(h);
            logLine("camera " + std::to_string(index) + ": " + std::to_string(w) + "x" + std::to_string(h));
            UINT32 stride = 0;
            if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) state_->stride = static_cast<LONG>(stride);
            else hr(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, w, &state_->stride), "Read camera stride");
            hr(state_->reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE), "Disable camera audio");
            hr(state_->reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE), "Enable camera video");
            hr(state_->reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr), "Start camera");
        } catch (...) { close(); throw; }
    }
    void close() {
        ComPtr<IMFSourceReader> reader;
        { std::lock_guard lock(state_->mutex); state_->stopping = true; reader = std::move(state_->reader); }
        if (reader) reader->Flush(MF_SOURCE_READER_ALL_STREAMS);
        if (source_) source_->Shutdown();
    }
    ~CameraCapture() override { close(); }
    void poll() override { std::lock_guard lock(state_->mutex); if (!state_->error.empty()) throw std::runtime_error(state_->error); }
    std::shared_ptr<const Image> at(Nanoseconds time) override { poll(); return state_->history.at(time); }
};

class WasapiCapture final : public AudioCapture {
    std::atomic<bool> stop_{false};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::string problem_;
    const std::string label_;
    void setProblem(std::string text) {
        std::lock_guard lock(mutex_);
        if (text == problem_) return;
        logLine(text.empty() ? label_ + " capture working" : "warning: " + text);
        problem_ = std::move(text);
    }
    // One connection to the current default device. Returns true when the default device changed and the
    // new one should be opened at once, false when stopped; throws when the device fails.
    bool session(AudioTimeline& mixer, Nanoseconds origin, bool loopback) {
        const EDataFlow flow = loopback ? eRender : eCapture;
        ComPtr<IMMDeviceEnumerator> enumerator;
        hr(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)), "Enumerate audio devices");
        ComPtr<IMMDevice> device;
        hr(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device), "Open default audio device");
        std::wstring opened;
        { LPWSTR id = nullptr; if (SUCCEEDED(device->GetId(&id)) && id) { opened = id; CoTaskMemFree(id); } }
        ComPtr<IAudioClient> client;
        hr(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf())), "Activate audio capture");
        // Ask Windows to convert whatever the device runs at (often 44.1 kHz or mono) to the studio format.
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT; format.nChannels = 2; format.nSamplesPerSec = sampleRate;
        format.wBitsPerSample = 32; format.nBlockAlign = 8; format.nAvgBytesPerSec = sampleRate*8;
        const DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
                            (loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0);
        hr(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 1000000, 0, &format, nullptr), "Start 48 kHz stereo audio capture");
        ComPtr<IAudioCaptureClient> capture; hr(client->GetService(IID_PPV_ARGS(&capture)), "Get audio packets");
        hr(client->Start(), "Start audio device");
        setProblem("");
        Nanoseconds nextDeviceCheck = hostNow() + 2000000000LL;
        bool changed = false;
        while (!stop_ && !changed) {
            UINT32 frames = 0; hr(capture->GetNextPacketSize(&frames), "Poll audio device");
            while (frames && !stop_) {
                BYTE* data = nullptr; DWORD bufferFlags = 0; UINT64 position = 0, qpc = 0;
                hr(capture->GetBuffer(&data, &frames, &bufferFlags, &position, &qpc), "Capture audio packet");
                try {
                    const Nanoseconds time = bufferFlags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR ? hostNow()-int64_t(frames)*1000000000LL/sampleRate : static_cast<int64_t>(qpc)*100;
                    const int64_t delta = time-origin;
                    const int64_t sample = (delta/1000000000LL)*sampleRate + (delta%1000000000LL)*sampleRate/1000000000LL;
                    if (bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        std::vector<float> silent(static_cast<size_t>(frames)*2, 0);
                        mixer.push(loopback ? "desktop" : "microphone", sample, silent.data(), static_cast<int>(frames));
                    } else mixer.push(loopback ? "desktop" : "microphone", sample, reinterpret_cast<float*>(data), static_cast<int>(frames));
                } catch (...) { capture->ReleaseBuffer(frames); throw; }
                hr(capture->ReleaseBuffer(frames), "Release audio packet");
                hr(capture->GetNextPacketSize(&frames), "Poll next audio packet");
            }
            // Follow the default device: plugging in headphones or a USB microphone mid-show moves the
            // default, and the old device keeps "working" in silence.
            if (hostNow() >= nextDeviceCheck) {
                nextDeviceCheck = hostNow() + 2000000000LL;
                ComPtr<IMMDevice> current; LPWSTR id = nullptr;
                if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &current)) && SUCCEEDED(current->GetId(&id)) && id) {
                    changed = opened != id;
                    CoTaskMemFree(id);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        client->Stop();
        if (changed) logLine(label_ + ": default device changed, reopening");
        return changed;
    }
public:
    // Never throws and never blocks the caller: the device is opened, watched and reopened on this
    // object's own thread, and trouble is reported through problem().
    WasapiCapture(AudioTimeline& mixer, Nanoseconds origin, bool loopback) : label_(loopback ? "Desktop sound" : "Microphone") {
        thread_ = std::thread([this, &mixer, origin, loopback] {
            const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
            DWORD task = 0;
            const HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &task);
            while (!stop_) {
                bool reopenNow = false;
                try { reopenNow = session(mixer, origin, loopback); }
                catch (const std::exception& e) { setProblem(label_ + " unavailable: " + e.what()); }
                // Like OBS, keep trying every few seconds for as long as the session runs.
                for (int n = 0; n < 30 && !stop_ && !reopenNow; ++n) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
            if (com) CoUninitialize();
        });
    }
    ~WasapiCapture() override { stop_ = true; if (thread_.joinable()) thread_.join(); }
    std::string problem() override { std::lock_guard lock(mutex_); return problem_; }
};
}
void initializeWindowsMedia() {
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    catch (const winrt::hresult_error& e) { hr(e.code(), "Initialize Windows capture apartment"); }
    const HRESULT result = MFStartup(MF_VERSION);
    if (FAILED(result)) { winrt::uninit_apartment(); hr(result, "Initialize Media Foundation"); }
}
void shutdownWindowsMedia() { MFShutdown(); winrt::uninit_apartment(); }
namespace {
std::string utf8(const wchar_t* value) {
    int n = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), n, nullptr, nullptr);
    if (!out.empty()) out.pop_back();
    return out;
}
BOOL CALLBACK windowCallback(HWND window, LPARAM parameter) {
    // Never send window messages from here. The studio lists windows on a worker thread while its UI thread
    // waits: GetWindowTextLength/GetWindowText send WM_GETTEXT(LENGTH) to Framecast's own windows (and
    // GetWindowTextLength to every window), so the UI thread, or any hung app, froze the Add dialog forever.
    // InternalGetWindowText reads the stored title without a message. Framecast's own windows are skipped.
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
    // Minimised windows cannot be captured; tool windows and "cloaked" windows (background store apps,
    // windows on another virtual desktop) only clutter the list.
    if (IsIconic(window) || (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) ||
        (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD)) return TRUE;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
    wchar_t title[512] = {};
    if (InternalGetWindowText(window, title, static_cast<int>(std::size(title))) <= 0) return TRUE;
    reinterpret_cast<std::vector<CaptureChoice>*>(parameter)->push_back({reinterpret_cast<uintptr_t>(window), utf8(title)});
    return TRUE;
}
}
std::string describeSystem() {
    std::string out;
    // RtlGetVersion reports the real build; GetVersionEx answers according to the app manifest.
    RTL_OSVERSIONINFOW version{}; version.dwOSVersionInfoSize = sizeof(version);
    using RtlGetVersionFn = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (auto get = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) get(&version);
    out += "Windows " + std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion) +
           " build " + std::to_string(version.dwBuildNumber);
    wchar_t cpu[256] = {}; DWORD cpuBytes = sizeof(cpu);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString",
                     RRF_RT_REG_SZ, nullptr, cpu, &cpuBytes) == ERROR_SUCCESS)
        out += "; CPU " + utf8(cpu);
    out += " (" + std::to_string(std::thread::hardware_concurrency()) + " logical)";
    MEMORYSTATUSEX memory{}; memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
        out += "; RAM " + std::to_string(memory.ullTotalPhys >> 20) + " MB, " + std::to_string(memory.ullAvailPhys >> 20) + " MB free";
    ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT n = 0; factory->EnumAdapters1(n, &adapter) != DXGI_ERROR_NOT_FOUND; ++n) {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)))
                out += "; GPU " + std::to_string(n) + " " + utf8(desc.Description) + " " +
                       std::to_string(desc.DedicatedVideoMemory >> 20) + " MB" +
                       (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE ? " (software)" : "");
            adapter.Reset();
        }
    }
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, monitorCallback, reinterpret_cast<LPARAM>(&monitors));
    for (size_t n = 0; n < monitors.size(); ++n) {
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitors[n], &info)) continue;
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode);
        out += "; display " + std::to_string(n + 1) + " " + std::to_string(info.rcMonitor.right - info.rcMonitor.left) + "x" +
               std::to_string(info.rcMonitor.bottom - info.rcMonitor.top) + " " + std::to_string(mode.dmDisplayFrequency) + " Hz";
    }
    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power))
        out += power.ACLineStatus == 1 ? "; on mains power" : power.ACLineStatus == 0 ? "; on battery" : "; power unknown";
    return out;
}
std::vector<CaptureChoice> listMonitors() {
    std::vector<HMONITOR> monitors; std::vector<CaptureChoice> out;
    EnumDisplayMonitors(nullptr,nullptr,monitorCallback,reinterpret_cast<LPARAM>(&monitors));
    for (size_t n=0;n<monitors.size();++n) {
        MONITORINFO info{}; info.cbSize=sizeof(info); GetMonitorInfoW(monitors[n],&info);
        out.push_back({n,"Display "+std::to_string(n+1)+" · "+std::to_string(info.rcMonitor.right-info.rcMonitor.left)+"×"+std::to_string(info.rcMonitor.bottom-info.rcMonitor.top)});
    }
    return out;
}
std::vector<CaptureChoice> listWindows() {std::vector<CaptureChoice> out;EnumWindows(windowCallback,reinterpret_cast<LPARAM>(&out));return out;}
std::vector<CaptureChoice> listCameras() {
    ComPtr<IMFAttributes> attributes; hr(MFCreateAttributes(&attributes,1),"Create device list");
    hr(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),"Select camera list");
    IMFActivate** devices=nullptr;UINT32 count=0;
    hr(MFEnumDeviceSources(attributes.Get(),&devices,&count),"List cameras");
    std::vector<CaptureChoice> out;
    for(UINT32 n=0;n<count;++n){
        wchar_t* name=nullptr;UINT32 length=0;
        if(SUCCEEDED(devices[n]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,&name,&length))){out.push_back({n,utf8(name)});CoTaskMemFree(name);}
        devices[n]->Release();
    }
    CoTaskMemFree(devices);return out;
}
std::unique_ptr<VideoCapture> captureScreen(int monitor, uintptr_t window, int fps) {
    try { return std::make_unique<ScreenCapture>(monitor, window, fps); }
    catch (const winrt::hresult_error& e) { hr(e.code(), "Start Windows screen capture"); throw; }
}
std::unique_ptr<VideoCapture> captureCamera(int device) { return std::make_unique<CameraCapture>(device); }
std::unique_ptr<AudioCapture> captureAudio(AudioTimeline& mixer, Nanoseconds origin, bool loopback) { return std::make_unique<WasapiCapture>(mixer, origin, loopback); }
}
