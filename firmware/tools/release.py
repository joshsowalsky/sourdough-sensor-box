"""Build the firmware and publish it to the firmware branch, where the board
picks it up (it checks every 10 minutes, or right away after GET /check).

    python release.py                 build sourdough_box, publish if FW_VERSION went up
    python release.py --restart       only ask the board to restart
    python release.py --board 192.168.1.135   also tell the board to check now

The firmware branch holds one parentless commit with sourdough_box.bin and
manifest.json, force-pushed each time, so it never grows a history. Pushing
uses this PC's git access to the repo; the board reads it with its token.
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys
import urllib.request
from pathlib import Path

from make_uf2 import to_uf2

HERE = Path(__file__).resolve().parent
SKETCH = HERE.parent / "sourdough_box"
BUILD = Path(r"D:\Arduino15\build-out")
CLI = Path(r"C:\Users\techstyle\tools\arduino-cli\arduino-cli.exe")
FQBN = "esp32:esp32:adafruit_feather_esp32s3_nopsram"
CLONE = Path(r"C:\Users\techstyle\sourdough-sensor-firmware")
REMOTE = "git@github.com:joshsowalsky/sourdough-sensor-box.git"


def git(*args, check=True):
    return subprocess.run(["git", "-C", str(CLONE), *args], check=check, capture_output=True, text=True)


def build():
    subprocess.run([str(CLI), "compile", "-b", FQBN, "--output-dir", str(BUILD), str(SKETCH)], check=True)
    binary = (BUILD / "sourdough_box.ino.bin").read_bytes()
    (BUILD / "sourdough_box.uf2").write_bytes(to_uf2(binary))
    return binary


def current_manifest():
    r = git("show", "origin/firmware:manifest.json", check=False)
    return json.loads(r.stdout) if r.returncode == 0 else {"version": 0, "restart": 0}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--restart", action="store_true", help="only ask the board to restart")
    ap.add_argument("--board", help="board address, to have it check right away")
    a = ap.parse_args()

    if not (CLONE / ".git").exists():
        CLONE.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "init", "-q", str(CLONE)], check=True)
        git("remote", "add", "origin", REMOTE)
    git("fetch", "-q", "origin", "firmware", check=False)
    manifest = current_manifest()

    if a.restart:
        manifest["restart"] = manifest.get("restart", 0) + 1
        if not (CLONE / "sourdough_box.bin").exists():
            git("checkout", "-q", "-B", "firmware", "origin/firmware")
    else:
        version = int(re.search(r"FW_VERSION\s+(\d+)", (SKETCH / "version.h").read_text()).group(1))
        if version <= manifest.get("version", 0):
            sys.exit("FW_VERSION is %d but the branch already has %d; bump version.h first"
                     % (version, manifest["version"]))
        binary = build()
        (CLONE / "sourdough_box.bin").write_bytes(binary)
        manifest.update(version=version, size=len(binary), sha256=hashlib.sha256(binary).hexdigest())

    # Compact: build 1 reads "key":"value" with no spaces.
    (CLONE / "manifest.json").write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    git("checkout", "-q", "--orphan", "release-tmp")
    git("add", "-A")
    git("commit", "-q", "-m", "build %d, restart %d" % (manifest.get("version", 0), manifest["restart"]))
    git("branch", "-q", "-M", "firmware")
    r = git("push", "-q", "-f", "origin", "firmware", check=False)
    if r.returncode:
        sys.exit("push failed: " + r.stderr.strip())
    print("published:", json.dumps(manifest))

    if a.board:
        try:
            print(urllib.request.urlopen("http://%s/check" % a.board, timeout=5).read().decode().strip())
        except Exception as e:
            print("board didn't answer /check (%s); it will see it within 10 minutes" % e)


if __name__ == "__main__":
    main()
