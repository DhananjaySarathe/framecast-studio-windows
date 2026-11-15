# Reliability rules

The first Windows previews passed every automated check and still froze and failed on a real laptop. This page records why, what mature projects do instead, and the rules this code now follows. Read it before changing capture, timing, encoding or the Add dialog.

## What went wrong

| Symptom on a real PC | Cause | Why the checks passed |
|---|---|---|
| Studio falls behind, black picture, "Recording cannot keep up" | Compositor needed 159 ms per 1080p frame pair with MSVC; one frame at 30 fps is 33 ms | Tests ran at 640×360 |
| Recording fails within a second on PCs without a vendor encoder | OpenH264 cannot encode two 1080p30 streams; the recording aborted after eight queued frames | Tests ran at 640×360 |
| Window freezes when **Add** is clicked | The UI thread waited for a worker that asked every window for its title. For Framecast's own window, Windows delivers that question to the waiting UI thread: a deadlock | Tests used synthetic sources and never listed real windows |
| Scene list looks empty | Thumbnails were only drawn from live video, and Add never got that far | Screenshots used synthetic sources |

Each was a different bug. The shared cause was that nothing exercised the real Windows path at the real size, and nothing reported what happened on the user's machine.

## What the reference projects do

Read from source: OBS Studio (`libobs`, `libobs-winrt`, `win-capture`, `win-wasapi`, `obs-ffmpeg`), Sunshine (`src/video.cpp`, `src/platform/windows`) and Microsoft's Win32CaptureSample.

- **Encoder lag never ends a recording.** OBS repeats or skips frames and counts them (`video-io.c`, `obs-video.c`); only an encoder error stops an output.
- **Encoders are proved before use.** Sunshine test-encodes a frame with each encoder and falls back in the order NVENC, Quick Sync, AMF, Media Foundation, software. OBS probes hardware encoders in helper processes at start-up.
- **Devices fail on their own.** OBS keeps rendering when a source has no data, retries a lost audio device every three seconds and follows default-device changes. Sunshine continues without audio when there is no device.
- **Timing is explicit.** Both raise the Windows timer resolution; OBS sleeps to absolute frame deadlines; Sunshine raises thread priorities.
- **The UI thread never waits on capture or encode work**, and anything that must wait is bounded.
- **Everything is logged**: the computer, the adapters, the devices, the encoder, and lagged and skipped frame counts when an output stops.
- **Frames stay on the GPU** in OBS, from capture to encoder. Sunshine also ships a CPU path like ours. A GPU pipeline is the next efficiency step here, not a correctness requirement.

## The rules

1. **Degrade, don't abort.** A slow encoder skips pictures and keeps audio (`OutputWorker::submit`). A late engine repeats the last picture (`runEngine`). Only a writer with twenty seconds queued, or an encoder error, fails an output.
2. **A device may fail at any time.** Screen and camera live in a `VideoSlot`: opened in the background, dropped on error, reopened every three seconds. Audio capture owns a thread that reopens its device and follows the default device. Failures become `LiveState::warnings`, never exceptions that end the session. The only fatal source error is a screen that cannot be opened at all when the session starts.
3. **An encoder is trusted only after a test encode** (`testEncode`), and forgotten when it fails mid-recording. The choice is made while the preview runs, or before the clock starts on the command line, never inside a recording's first frames.
4. **Never message a window from a thread the UI is waiting for.** Window titles come from `InternalGetWindowText`; Framecast's own windows are skipped; the UI pumps messages while devices are listed.
5. **Run as a real-time app.** `RealtimeScope` sets a 1 ms timer resolution, opts out of Windows 11 background throttling and raises the engine thread's priority. Frames sleep towards absolute deadlines.
6. **Leave a trail.** Every session writes a flushed log: the computer, devices, encoder tests, warnings, ten-second statistics and the result of each output. A watchdog thread records when the interface stops responding and the action it was in. A crash writes the exception and a minidump.
7. **Test the path people take, at the size they use.** `native-studio` drives the real window with real Windows devices: Add, device lists, Start preview, record, stop, check the files. `realtime-1080p` and `compose-1080p` run at full HD. `source-failures` breaks every device mid-session. `recording-backlog` stalls the writer. A change to capture, timing or encoding is not done until these pass on Windows CI.

## What automated checks still cannot see

Hosted Windows runners have no physical webcam, no vendor GPU encoder and usually no audio device. Those paths are covered by the failure-isolation design and by the session log, not by a pass. When a problem is reported, ask for the newest file from **Settings → Open logs folder**.
