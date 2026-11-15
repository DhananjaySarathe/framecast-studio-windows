#!/usr/bin/env python3
"""Offline media acceptance checks. All streaming stays on loopback; no capture permissions needed."""
import argparse
import array
from contextlib import nullcontext
import json
import math
import os
from pathlib import Path
import selectors
import shutil
import socket
import subprocess
import tempfile
import threading
import time


def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, capture_output=True, **kwargs)


def probe(path):
    try:
        return json.loads(run(["ffprobe", "-v", "error", "-count_frames", "-show_streams", "-show_format", "-of", "json", path]).stdout)
    except subprocess.CalledProcessError as error:
        raise RuntimeError(f"Cannot inspect {path}: {error.stderr.decode(errors='replace')}") from error


def pcm(path):
    data = run(["ffmpeg", "-v", "error", "-i", path, "-map", "0:a:0", "-f", "f32le", "-acodec", "pcm_f32le", "-ac", "2", "-ar", "48000", "pipe:1"]).stdout
    samples = array.array("f")
    samples.frombytes(data)
    if os.sys.byteorder != "little":
        samples.byteswap()
    return samples


def inspect_pair(folder, fps, frames):
    files = sorted(Path(folder).glob("Framecast-*"))
    assert len(files) == 2, f"Expected two canvas files, found {files}"
    infos = [probe(f) for f in files]
    videos = [next(s for s in info["streams"] if s["codec_type"] == "video") for info in infos]
    assert {(s["width"], s["height"]) for s in videos} == {(640, 360), (360, 640)}
    for video, info in zip(videos, infos):
        assert int(video["nb_read_frames"]) == frames, video
        assert video["codec_name"] == "h264", video
        audio = next(s for s in info["streams"] if s["codec_type"] == "audio")
        assert audio["codec_name"] == "aac" and audio["sample_rate"] == "48000" and audio["channels"] == 2
    sound = [pcm(f) for f in files]
    assert len(sound[0]) == len(sound[1]) and sound[0] == sound[1], "Canvases have different decoded audio"
    timelines = []
    for file in files:
        timeline = json.loads(run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_frames", "-show_entries", "frame=best_effort_timestamp_time", "-of", "json", file]).stdout)
        timelines.append([float(f["best_effort_timestamp_time"]) for f in timeline["frames"]])
    assert timelines[0] == timelines[1], "Canvas video timestamps differ"
    assert all(abs((b-a)-1/fps) <= .0011 for a, b in zip(timelines[0], timelines[0][1:])), "Video cadence is discontinuous"
    flashes = []
    for file in files:
        values = list(run(["ffmpeg", "-v", "error", "-i", file, "-map", "0:v:0", "-vf", "scale=1:1", "-fps_mode", "passthrough", "-pix_fmt", "gray", "-f", "rawvideo", "pipe:1"]).stdout)
        median = sorted(values)[len(values)//2]
        assert max(values)-median > 25, "Rendered test picture is black or lacks flashes"
        threshold = median+(max(values)-median)*.5
        flashes.append([n for n, value in enumerate(values) if value > threshold])
    assert flashes[0] == flashes[1], f"Canvas flash frames differ: {flashes}"
    assert flashes[0] == list(range(0, frames, fps)), flashes
    # AAC has a short priming/tail interval. Compare actual decoded onset times after the first second.
    samples = sound[0]
    audio_frames = json.loads(run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_frames", "-show_entries", "frame=best_effort_timestamp_time", "-of", "json", files[0]]).stdout)["frames"]
    audio_start = float(audio_frames[0]["best_effort_timestamp_time"])
    onsets = []
    for n in range(48000, len(samples)//2-48, 48):
        peak = max(abs(x) for x in samples[n*2:(n+48)*2])
        if peak > .1 and (not onsets or n/48000-onsets[-1] > .5):
            onsets.append(audio_start+n/48000)
    offsets = []
    for second in range(1, math.ceil(frames/fps)):
        if second*fps >= frames:
            continue
        flash_time = timelines[0][second*fps]
        offset = min(abs(t-flash_time) for t in onsets) if onsets else float("inf")
        assert offset <= 1/fps+.005, (flash_time, onsets)
        offsets.append(offset)
    print(f"PASS {files[0].suffix}: {frames} frames per canvas, matching video timestamps and decoded audio, max A/V onset error {max(offsets, default=0)*1000:.1f} ms")
    return files


def receiver(port, destination):
    with destination.with_suffix(".log").open("wb") as log:
        return subprocess.Popen(["ffmpeg", "-v", "error", "-listen", "1", "-analyzeduration", "500000", "-probesize", "1048576", "-i", f"rtmp://127.0.0.1:{port}/live/test",
                                 "-c", "copy", "-cluster_time_limit", "500", "-flush_packets", "1", "-y", str(destination)], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=log)


class DelayedRelay:
    """Real loopback RTMP transport with setup slower than the eight-frame queue."""
    def __init__(self, receiver_port):
        self.receiver_port = receiver_port
        self.stopped = threading.Event()
        self.clients = []
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.port = self.listener.getsockname()[1]
        self.listener.listen()
        self.listener.settimeout(.1)
        self.thread = threading.Thread(target=self.accept, daemon=True)
        self.thread.start()

    def accept(self):
        while not self.stopped.is_set():
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            worker = threading.Thread(target=self.forward, args=(client,), daemon=True)
            self.clients.append(worker)
            worker.start()

    def forward(self, client):
        try:
            with client, socket.create_connection(("127.0.0.1", self.receiver_port), timeout=1) as server:
                if self.stopped.wait(.6):
                    return
                client.settimeout(1)
                with selectors.DefaultSelector() as selector:
                    selector.register(client, selectors.EVENT_READ, server)
                    selector.register(server, selectors.EVENT_READ, client)
                    while not self.stopped.is_set():
                        for key, _ in selector.select(.1):
                            data = key.fileobj.recv(65536)
                            if not data:
                                return
                            key.data.sendall(data)
        except OSError:
            pass  # The deliberately stopped receiver refuses/closes connections.
        finally:
            client.close()

    def close(self):
        self.stopped.set()
        self.listener.close()
        self.thread.join(timeout=2)
        for client in self.clients:
            client.join(timeout=2)


def stop_receiver(process):
    if process and process.poll() is None:
        try:
            # FFmpeg's q command also works on Windows, where terminate() is a
            # hard kill and can interrupt muxer header/trailer writes.
            process.communicate(input=b"q\n", timeout=5)
        except subprocess.TimeoutExpired:
            terminate(process)


def terminate(process):
    if process and process.poll() is None:
        if os.name == "nt":
            # Chocolatey's ffmpeg.exe can be a launcher. Terminating only that
            # parent leaves its receiver alive and never creates the outage.
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("engine", type=Path)
    parser.add_argument("--network", action="store_true", help="Also test RTMP on loopback, outage, reconnect and recording isolation")
    parser.add_argument("--evidence", type=Path, help="Retain media and receiver logs for CI diagnosis")
    args = parser.parse_args()
    assert shutil.which("ffmpeg") and shutil.which("ffprobe"), "Install ffmpeg and ffprobe for media checks"
    engine = args.engine.resolve()
    engine_env = os.environ.copy()
    for name in ("FRAMECAST_STREAM_HORIZONTAL", "FRAMECAST_STREAM_VERTICAL"):
        engine_env.pop(name, None)
    if args.evidence:
        args.evidence.mkdir(parents=True, exist_ok=True)
    with (nullcontext(str(args.evidence.resolve())) if args.evidence else tempfile.TemporaryDirectory(prefix="framecast-windows-verify-")) as temporary:
        root = Path(temporary)
        for container in ("mkv", "mp4", "mov"):
            folder = root/container
            result = run([engine, "--synthetic", "--record", folder, "--size", "640x360", "--fps", "29", "--seconds", "3", "--container", container], timeout=30, env=engine_env)
            assert b"failed" not in result.stderr and b"dropped=0" in result.stderr, result.stderr.decode()
            inspect_pair(folder, 29, 87)
        # Unusable output must return a failing process status, not just print FAIL.
        bad = subprocess.run([str(engine), "--synthetic", "--record", str(root/"bad"), "--encoder", "missing-framecast-encoder", "--seconds", "1", "--size", "640x360"], capture_output=True, timeout=15, env=engine_env)
        assert bad.returncode != 0 and b"failed" in bad.stderr
        print("PASS unavailable encoder is reported as an output failure")
        if args.network:
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            first, second, studio = None, None, None
            relay = DelayedRelay(port)
            seconds = 14
            folder = root/"network"
            try:
                first = receiver(port, root/"received-first.mkv")
                time.sleep(.5)
                studio = subprocess.Popen([str(engine), "--synthetic", "--record", str(folder), "--size", "640x360", "--seconds", str(seconds),
                                           "--stream-horizontal", f"rtmp://127.0.0.1:{relay.port}/live/test"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=engine_env)
                time.sleep(3)
                stop_receiver(first)
                time.sleep(1)
                second = receiver(port, root/"received-second.mkv")
                _, log = studio.communicate(timeout=30)
                (root/"network-engine.log").write_bytes(log)
                assert studio.returncode == 0, log.decode()
                assert b"reconnects=0" in log and any(f"reconnects={n}".encode() in log for n in range(1, 20)), log.decode()
                try:
                    second.communicate(timeout=5)  # Drain the publisher's EOF before requesting exit.
                except subprocess.TimeoutExpired:
                    stop_receiver(second)
                inspect_pair(folder, 30, seconds*30)
                for path in (root/"received-first.mkv", root/"received-second.mkv"):
                    info = probe(path)
                    assert {s["codec_type"] for s in info["streams"]} >= {"video", "audio"}
                    video = next(s for s in info["streams"] if s["codec_type"] == "video")
                    assert int(video["nb_read_frames"]) >= 10
                print("PASS slow RTMP handshake, delivery, reconnect after receiver outage, and uninterrupted dual recordings")
            finally:
                terminate(studio)
                stop_receiver(first)
                stop_receiver(second)
                relay.close()


if __name__ == "__main__":
    main()
