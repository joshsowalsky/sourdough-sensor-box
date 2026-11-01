"""Read the Feather's readings and publish a one-minute summary to GitHub.

The Feather prints lines like:  distance: 6.6 cm | temp: 24.31 C
They arrive over USB serial, or over Wi-Fi as a POST to /line on PORT.
Every INTERVAL seconds this writes latest.json (current values plus recent history)
and force-pushes it as a single commit to the `data` branch of the repo, so the
live page can read it. Only sensor numbers are published.
"""
import json
import re
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import json as _json
import queue
import socket
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import serial
from serial.tools import list_ports

PORT = 8765            # fast local page: http://<this PC's address>:8765
NOW = {"distance_cm": None, "temp_c": None, "t": None}
WIFI_SEEN = {"t": None}  # when the Feather last posted over Wi-Fi
FEATHER_GH = {"t": 0}  # when the Feather last said it published to GitHub itself

LOCAL_PAGE = """<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Sourdough Fast View</title>
<style>:root{--bg:#f4f3ee;--fg:#1f2421;--m:#66706a;--a:#b4691f}
@media (prefers-color-scheme: dark){:root{--bg:#161a18;--fg:#ebeee9;--m:#9ba59f;--a:#e2994d}}
body{margin:0;background:var(--bg);color:var(--fg);font:16px system-ui,sans-serif;padding:16px;text-align:center}
.big{font-size:5.5rem;font-weight:700;font-variant-numeric:tabular-nums;color:var(--a);line-height:1.1}
.u{font-size:1.4rem;color:var(--m)}.l{color:var(--m);text-transform:uppercase;font-size:.8rem;letter-spacing:.06em;margin-top:18px}
.t{font-size:2.2rem;font-weight:600}.s{color:var(--m);font-size:.85rem;margin-top:22px}</style></head><body>
<div class="l">Distance to surface</div><div><span class="big" id="d">-</span><span class="u"> cm</span></div>
<div class="l">Temperature</div><div class="t"><span id="c">-</span> <span class="u">&deg;C</span></div>
<div class="s" id="s">Connecting...</div>
<script>
async function tick(){try{const r=await fetch('/now',{cache:'no-store'});const j=await r.json();
document.getElementById('d').textContent=j.distance_cm==null?'-':j.distance_cm.toFixed(1);
document.getElementById('c').textContent=j.temp_c==null?'no probe':j.temp_c.toFixed(1);
document.getElementById('s').textContent='Live, about 4 updates a second';}catch(e){document.getElementById('s').textContent='Lost connection to the PC'}}
setInterval(tick,250);tick();
</script></body></html>"""


LINES = queue.Queue()  # sensor lines from USB serial and from the Feather over Wi-Fi


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        if not self.path.startswith("/line"):
            self.send_error(404)
            return
        n = int(self.headers.get("Content-Length") or 0)
        text = self.rfile.read(min(n, 16384)).decode("utf-8", "ignore")
        for line in text.splitlines():
            LINES.put(line)
        if not WIFI_SEEN.get("t"):
            log("Feather is sending over Wi-Fi from " + self.client_address[0])
        WIFI_SEEN["t"] = time.time()
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        if self.path.startswith("/now"):
            body = _json.dumps(NOW).encode()
            ctype = "application/json"
        else:
            body = LOCAL_PAGE.encode()
            ctype = "text/html; charset=utf-8"
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"
    finally:
        s.close()

REPO_DIR = Path(r"C:\Users\techstyle\sourdough-sensor-data")  # clone checked out on the data branch
INTERVAL = 60          # seconds between published points
MAX_POINTS = 720       # 12 hours at one point per minute
VID = 0x239A           # Adafruit

DIST = re.compile(r"distance:\s*([0-9.]+)\s*cm")
TEMP = re.compile(r"temp:\s*(-?[0-9.]+)\s*C")


def log(msg):
    print(datetime.now().strftime("%H:%M:%S"), msg, flush=True)


def find_port():
    for p in list_ports.comports():
        if p.vid == VID:
            return p.device
    return None


def read_serial():
    """Feed lines from the Feather's USB serial into LINES, when it's plugged in.
    While the Feather is also posting over Wi-Fi, USB lines are dropped so
    readings aren't counted twice."""
    ser, waiting_logged = None, False
    while True:
        if ser is None:
            port = find_port()
            if not port:
                if not waiting_logged:
                    log("Feather not on USB (fine if it's on Wi-Fi)")
                    waiting_logged = True
                time.sleep(5)
                continue
            try:
                ser = serial.Serial(port, 115200, timeout=2)
                ser.dtr = True
                ser.rts = True
                log("reading " + port)
                waiting_logged = False
            except Exception as e:
                log("cannot open port: %s" % e)
                ser = None
                time.sleep(5)
                continue
        try:
            line = ser.readline().decode("utf-8", "ignore")
        except Exception as e:
            log("serial lost: %s" % e)
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(3)
            continue
        if line.startswith("wifi:") or "bridge failed" in line:
            log("feather " + line.strip())
        if WIFI_SEEN["t"] and time.time() - WIFI_SEEN["t"] < 10:
            continue
        LINES.put(line)


def git(*args):
    return subprocess.run(["git", "-C", str(REPO_DIR), *args], capture_output=True, text=True)


def publish(history):
    latest = history[-1]
    doc = {"updated": latest["t"], "distance_cm": latest["d"], "temp_c": latest["c"], "history": history}
    (REPO_DIR / "latest.json").write_text(json.dumps(doc, separators=(",", ":")))
    git("add", "latest.json")
    git("commit", "--amend", "-m", "sensor data", "--allow-empty")
    r = git("push", "-f", "origin", "data")
    if r.returncode != 0:
        log("push failed: " + r.stderr.strip()[-200:])
    else:
        log("published d=%s c=%s (%d points)" % (latest["d"], latest["c"], len(history)))


def main():
    file = REPO_DIR / "latest.json"
    history = []
    if file.exists():
        try:
            history = json.loads(file.read_text()).get("history", [])
        except Exception:
            history = []
    dists, last_temp, last_pub = [], None, time.time() - INTERVAL + 10
    try:
        srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        log("fast page: http://%s:%d" % (lan_ip(), PORT))
    except Exception as e:
        log("fast page not started: %s" % e)
    threading.Thread(target=read_serial, daemon=True).start()
    while True:
        try:
            line = LINES.get(timeout=1)
        except queue.Empty:
            line = ""
        if line.startswith("github:"):
            if line.startswith("github: ok"):
                if time.time() - FEATHER_GH["t"] > 180:
                    log("Feather is publishing to GitHub itself; the PC will stand by")
                FEATHER_GH["t"] = time.time()
            else:
                log("feather " + line.strip())
            continue
        m = DIST.search(line)
        if m:
            try:
                dists.append(float(m.group(1)))
                NOW["distance_cm"] = dists[-1]
                NOW["t"] = time.time()
            except ValueError:
                pass
        m = TEMP.search(line)
        if m:
            last_temp = float(m.group(1))
            NOW["temp_c"] = last_temp
        if time.time() - last_pub >= INTERVAL and dists:
            point = {
                "t": datetime.now(timezone.utc).isoformat(timespec="seconds"),
                "d": round(statistics.median(dists), 2),
                "c": round(last_temp, 2) if last_temp is not None else None,
            }
            history.append(point)
            history = history[-MAX_POINTS:]
            # The Feather writes the same file when it can reach GitHub; only
            # publish when it hasn't reported doing so in the last 3 minutes.
            if time.time() - FEATHER_GH["t"] > 180:
                publish(history)
            dists, last_pub = [], time.time()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
