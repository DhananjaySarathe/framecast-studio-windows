# Framecast Studio Windows

This repository contains only the Windows implementation. The Mac app lives in https://github.com/DhananjaySarathe/framecast-studio. The Mac app remains the feature and visual reference. The Qt desktop studio now connects directly to the same C++ engine as the command-line verifier. This is a developer preview; full feature parity and native Windows hardware validation are still in progress.

## Install on a Windows laptop

Target: Windows 11 on Intel or AMD, 64-bit. Download `Framecast-Studio-Windows-0.1.1-x64-Setup.exe` from the [v0.1.1 release](https://github.com/DhananjaySarathe/framecast-studio-windows/releases/tag/v0.1.1) and run it; it installs into your user account and adds a Start menu entry. Qt and the C++ runtime are included, so Visual Studio, Python and Qt are not needed on the laptop. The portable ZIP is an alternative: extract the entire folder and open `FramecastStudio.exe`.

This preview installer is unsigned; Windows may show an unknown-publisher or SmartScreen prompt. Use the release's `SHA256SUMS.txt` to identify the build. The application does not start capture on launch. Open **Add**, select your sources, and choose **Start preview**. Scenes and recording preferences are kept between launches; stream keys are kept only for the current session.

The pipeline tests installer execution, installed-app launch with developer runtime paths removed, actual dual-canvas media files, and uninstall. Physical webcam/microphone, graphics-driver combinations and real streaming-platform credentials still require laptop testing. An unavailable native capture service on a hosted Windows VM is reported as a skipped test, not as a capture pass.

## Desktop studio

`FramecastStudio` preserves the Mac app's midnight palette, violet accent, 236 px scenes sidebar, dual-canvas program well, 340 px inspector and bottom transport bar. It includes:

- Eight reference scenes, live thumbnails, scene copies and renaming, and independent scene selection for each canvas.
- A continuously running preview without recording. Preview mode holds candidate edits off air, shows a separate on-air monitor, and commits with **Put on air** / Ctrl+Enter or a scene double-click.
- Layer position, size, opacity, fit, shape and mirror controls connected to the compositor.
- Microphone, desktop and master faders, mute and live RMS meters connected to the mixer.
- Independent recording and broadcast start/stop controls. Outputs finalize off the render thread, and stopping a broadcast leaves local recording and preview running.
- Source selection, recording folder/container/canvas settings, and multiple RTMP/RTMPS destinations with masked, session-only stream keys.
- Scene and recording preferences saved through QSettings; capture never starts automatically on a normal launch.

On Windows, open **Add**, select a screen/window and optional webcam and audio, then choose **Start preview**. Choose **Both**, **16:9** or **9:16** to select the recording canvases. **Destinations** configures enabled broadcast outputs independently. On this development Mac use `--synthetic`; native capture is Windows-only.

Comments, green room, guests, angles, overlays, captions and rundown retain their inspector locations and explicitly describe their unavailable integrations. There are no fake connections, comments or guests. Animated transitions, direct canvas dragging, undo/redo, replay, private presenter windows and the remaining integrations are not implemented yet.

## Current engine implementation

- Windows 11 x64 capture: Windows Graphics Capture for a monitor or window, Media Foundation for a webcam, WASAPI for the default microphone and desktop playback endpoint.
- Landscape and portrait scenes with independent normalized layer geometry. The screen-plus-camera layout follows the Mac app’s `Sources/FramecastEngine/Model/Templates.swift`, including its masks and portrait arrangement.
- One frame/sample clock; 48 kHz stereo mixing, per-source gain, timestamped capture history, silence for missing audio and expiry of stale video.
- Simultaneous landscape and portrait recording in MKV, fragmented MP4 or fragmented MOV, H.264 video and AAC audio.
- Multiple RTMP/RTMPS outputs, each with its own bounded queue, connection timeout and reconnect backoff. Stream reconnects restart the encoder session and rebase audio and video together.
- Automatic encoder selection on Windows: NVIDIA, Intel, AMD, then OpenH264 software fallback. An explicit `--encoder` overrides selection. Availability is checked by opening the encoder, not merely listing it.
- Synthetic flash/beep verification without opening capture devices or contacting a streaming platform.

Capture uses a D3D11 texture followed by CPU readback, throttled to about 1.5 times the output frame rate so a 120–144 Hz panel doesn't cost a full-screen copy every few milliseconds. Webcams open in their own 30 fps mode nearest 1280×720 before conversion to RGB. Composition runs on the CPU. Column and row geometry is computed once per layer, and rows are split across up to four cores. Both 1080p canvases compose in a few milliseconds. `tests/compose_perf_tests.cpp` checks the output is pixel-identical to the original compositor. The studio monitors get renders at most 960 pixels wide; full-size canvases are composed only for active outputs. When the engine falls more than two frames behind real time, it repeats the previous picture for outputs instead of composing, so audio and timing stay continuous and it catches up. `LiveState::lateFrames` counts these frames. `tests/realtime_tests.cpp` records both 1080p canvases for six seconds and requires every frame to reach the encoders on time. A GPU compositor and texture-to-encoder path remain the next efficiency step. Compatible outputs currently share composed frames and audio blocks, but **do not yet share encoders**. Recording queue overload is an explicit failure instead of silently dropping audio; network queue overload reconnects that destination.

## Build on Windows

Install Visual Studio 2022 with Desktop development with C++, a recent Windows SDK, CMake 3.24+, vcpkg, and a Qt 6.5+ MSVC x64 desktop SDK including Widgets and Test (the workflow uses Qt 6.8.3). From PowerShell in the repository root:

```powershell
cmake -S . -B build/windows -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  "-DVCPKG_OVERLAY_TRIPLETS=$pwd/triplets" `
  -DVCPKG_TARGET_TRIPLET=x64-windows -DFRAMECAST_BUILD_UI=ON `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build/windows --config Release --parallel
$env:PATH = "C:/Qt/6.8.3/msvc2022_64/bin;$env:PATH"
ctest --test-dir build/windows -C Release --output-on-failure
```

Set `VCPKG_ROOT` to your vcpkg checkout first. On GitHub-hosted Windows runners the workflow uses `VCPKG_INSTALLATION_ROOT`. The manifest requests FFmpeg libraries and the selected encoder integrations. The manifest pins the dependency baseline. The setup helper checks out that revision and the root triplet builds Release libraries only. The supported setup/build commands are in the root README. FFmpeg's Windows network feature uses Schannel for TLS.

The studio (`FramecastStudio.exe`), CLI (`framecast-engine.exe`) and runtime DLLs are under `build/windows/Release`. Keep them together. This is a developer build; signing and full Mac feature parity are later milestones. A per-user installer and portable package are built by the pipeline.

```powershell
# Open the studio. Choose Add to start native capture explicitly.
.\build\windows\Release\FramecastStudio.exe

# Explore the UI with synthetic sources and no capture permissions.
.\build\windows\Release\FramecastStudio.exe --synthetic

# Synthetic dual-canvas recording; never opens your camera, microphone or screen.
.\build\windows\Release\framecast-engine.exe --synthetic --record .\local-output\windows --seconds 10

# Monitor 0, webcam 0, microphone and desktop audio, in the Mac screen-plus-camera layout.
.\build\windows\Release\framecast-engine.exe --monitor 0 --camera 0 --mic --desktop-audio `
  --record .\local-output\windows --seconds 60

# Record both canvases and publish landscape to a local RTMP server.
.\build\windows\Release\framecast-engine.exe --monitor 0 --record .\local-output\windows `
  --stream-horizontal rtmp://127.0.0.1:1935/live/framecast --seconds 60
```

`--window HWND` selects a specific window instead of a monitor; HWND accepts decimal or `0x` notation. Device indices are zero based. `--size 1280x720` configures landscape and automatically makes portrait 720x1280. `--stream-vertical URL` adds a portrait destination; either stream flag may be repeated. Use `FRAMECAST_STREAM_HORIZONTAL` and `FRAMECAST_STREAM_VERTICAL` environment variables to avoid putting a private stream key in command history. The engine never logs destination URLs. Ctrl+C stops early and finalizes recording files.

## Verify

With Python 3, `ffmpeg`, and `ffprobe` on PATH:

```powershell
python tests/verify_media.py build/windows/Release/framecast-engine.exe --network
```

The media test checks all three containers, 29 fps frame/sample timing, exact frame counts, nonblack rendered flashes, identical decoded audio between canvases, A/V onset tolerance, unavailable-encoder errors, loopback RTMP delivery, an interrupted receiver, automatic reconnect, and continuous dual recordings through the outage. A loopback relay delays every RTMP handshake by 600 ms, exceeding the eight-frame output queue, to verify that slow connection setup does not trigger repeated restarts. Streaming starts at the newest complete A/V batch after connecting; recording keeps every batch. It opens only temporary loopback listeners. The C++ checks cover layout rendering, source history, audio gaps, ring wrap, gain and clock arithmetic. Failures return a nonzero exit code. CTest also checks idle/private previews and exercises the real Qt controls: scene preview/take, layer edits, mute, dual recording, a failed loopback broadcast, independent output stop, file finalization and capture shutdown. Qt UI tests run offscreen with synthetic sources and do not write normal studio preferences.

The portable engine and synthetic tests can also build on macOS or Linux with FFmpeg development libraries and pkg-config:

```sh
cmake -S . -B build/windows-portable
cmake --build build/windows-portable --parallel
ctest --test-dir build/windows-portable --output-on-failure
python3 tests/verify_media.py build/windows-portable/framecast-engine --network
```

Set `FRAMECAST_BUILD_UI=OFF` for an engine-only build. To build the portable UI, provide a desktop Qt SDK through `CMAKE_PREFIX_PATH` and set `FRAMECAST_BUILD_UI=ON`.

Native capture intentionally refuses to run on other operating systems. `.github/workflows/windows.yml` builds the Windows studio and engine, runs the synthetic suites and packages the executables with Qt runtime plugins when CI runs. For a local distributable developer folder run `windeployqt --release build/windows/Release/FramecastStudio.exe` and keep the resulting plugins and FFmpeg DLLs with the executables. CI cannot substitute for real capture-device testing.

## Feature and UI parity plan

The Mac app remains the visual and feature reference. The first Windows GUI is implemented against the engine's thread-safe LiveControl commands and immutable LiveState frame mailbox; media work stays off the interface thread.

| Area | This milestone | Remaining parity work |
|---|---|---|
| Capture | Display/window and webcam pickers, default mic and playback endpoint | app identity/relaunch, process audio, hotplug recovery, self-audio exclusion, permission onboarding |
| Scenes | Independent canvases, fit/fill/stretch, opacity, mirror, masks, eight layouts and saved collections | Mac collection import, backgrounds, assets, text, crops/rotation, filters, transitions, all templates |
| Output | H.264/AAC, three containers, simultaneous recording and RTMP destinations | Shared encoders, adaptive bitrate, HEVC, pause/split, multitrack audio, ISO files, replay and chapters |
| Studio UI | Qt studio, live previews, scenes, layer inspector, mixer, record/broadcast, settings and destinations | Direct canvas manipulation, undo/redo, advanced controls and remaining integrations |
| Guests and angles | Source slots can feed the compositor | LiveKit integration, browser guests, lobby/stage, return feed, camera links, alignment and shuffle |
| Captions and AI | Common audio timeline available | Local speech provider, Hindi/Hinglish validation, captions, rundown, prompter, rehearsal, AI director, vision and MCP |
| Privacy and integrations | Synthetic mode isolated; destination URLs suppressed | Private presenter windows, password detection, capture exclusions, credential storage, chat and shortcuts |
| Release | Windows x64 build, per-user installer, portable runtime packaging, dependency notices and clean-install qualification pipeline | Physical laptop hardware checks, signed installer and release support policy |

## Validation boundaries and next gate

Development verification on 2026-11-15: the portable CMake Release build and all 21 C++ core checks passed on the Mac. The media suite passed MKV, MP4 and MOV at 640x360 / 360x640 and 29 fps (87 frames each), with identical decoded audio and video timestamps across canvases. The loopback RTMP outage/reconnect test preserved all 330 frames in each of the two local recordings. These are synthetic software-encoder results, not native Windows or full-HD performance results. The Qt Release build, core checks, live-session test and UI integration suite also passed locally. The UI was inspected at 1440×900 and 1280×800, including the empty and private-preview states. Windows build and installer qualification results are recorded separately in the release evidence.

Local verification on the development Mac exercises the same portable compositor, mixer, encoders, muxers and streaming workers. Windows capture APIs, hardware encoder fallback, DPI/HDR/multiple displays, real microphone latency, echo/feedback, device loss, and sleep/resume require a Windows machine. RTMPS needs a TLS endpoint test; plain loopback RTMP is not evidence of platform RTMPS compatibility. Start with headphones when combining microphone and desktop capture: echo cancellation and monitor routing are not implemented yet.

Before a Windows release, compile and run the Windows target on a real Windows 11 x64 laptop; record both canvases while streaming, unplug/reconnect devices, and measure sync and dropped frames. Then replace CPU composition with a D3D11 GPU pipeline while keeping the synthetic tests as acceptance gates. Do not claim performance parity from low-resolution synthetic runs.

API references: [Windows Graphics Capture](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframepool), [WASAPI capture timestamps](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer), [FFmpeg muxing](https://www.ffmpeg.org/doxygen/8.0/group__lavf__encoding.html). The Windows dependency manifest avoids requesting FFmpeg GPL/nonfree features; shipping dependencies still need their license notices. Local verification may use a differently configured system FFmpeg build.
