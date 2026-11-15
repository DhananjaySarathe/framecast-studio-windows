You are Claude Code running on my Windows 11 laptop. Your job is to make Framecast Studio Windows work properly on this real machine and then bring it to full feature parity with the Mac app. Work steadily through the phases below. Verify everything on this laptop: earlier versions passed CI and still froze here.

## Accounts and repositories

- GitHub account: `DhananjaySarathe`. `gh` and `git` should be signed in to it. If they aren't, run `gh auth login`, then `gh auth setup-git`.
- Windows app (your main repo): https://github.com/DhananjaySarathe/framecast-studio-windows
- Mac app (the feature and design reference; read it, don't change it): https://github.com/DhananjaySarathe/framecast-studio
- Website: https://github.com/DhananjaySarathe/framecast-site, live at https://dhananjaysarathe.github.io/framecast-site/ (GitHub Pages from `main`)

Clone all three side by side, for example in `%USERPROFILE%\Projects`:

```powershell
winget install --exact --id Git.Git
winget install --exact --id GitHub.cli
cd $HOME; mkdir Projects -Force; cd Projects
git clone https://github.com/DhananjaySarathe/framecast-studio-windows.git
git clone https://github.com/DhananjaySarathe/framecast-studio.git
git clone https://github.com/DhananjaySarathe/framecast-site.git
```

## Read first

In framecast-studio-windows: `README.md`, `docs/RELIABILITY.md` (why it broke, and the rules the code follows), `docs/ENGINE.md` and `docs/VALIDATION.md`. In framecast-studio: `README.md` and the folders `Sources/Framecast` (app and UI) and `Sources/FramecastEngine` (engine).

## Where things stand (version 0.1.3, released 2026-11-15)

- Stack: C++20, Qt 6.8.3 Widgets UI, FFmpeg through vcpkg (a pinned baseline), Windows Graphics Capture for screen and window, Media Foundation for the webcam, WASAPI for the mic and desktop sound, H.264 + AAC, MKV/MP4/MOV, RTMP/RTMPS. Inno Setup installer.
- Folders: `src/` engine (core.cpp compositor, engine.cpp session loop, output.cpp encoders, windows_capture.cpp devices), `ui/` Qt studio, `tests/`, `scripts/` (setup, build, package, install), `installer/`.
- Done and passing on Windows CI:
  - CPU compositor, about 10 ms for both 1080p canvases.
  - Encoders test-encoded before use, in this order: NVENC, QSV, AMF, h264_mf hardware, h264_mf, OpenH264 (OpenH264 records at 720p).
  - Devices fail on their own and are retried every 3 s. A slow encoder skips pictures instead of ending the recording.
  - 1 ms timers, opted out of background throttling.
  - Session log in `%LOCALAPPDATA%\Framecast\Framecast Studio\logs`, a freeze watchdog, and a crash minidump.
  - The Add-dialog deadlock is fixed.
  - Placeholder scene thumbnails.
  - Eight camera/screen layouts, dual 16:9 + 9:16 canvases, preview / put on air, mixer, multiple RTMP destinations.
- Never tested on real hardware: a physical webcam, a real microphone, vendor GPU encoders, high-DPI or multiple displays, a real streaming service. This laptop is the first real test.
- Owner's report on earlier versions: the app hung; clicking Add froze it; the scene list was blank; it didn't record. 0.1.3 is meant to fix all of that and hasn't been confirmed here yet.

## Phase 1: make it work on this laptop (do this first, report before moving on)

1. Collect facts. Note the Windows build, CPU, GPU, displays and DPI scaling, and any webcam and microphone (`Get-ComputerInfo`, `Get-PnpDevice -Class Camera,AudioEndpoint`).
2. Install the released 0.1.3 and run it:
   `curl.exe -LO https://github.com/DhananjaySarathe/framecast-studio-windows/releases/download/v0.1.3/Framecast-Studio-Windows-0.1.3-x64-Setup.exe`, then run it with `/VERYSILENT`.
3. Exercise the real path through the UI. You can control the desktop (computer use), and the CLI and logs work as well:
   - Open the app. Click Add and pick the display, the webcam, the mic and desktop sound. Click Start preview.
   - Switch through every scene. Record for 60 s and stop. Play both files. Check that sound and picture stay in sync.
   - Stream to a local RTMP server as well. Get `mediamtx` from its GitHub releases, or use FFmpeg listening on `rtmp://127.0.0.1/live/test`.
   - Also run `"%LOCALAPPDATA%\Programs\Framecast Studio\framecast-engine.exe" --monitor 0 --camera 0 --mic --desktop-audio --record "%USERPROFILE%\Videos\fc-test" --seconds 30`. It prints the encoder and per-frame timings.
4. Read the newest session log. Every warning, freeze-watchdog entry, late-frame count, skipped count and encoder rejection is a lead.
5. Set up the build so you can fix things. It takes a while, several GB, and admin rights for the C++ tools:

   ```powershell
   winget install --exact --id Python.Python.3.12
   winget install --exact --id Kitware.CMake
   winget install --exact --id Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
   winget install --exact --id JRSoftware.InnoSetup
   winget install --exact --id Gyan.FFmpeg
   cd $HOME\Projects\framecast-studio-windows; .\setup.cmd; .\build.cmd -Test
   ```

   Run your own build with `.\build\windows\Release\FramecastStudio.exe`.
6. Fix every failure you find. Find the root cause, write a test that reproduces it when that's possible, and re-check on this laptop. Check these known weak spots in particular:
   - Webcam formats: MJPG/NV12 conversion and its speed, a camera already in use, mirroring.
   - Microphone privacy permission (0x80070005 gives a clear message).
   - Per-monitor DPI and the window size on a scaled display.
   - Capturing a window that is then minimised.
   - Hardware encoder choice on this GPU: is NVENC, QSV or AMF actually chosen?
   - CPU and GPU use during a 1080p recording (record it in the report).
   - Settings persistence. Audio sync over a 10-minute recording.
   - Closing the app while recording: the files must be finalized.

Phase 1 is done when you can do all of this on this laptop without a freeze:

- Add, Start preview, switch scenes, record 10 minutes on both canvases and stream to local RTMP at the same time.
- Unplug and replug the webcam and headset mid-show; the show must survive.

Then give the owner a short report (what broke, what you fixed, the numbers) and release it (see the release process below).

## Phase 2: full parity with the Mac app

The owner wants every Mac feature on Windows. For each one: find how the Mac does it (file names below), build the Windows equivalent, test it on this laptop, and record it in `docs/ENGINE.md`. Where the Mac relies on Apple-only technology, use the Windows replacement listed. If a feature truly can't be done, tell the owner why and what the closest alternative is; don't drop it silently.

| Mac feature (Mac source) | Windows approach | Feasibility |
|---|---|---|
| Backgrounds and wallpapers (`Compositor/Wallpapers.swift`) | Draw the gradients into the CPU compositor as a layer | Straightforward |
| Text, lower thirds, Intro/Break/Outro/Headline cards, QR, thank-you overlays (`OverlayRenderer.swift`, `ElementsPanel.swift`, `CardMaker.swift`, `Looks.swift`) | Render text and shapes with Qt (QPainter into QImage) on the UI side, pass them to the engine as image sources, and add a "text/image" layer kind to `Scene` | Straightforward |
| Media sources: images, video files, creator pack and asset pack (`MediaSources.swift`, `PackSources.swift`, `AssetPack.swift`, `PackLayouts.swift`) | Decode with FFmpeg into frame sources | Straightforward |
| Remaining layouts: camera-left, split variants, Conversation (podcast) (`Model/Templates.swift`) | Port the geometry into `ui/studio_model.cpp` | Straightforward |
| Animated transitions, Motion, filters (`Motion.swift`, `Filters.swift`, `LiveEditor/Spring.swift`) | Interpolate layer geometry and opacity per frame in the engine | Medium |
| Direct canvas dragging, undo/redo (`Undo.swift`, `LiveEditor*.swift`) | Qt mouse handling on CanvasView; a command stack | Medium |
| Replay buffer (`Output/ReplayBuffer.swift`) | Ring buffer of encoded packets that dumps to a file | Medium |
| Pre-flight checks and upload speed test (`Preflight.swift`) | Disk space, devices, `speed.cloudflare.com` | Straightforward |
| Destinations with keys stored safely (`Destinations.swift`; Mac uses the Keychain) | Windows Credential Manager (`CredWriteW`/`CredReadW`) in place of the session-only keys | Straightforward |
| Chat (Google/YouTube, Twitch) (`Chat.swift`, `ChatConnectors.swift`) | Same OAuth/HTTP APIs with Qt Network and the system browser for OAuth | Medium |
| AI show planning and the live director (`ShowDirector.swift`, `LiveBrain.swift`, `BrainDirector.swift`, `ScriptWriter.swift`, `ShowRundown.swift`, `CueMatcher.swift`, `ShowMoments.swift`, `AISettings.swift`) | Same Claude/OpenAI HTTPS calls on the owner's API key (keys in Credential Manager). Port the prompts and logic. | Medium; needs speech-to-text (next row) |
| Following your voice and live captions with an .srt (`ShowListener.swift`, `CaptionEngine.swift`, `CaptionRenderer.swift`, `Captions/`; Mac uses Apple on-device speech recognition) | Apple's recogniser doesn't exist on Windows. Use whisper.cpp on-device (MIT; ship a small English/multilingual model, GPU via Vulkan/CUDA when present). Windows' built-in `Windows.Media.SpeechRecognition` is lower quality. Hindi + English mixed: whisper multilingual handles it reasonably | Possible; needs a model download (~150–500 MB) and enough CPU/GPU |
| Teleprompter and presenter view (`PrompterView.swift`, `PrivatePanel.swift`, `ScriptTracker.swift`) | A second Qt window, excluded from capture with `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` | Straightforward |
| Remote guests (`Guests.swift`, `GuestServer.swift`, `GuestNetwork.swift`; LiveKit) | LiveKit has a C++ client SDK, and the guest web page is shared with the Mac (`scripts/export_guest_page.sh`). Host a local LiveKit server, or the owner's LiveKit Cloud. Windows Firewall prompt on first use | Larger job; possible |
| Camera angles and virtual framing (`CameraAngles.swift`, `AngleBrain.swift`, `Angles/VirtualFraming.swift`, `ShufflePlanner.swift`) | Virtual framing is crop/zoom math (portable). Face/person tracking: Mac uses Apple Vision; on Windows use ONNX Runtime with a small face detector (MIT models), or Windows.Media.FaceAnalysis | Possible; detector choice needed |
| Screen auto-zoom (`LiveEditor/FocusController.swift`, `Framing.swift`) | Cursor position via `GetCursorInfo`; zoom math is portable | Straightforward |
| Control link for Claude Desktop / Claude Code (`ControlLink.swift`) | Same local protocol, served on localhost from the engine | Straightforward |
| Onboarding, settings and the Mac's visual design (`OnboardingView.swift`, `SettingsView.swift`, `Theme.swift`, `ui/DESIGN.md`) | Port to Qt | Medium |

Mac-only by nature, and how to handle each:
- Apple Speech and Apple Vision: replace as listed in the table.
- "macOS 26 needed for voice": not applicable. On Windows the requirement becomes "enough CPU/GPU for whisper"; detect it and say so.
- Keychain: Credential Manager.
- Gatekeeper prompts: Windows SmartScreen. Only code signing (an Authenticode certificate, paid) removes that warning. Tell the owner rather than hiding it.

Keep performance in mind as features land. Overlays, transitions and captions all add per-frame work, and the CPU compositor has headroom of roughly 20 ms per frame. If 1080p30 with everything on drops frames on this laptop, move composition to the GPU: D3D11 textures from capture, shader composition, NV12 conversion on the GPU, and passing the texture to the encoder where FFmpeg supports it. That is how OBS stays fast.

## Rules

- Never block the UI thread on capture, encoding, the network or device enumeration (`docs/RELIABILITY.md`). Degrade instead of aborting. Log every new failure path. Test the real path at real size.
- Work on a branch, keep commits small, push, and watch Windows CI (`.github/workflows/windows.yml`). Merge to `main` only when CI is green and you have checked the change on this laptop.
- Never put stream keys, API keys or personal data in logs, commits or tests.

## Release process (as used for 0.1.1–0.1.3)

1. Bump the version in `CMakeLists.txt`, `installer/framecast.iss`, `scripts/package.ps1`, `scripts/install-windows.ps1`, `.github/workflows/install-release.yml`, the links in `README.md` and `docs/ENGINE.md`, and add a section to `docs/VALIDATION.md`.
2. Merge to `main`, tag `vX.Y.Z`, push the tag, and wait for the "Windows studio" workflow on the tag.
3. Download its `framecast-windows-installer-x64` artifact and check the SHA256SUMS. Then `gh release create vX.Y.Z --prerelease` with the Setup.exe, the Portable.zip and SHA256SUMS.txt. Add "(superseded)" to the previous release's title.
4. Run `gh workflow run install-release.yml -f version=X.Y.Z` and confirm it passes.
5. Update the README checksum. In framecast-site, replace the old version, the hash and the sizes in `index.html` (search for the old version string), then push. Pages redeploys within a minute.

Start with Phase 1 now. Keep the owner updated in short plain messages: what you're checking, what you found, what you fixed.
