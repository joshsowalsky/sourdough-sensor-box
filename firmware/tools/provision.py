"""Copy Wi-Fi and GitHub settings from a CircuitPython settings.toml into the
board over USB serial, without printing any of the values.

    python provision.py COM9 path/to/settings.toml

Uses CIRCUITPY_WIFI_SSID / CIRCUITPY_WIFI_PASSWORD, GITHUB_TOKEN, GITHUB_REPO
and SOURDOUGH_BRIDGE when they're there, then restarts the board.
"""
import sys
import time

import serial


def read_toml(path):
    values = {}
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, val = line.partition("=")
        val = val.strip()
        if len(val) >= 2 and val[0] == val[-1] == '"':
            val = val[1:-1].replace('\\"', '"').replace("\\\\", "\\")
        values[key.strip()] = val
    return values


def main(port, path):
    v = read_toml(path)
    cmds = []
    if v.get("CIRCUITPY_WIFI_SSID"):
        cmds.append(("wifi add {}\t{}".format(v["CIRCUITPY_WIFI_SSID"], v.get("CIRCUITPY_WIFI_PASSWORD", "")),
                     "Wi-Fi network"))
    for toml_key, key in (("GITHUB_TOKEN", "gh_token"), ("GITHUB_REPO", "gh_repo"), ("SOURDOUGH_BRIDGE", "bridge")):
        if v.get(toml_key):
            cmds.append(("set {} {}".format(key, v[toml_key]), key))
    s = serial.Serial(port, 115200, timeout=1)
    s.dtr = True
    time.sleep(1)
    s.reset_input_buffer()
    for cmd, label in cmds:
        s.write(cmd.encode() + b"\n")
        end, reply = time.time() + 5, ""
        while time.time() < end and "ok:" not in reply:
            reply = s.readline().decode("utf-8", "ignore")
        print(label + ":", "saved" if "ok:" in reply else "NO REPLY")
    s.write(b"restart\n")
    print("restarting the board")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
