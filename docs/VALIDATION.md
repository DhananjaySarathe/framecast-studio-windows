# Windows qualification and migration

## Current preview: 0.1.4

0.1.4 is the first preview checked on a physical laptop: an ASUS ROG Strix G17 (Windows 11 build 26200, Ryzen 7 4800H, 16 GB, RTX 3050 Ti Laptop GPU plus Radeon iGPU driving a 1920×1080 144 Hz panel at 100% scaling, Iriun phone webcam, Realtek microphone, Bluetooth headset). The owner's report on 0.1.3 (it didn't record, the camera couldn't be added, the window list was nearly empty) was traced through the session log and the code, and each cause is fixed:

- **Record with no preview recorded nothing.** It opened Add and stopped there. Record and Start broadcast now start the preview (asking for sources once if none were ever chosen) and begin the output after one second of preview, once the encoder test has settled. On the laptop, one press of Record produced both canvases 1.3 s after the preview started, with no skipped frames.
- **Add was disabled during a preview**, so the camera could never join. It now offers **Switch sources**; the preview restarted in 49 ms on a newly chosen window.
- **Minimised windows were left out of the window list.** They are listed as "(minimised)". Minimising the captured window keeps its last picture and shows a notice, which cleared within five seconds of restoring it.
- **Settings were forgotten** on every launch, and the canvas defaulted to 1280×720. Canvas size, frame rate, sources and destinations are now saved, the default is 1920×1080, and stream keys live in Windows Credential Manager.
- **Every recording began by skipping pictures.** Recordings may now queue up to a second of pictures until their encoder and file are open, instead of eight. On the laptop's two NVENC sessions, start-up skips fell from 7 to 0 (16:9) and from 19 to 2 (9:16), and the false "cannot encode every frame" warning, which also counted the frames a stream discards by design while connecting, no longer appears.
- **Stale state and a dialog race.** Record decided from engine state up to 50 ms old: clicked just after the preview ended, it stuck on "Stop recording". A queued Record could also start the preview underneath the open Add dialog. Both are fixed, and `studio-ui` now covers Record with the preview stopped (2 failures in 5 runs before, 8 of 8 passes after).
- **A silent camera now raises a notice.** A camera that stays connected but sends no pictures (a phone camera whose phone went away) blanked without a word. It now shows a notice after three seconds.

Measured on the laptop:

- **Encoder:** NVENC chosen automatically for both canvases; about 6 ms colour conversion and 1 ms encode per 1080p frame; studio render of both canvases plus monitors about 10 ms per frame, with no late frames while switching through all eight scenes.
- **Ten-minute show** (0.1.3 engine; capture, encoding and streaming are unchanged in 0.1.4): both canvases at 1080p30 recorded while streaming to a local mediamtx RTMP server, read back by a second client. 17,993 / 17,981 / 17,959 of 18,000 frames, every skip in the first two seconds, no reconnects. A two-minute repeat with 0.1.4 skipped 0 and 2 frames; that run's stream reconnected once at start-up while the local server took three seconds to accept the publish, then ran cleanly.
- **A/V sync:** `tests/av_sync_real.py` (new) played a flash-and-beep stimulus and measured the captured screen against WASAPI loopback: sound 18 ms ahead of picture (median) in both canvases and in the received stream, all 600 events matched, 21 ms drift over ten minutes.
- **Load:** about 23% total CPU and 42% NVIDIA video-encoder use while recording both 1080p canvases.
- **Closing while recording:** the app asks, then finalizes both files (136.6 s each, no decode errors).
- **Scaling:** with Qt's scale factor at 125% and 150% the window fits the 1080p work area and the transport stays reachable. True Windows display scaling and multiple displays were not available.

