# Windows qualification and migration

## Current preview: 0.1.2

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
