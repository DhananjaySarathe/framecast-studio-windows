"""Measure audio/video sync in a real recording made from a flash-and-beep stimulus.

The synthetic media test proves the engine's own clock. This checks the path people use: the display is
captured by Windows Graphics Capture and the speakers by WASAPI loopback, each with its own latency.

  python tests/av_sync_real.py stimulus OUT.mkv --seconds 660   # make the stimulus clip
  ffplay -fs -autoexit OUT.mkv                                  # play it full screen while recording
  python tests/av_sync_real.py measure RECORDING.mkv            # report offsets and drift

Every second the stimulus shows a white frame for 100 ms and plays a 1 kHz beep for the same 100 ms.
Positive offsets mean the sound is late relative to the picture. Needs ffmpeg and ffprobe on PATH.
"""
import argparse
import array
import statistics
import subprocess
import sys


def run(args, **kwargs):
    return subprocess.run(args, check=True, capture_output=True, **kwargs)


def stimulus(path, seconds):
    video = ("color=c=black:s=1920x1080:r=60,"
             "drawbox=x=0:y=0:w=iw:h=ih:color=white:t=fill:enable='lt(mod(t\\,1)\\,0.1)'")
    audio = "sine=f=1000:r=48000,volume=0.6:enable='lt(mod(t\\,1)\\,0.1)',volume=0:enable='gte(mod(t\\,1)\\,0.1)'"
    run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i", video, "-f", "lavfi", "-i", audio,
         "-t", str(seconds), "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p",
         "-c:a", "aac", "-ac", "2", path])
    print(f"wrote {path} ({seconds} s)")


def frame_times_and_luma(path):
    times = [float(t) for t in run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                                    "frame=pts_time", "-of", "csv=p=0", path], text=True).stdout.split()
             if t.strip()]
    # passthrough: one picture per stored frame. Otherwise ffmpeg fills skipped-frame gaps with copies and
    # every later picture is paired with the wrong timestamp.
    raw = run(["ffmpeg", "-v", "error", "-i", path, "-map", "0:v:0", "-fps_mode", "passthrough",
               "-vf", "scale=64:36,format=gray", "-f", "rawvideo", "-"]).stdout
    size = 64*36
    luma = [sum(raw[n:n+size])/size for n in range(0, len(raw) - size + 1, size)]
    return times[:len(luma)], luma


def audio_envelope(path):
    start = float(run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=start_time",
                       "-of", "csv=p=0", path], text=True).stdout.strip() or 0)
    raw = run(["ffmpeg", "-v", "error", "-i", path, "-map", "0:a:0", "-ac", "1", "-ar", "48000", "-f", "f32le",
               "-"]).stdout
    samples = array.array("f")
    samples.frombytes(raw[:len(raw)//4*4])
    window = 48  # 1 ms
    env = [max(abs(v) for v in samples[n:n+window]) for n in range(0, len(samples) - window, window)]
    return start, env


def onsets(values, times, gap):
    # Halfway between the background and a typical event, not the loudest one: a notification sound or a
    # bright window in the recording must not hide every stimulus event.
    low = statistics.median(values)
    span = max(1, round(1/max(times[1] - times[0], 1e-6))) if len(times) > 1 else 1
    peaks = [max(values[n:n+span]) for n in range(0, len(values), span)]
    peaks = [p for p in peaks if p - low > 1e-3]
    if not peaks:
        return []
    threshold = low + (statistics.median(peaks) - low)*.5
    out = []
    for n in range(1, len(values)):
        if values[n] > threshold >= values[n-1] and (not out or times[n] - out[-1] > gap):
            out.append(times[n])
    return out


def measure(path):
    times, luma = frame_times_and_luma(path)
    flashes = onsets(luma, times, .5)
    start, env = audio_envelope(path)
    beeps = onsets(env, [start + n/1000 for n in range(len(env))], .5)
    if len(flashes) < 3 or len(beeps) < 3:
        print(f"FAIL not enough events: {len(flashes)} flashes, {len(beeps)} beeps "
              "(was the stimulus visible and audible while recording?)")
        return 1
    pairs = []
    for f in flashes:
        b = min(beeps, key=lambda t: abs(t - f))
        if abs(b - f) < .4:
            pairs.append((f, b - f))
    offsets = [o*1000 for _, o in pairs]
    third = max(1, len(pairs)//3)
    early = statistics.mean(offsets[:third])
    late = statistics.mean(offsets[-third:])
    print(f"events: {len(flashes)} flashes, {len(beeps)} beeps, {len(pairs)} matched over "
          f"{pairs[-1][0] - pairs[0][0]:.0f} s")
    print(f"offset ms (audio minus video): mean {statistics.mean(offsets):+.1f}, median "
          f"{statistics.median(offsets):+.1f}, min {min(offsets):+.1f}, max {max(offsets):+.1f}")
    print(f"drift: first third {early:+.1f} ms, last third {late:+.1f} ms, change {late - early:+.1f} ms")
    # Broadcast guidance (EBU R37 / ITU-R BT.1359): sound no more than ~40 ms early or ~60 ms late.
    ok = -40 <= statistics.median(offsets) <= 60 and abs(late - early) <= 20
    print("PASS" if ok else "FAIL", "A/V sync within -40/+60 ms and drift within 20 ms")
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    s = sub.add_parser("stimulus")
    s.add_argument("path")
    s.add_argument("--seconds", type=int, default=90)
    m = sub.add_parser("measure")
    m.add_argument("path")
    args = parser.parse_args()
    if args.command == "stimulus":
        stimulus(args.path, args.seconds)
        return 0
    return measure(args.path)


if __name__ == "__main__":
    sys.exit(main())