Evidence: [Windows CI on the released commit](https://github.com/DhananjaySarathe/framecast-studio-windows/actions/runs/43730049296) ran all eight CTest suites, the media and RTMP checks and the installer check. The [tag build](https://github.com/DhananjaySarathe/framecast-studio-windows/actions/runs/43730511779) produced the [published binaries](https://github.com/DhananjaySarathe/framecast-studio-windows/releases/tag/v0.1.4) (Setup.exe SHA-256 `c89e34a5b4fe3eb961bb2abd4bfbb4eba8296c2b2312b2e763e613df2fcb7149`), which were also installed and recorded with on the laptop, and the [published-release test](https://github.com/DhananjaySarathe/framecast-studio-windows/actions/runs/43731184250) downloaded, verified, installed, recorded with and uninstalled them. The 0.1.4 executable's file properties still read 0.1.1, a stale version resource carried since 0.1.1; the installer and the Windows Apps list report 0.1.4, and the resource follows the project version from the next release.

Not yet checked on hardware: unplugging the camera and headset mid-show (the automated `source-failures` suite covers the code path), the microphone privacy-denied message, vendor encoders other than NVENC, and real streaming services.

## Previous preview: 0.1.3

0.1.3 follows a study of how OBS Studio, Sunshine and Microsoft's capture sample handle the same problems; the findings and the rules taken from them are in [RELIABILITY.md](RELIABILITY.md). It fixes the studio freezing when **Add** is clicked and changes how the engine behaves when something goes wrong.

- **Add no longer freezes.** The window list no longer sends messages to windows (Framecast's own window had to answer one while it was waiting for the list). `native-window-capture` now lists windows from a worker while the UI thread waits and requires it to return.
- **Real Windows path tested.** `native-studio` drives the real window with real Windows devices and no synthetic sources: Add, device lists, Start preview on the first display, record both canvases, stop. On the hosted runner (Windows build 26100, no audio devices, no GPU): one display and one window were listed in 6 ms; the preview captured the display; missing microphone and desktop sound appeared as warnings while the session carried on; both recordings got 62 of 62 frames with none skipped and none late.
- **Devices fail on their own.** `source-failures` runs a session through a busy camera, a screen capture that breaks mid-session, a late microphone and desktop sound that cannot be opened. The recording completes, each failure shows as a warning, and the sources come back without a restart.
- **Slow encoders cost pictures, not recordings.** `recording-backlog` stalls the writer: the recording completes with skipped pictures counted; only a writer that never recovers fails.
- **Encoders are test-encoded before use.** On the runner NVENC, Quick Sync, AMF and Media Foundation hardware were rejected and Media Foundation's software encoder passed.
- **Evidence.** [Windows CI on the released commit](https://github.com/DhananjaySarathe/framecast-studio-windows/actions/runs/43718068994) ran all eight CTest suites, the media and RTMP checks and the installer check. The [tag build](https://github.com/DhananjaySarathe/framecast-studio-windows/actions/runs/43718607813) produced the [published binaries](https://github.com/DhananjaySarathe/framecast-studio-windows/releases/tag/v0.1.3), and the [published-release test](https://github.com/DhananjaySarathe/framecast-studio-windows/actions/runs/43719170821) downloaded, verified, installed, recorded with and uninstalled them.
- **Session log.** Each session writes the computer, devices, encoder tests, warnings, statistics and output results to a log, with a watchdog entry if the interface stops responding.

## Previous preview: 0.1.2

0.1.2 fixes the studio falling behind real time on laptops. That showed up as hangs, a black screen and recordings failing with "Recording cannot keep up". Evidence from [Windows CI run 43713599413](https://github.com/DhananjaySarathe/framecast-studio-windows/actions/runs/43713599413) (Windows Server 2025, MSVC v143, no GPU):

- Composing both 1080p canvases took **158.8 ms** per frame with the 0.1.1 compositor, against a 33 ms frame at 30 fps. The 0.1.2 compositor takes **8.8 ms** and produces pixel-identical output (`compose-1080p`).
- A preview followed by a landscape and a portrait recording at 1920×1080 / 1080×1920: 182 of 182 frames in each, none dropped, 0 of 225 late frames (`realtime-1080p`). No vendor encoder was available, so automatic selection used Media Foundation `h264_mf`, at about 7 ms colour conversion and 1.2 ms encode per frame.
- The H.264 encoders in the packaged FFmpeg are `libopenh264 h264_amf h264_d3d12va h264_mf h264_nvenc h264_qsv`. OpenH264 alone could not keep two streams at 1080p30, or even 720p30, on that runner. It remains the last fallback, at 720p.

- [Tag build of v0.1.2](https://github.com/DhananjaySarathe/framecast-studio-windows/actions/runs/43714032061) produced the [published binaries](https://github.com/DhananjaySarathe/framecast-studio-windows/releases/tag/v0.1.2). The [published-release test](https://github.com/DhananjaySarathe/framecast-studio-windows/actions/runs/43714685310) downloads them, checks the checksum, installs, launches, records both canvases and uninstalls. It passed.

Other changes: the studio monitors receive 960-wide renders instead of full-size frames. Screen readback is limited to about 1.5 times the output rate. Webcams open in a native 30 fps mode near 720p. The encoder choice is settled while the preview runs.

## Previous preview: 0.1.1

Qualified on 2026-11-15. The published installer and portable ZIP were built from commit `50e8dc8b6a1ab90a0b199104b8b68e2ef70dbf87` in this standalone Windows repository.

- [Main-branch Windows build, native/core/UI/media tests, packaging and installed-runtime checks](https://github.com/DhananjaySarathe/framecast-studio-windows/actions/runs/43707862308): passed.
- [Published 0.1.1 install.cmd download, checksum verification, installation, GUI launch, dual recording and uninstall](https://github.com/DhananjaySarathe/framecast-studio-windows/actions/runs/43708131231): passed.
- [Qualified release build used for the published binaries](https://github.com/DhananjaySarathe/framecast-studio-windows/actions/runs/43707503541): passed.
- [Preview downloads and checksums](https://github.com/DhananjaySarathe/framecast-studio-windows/releases/tag/v0.1.1).

All four Windows CTest suites passed without skips: core, live session, Qt studio UI and real Windows Graphics Capture of a red/green/blue test window. MKV, MP4 and MOV each contained exactly 87 frames per canvas at 29 fps, with matching video timestamps and decoded 48 kHz stereo AAC audio. The RTMP test delays each handshake by 600 ms, interrupts the receiver, verifies resumed delivery and retains all 420 frames in each local recording.

The command launchers were exercised through Windows PowerShell, including SDK setup, CMake builds and packaging through build.cmd. Installation checks reject a wrong checksum before execution. The published-release test downloads the public release through install.cmd, launches the normal Windows GUI with developer Qt/vcpkg paths removed, verifies actual dual-canvas files and uninstalls the app. This install path does not require a compiler, Qt SDK or Python on the user's laptop; Python and ffprobe are used only by the acceptance verifier.

0.1.1 also fixes a recording failure-status race: a full recording queue during slow output startup could previously be overwritten by live/success status for a partial file. A blocked-FIFO output reproduces the old behavior on the portable core; its regression now preserves failed status and the original error. That test passes locally alongside the core, session and UI suites. Windows uses the same corrected worker implementation. Console fixture processes are isolated, and their output is retained for diagnosis.

## Qualification boundaries

The hosted runner uses Windows Server 2025 with the v143 compiler toolset and Qt 6.8.3. The target laptop is Windows 11 Intel/AMD x64. The media checks use 640×360 / 360×640 canvases. Full-HD timing is covered by the compose-1080p and realtime-1080p tests, with synthetic sources on a hosted runner. Real webcams, microphone latency, GPU encoders and driver combinations, HDR and multiple monitors, hotplug, sleep/resume and real streaming-service RTMPS compatibility still require laptop validation. This is an unsigned developer preview, and remaining Mac feature parity is listed in ENGINE.md.

## Migration provenance

The initial Windows source was extracted from commit `ebf791855edf971e965da02c0eab8fef376af98b` in the Mac repository. Its [original engine qualification](https://github.com/DhananjaySarathe/framecast-studio/actions/runs/43658035316) is historical evidence. Current releases and command validation are owned by this repository.

No Swift source, Mac package manifest or Mac application resources are needed by this build. The Windows icon is included directly. The Mac working tree was restored to its original state, Windows source/build/review files were moved to the sibling Windows folder, and the misplaced Windows branch was removed from the Mac remote. Original generated files are retained only in ignored migration backups under this Windows folder's build directory.
