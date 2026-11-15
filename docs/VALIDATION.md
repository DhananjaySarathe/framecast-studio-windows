# Windows validation and migration

The standalone project was extracted from the Windows-only source at commit `ebf791855edf971e965da02c0eab8fef376af98b` in the Mac repository. No Swift source, Mac package manifest, or Mac application resources are needed by this build. The Windows icon is included directly.

Original engine qualification: https://github.com/DhananjaySarathe/framecast-studio/actions/runs/43658035316

That run passed all four CTest suites without skips: core, live session, Qt studio UI, and real Windows Graphics Capture of a red/green/blue test window. MKV, MP4 and MOV each contained exactly 87 frames per canvas at 29 fps, with matching video timestamps and decoded 48 kHz stereo AAC audio. The RTMP test delayed each handshake by 600 ms, interrupted the receiver, verified resumed delivery, and retained all 420 frames in each local recording. Per-user installer execution, installed GUI launch with developer paths removed, installed dual recording and uninstall all passed.

The standalone build changes repository paths, adds setup/build/install helpers, and uses a Release-only dependency triplet. Its own Windows workflow requalifies those changes and exercises the checksum-verifying install helper against the newly packaged app. See this repository's Actions tab for results; the earlier run alone is not proof that new helpers work.

The hosted runner uses Windows Server 2025 with the v143 compiler toolset and Qt 6.8.3. The target laptop is Windows 11 Intel/AMD x64. Synthetic tests use 640×360 / 360×640 canvases; they do not establish full-HD performance. Real webcam/microphone devices, GPU encoders and driver combinations, HDR and multiple monitors, hotplug, sleep/resume and real streaming-service RTMPS compatibility still require laptop validation. The app is an unsigned developer preview, and remaining Mac feature parity is listed in ENGINE.md.
