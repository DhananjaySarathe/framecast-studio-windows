"""Launch installed Windows binaries without Qt/vcpkg/developer directories on PATH."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from verify_media import inspect_pair

parser = argparse.ArgumentParser()
parser.add_argument("--installation", type=Path, required=True)
parser.add_argument("--evidence", type=Path, required=True)
args = parser.parse_args()
root = args.installation.resolve()
evidence = args.evidence.resolve()
evidence.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
for key in ("QT_PLUGIN_PATH", "QML2_IMPORT_PATH", "QT_QPA_PLATFORM", "FRAMECAST_STREAM_HORIZONTAL", "FRAMECAST_STREAM_VERTICAL"):
    env.pop(key, None)
system_root = Path(env["SYSTEMROOT"])
env["PATH"] = os.pathsep.join([str(root), str(system_root / "System32"), str(system_root)])

def launch(arguments, timeout=30):
    process = subprocess.run([str(a) for a in arguments], env=env, cwd=evidence, capture_output=True, timeout=timeout)
    if process.returncode:
        raise RuntimeError(f"Installed process exited with {process.returncode}: {process.stderr.decode(errors='replace')}")
    return process

shots = evidence / "studio"
launch([root / "FramecastStudio.exe", "--synthetic", "--screenshot", shots])
for name in ("studio.png", "layer.png", "comments.png", "preview.png", "starting.png", "recording.png"):
    image = shots / name
    assert image.is_file() and image.stat().st_size > 1000, f"Missing UI evidence: {name}"
launch([root / "FramecastStudio.exe", "--synthetic", "--size", "1280x640", "--screenshot", evidence / "laptop"])
launch([root / "FramecastStudio.exe", "--synthetic", "--empty", "--screenshot", evidence / "startup"])
recordings = evidence / "recordings"
launch([root / "framecast-engine.exe", "--synthetic", "--record", recordings, "--size", "640x360", "--fps", "29", "--seconds", "3"])
inspect_pair(recordings, 29, 87)
(evidence / "result.json").write_text(json.dumps({"installed_launch": "passed", "clean_runtime_path": True, "dual_recording": "passed", "ui_snapshots": "passed"}, indent=2))
print("PASS installed application launches and records with only its packaged runtime dependencies")
