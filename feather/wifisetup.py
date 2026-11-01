"""Wi-Fi for the sourdough Feather: join a known network, or run setup mode.

Known networks are the one in settings.toml (CircuitPython joins it at boot on
its own) plus up to 5 added through setup mode. Those are kept in the board's
non-volatile memory (microcontroller.nvm), not in a file, so saving them works
without the CIRCUITPY drive being writable from code.

Setup mode: when no known network can be reached, the Feather starts its own
open network "Sourdough-Setup". Join it from a phone and open http://192.168.4.1
to choose a Wi-Fi and type its password; it's saved and the Feather restarts.
After SETUP_MINUTES with nothing entered it restarts and tries again, in case
the usual Wi-Fi was only down for a bit.
"""
import json
import os
import time

import microcontroller
import wifi

AP_NAME = "Sourdough-Setup"
SETUP_MINUTES = 10
MAGIC = b"SDW1"
MAX_SAVED = 5


def saved_networks():
    if bytes(microcontroller.nvm[0:4]) != MAGIC:
        return []
    n = int.from_bytes(bytes(microcontroller.nvm[4:6]), "big")
    try:
        return json.loads(bytes(microcontroller.nvm[6:6 + n]).decode())
    except Exception:
        return []


def save_network(ssid, password):
    nets = [x for x in saved_networks() if x[0] != ssid]
    nets.insert(0, [ssid, password])
    data = json.dumps(nets[:MAX_SAVED]).encode()
    microcontroller.nvm[6:6 + len(data)] = data
    microcontroller.nvm[4:6] = len(data).to_bytes(2, "big")
    microcontroller.nvm[0:4] = MAGIC


def connect():
    """Join a known network. Returns True once connected."""
    home = os.getenv("CIRCUITPY_WIFI_SSID") or ""
    if home.startswith("PUT-"):
        home = ""
    if home:
        # CircuitPython is already joining the settings.toml network; give it a moment.
        start = time.monotonic()
        while not wifi.radio.connected and time.monotonic() - start < 20:
            time.sleep(0.5)
    if wifi.radio.connected:
        return True
    nets = saved_networks()
    if home:
        nets.append([home, os.getenv("CIRCUITPY_WIFI_PASSWORD") or ""])
    for ssid, password in nets:
        try:
            print("wifi: trying", ssid)
            wifi.radio.connect(ssid, password, timeout=10)
            if wifi.radio.connected:
                return True
        except Exception as e:
            print("wifi:", ssid, "failed:", e)
    return False


def _unquote(s):
    s = s.replace("+", " ")
    out = bytearray()
    i = 0
    while i < len(s):
        if s[i] == "%" and i + 3 <= len(s):
            try:
                out.append(int(s[i + 1:i + 3], 16))
                i += 3
                continue
            except ValueError:
                pass
        out.extend(s[i].encode())
        i += 1
    return out.decode()


def _form(body):
    fields = {}
    for pair in body.split("&"):
        key, _, value = pair.partition("=")
        fields[_unquote(key)] = _unquote(value)
    return fields


def _esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


PAGE_HEAD = """<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Sourdough Wi-Fi setup</title>
<style>body{font:17px system-ui,sans-serif;margin:0;padding:20px;max-width:420px}
label{display:block;margin:16px 0 6px;font-weight:600}select,input,button{font:inherit;width:100%;
padding:10px;box-sizing:border-box}button{margin-top:22px;background:#b4691f;color:#fff;border:0;border-radius:6px}
p{color:#555}</style></head><body><h2>Sourdough sensor Wi-Fi</h2>"""


def _setup_page(found):
    options = "".join('<option>{}</option>'.format(_esc(s)) for s in found)
    return PAGE_HEAD + ("""<form method="post">
<label>Wi-Fi network</label><select name="ssid"><option value="">(pick one)</option>{}</select>
<label>...or type its name</label><input name="other" autocapitalize="off" autocorrect="off">
<label>Password</label><input name="password" type="password">
<button type="submit">Save and connect</button></form>
<p>The sensor keeps up to {} networks and joins whichever one it finds.</p></body></html>""").format(
        options, MAX_SAVED)


def _send_all(conn, data):
    while data:
        n = conn.send(data)
        data = data[n:]


def _reply(conn, html):
    body = html.encode()
    _send_all(conn, ("HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                     "Content-Length: {}\r\nConnection: close\r\n\r\n").format(len(body)).encode() + body)


def _listen(pool, ip):
    for port in (80, 8080):
        srv = pool.socket(pool.AF_INET, pool.SOCK_STREAM)
        try:
            srv.setsockopt(pool.SOL_SOCKET, pool.SO_REUSEADDR, 1)
            srv.bind((ip, port))
            srv.listen(2)
            srv.settimeout(1)
            return srv, port
        except OSError as e:
            print("setup: port", port, "busy:", e)
            srv.close()
    raise RuntimeError("no free port for the setup page")


def setup_mode(pool):
    """Run the setup network and page. Never returns: ends in a restart."""
    found = []
    try:
        for net in wifi.radio.start_scanning_networks():
            if net.ssid and net.ssid not in found:
                found.append(net.ssid)
        wifi.radio.stop_scanning_networks()
    except Exception as e:
        print("setup: scan failed:", e)
    wifi.radio.start_ap(AP_NAME)
    ip = str(wifi.radio.ipv4_address_ap)
    srv, port = _listen(pool, ip)
    print("setup mode: join", AP_NAME, "and open http://{}{}".format(ip, "" if port == 80 else ":%d" % port))
    deadline = time.monotonic() + SETUP_MINUTES * 60
    buf = bytearray(1024)
    while time.monotonic() < deadline:
        try:
            conn, _ = srv.accept()
        except OSError:
            continue
        try:
            conn.settimeout(5)
            req = b""
            while b"\r\n\r\n" not in req and len(req) < 8192:
                n = conn.recv_into(buf)
                if not n:
                    break
                req += bytes(buf[:n])
            head, _, body = req.partition(b"\r\n\r\n")
            length = 0
            for line in head.split(b"\r\n"):
                if line.lower().startswith(b"content-length:"):
                    length = int(line[15:].strip())
            while len(body) < length:
                n = conn.recv_into(buf)
                if not n:
                    break
                body += bytes(buf[:n])
            if head.startswith(b"POST"):
                form = _form(body.decode())
                ssid = (form.get("other") or "").strip() or form.get("ssid", "")
                if ssid:
                    save_network(ssid, form.get("password", ""))
                    _reply(conn, PAGE_HEAD + "<p>Saved <b>{}</b>. The sensor is restarting to join it. "
                           "You can switch your phone back to your normal Wi-Fi.</p></body></html>".format(_esc(ssid)))
                    conn.close()
                    print("setup: saved", ssid, "- restarting")
                    time.sleep(2)
                    microcontroller.reset()
            _reply(conn, _setup_page(found))
        except Exception as e:
            print("setup: request failed:", e)
        finally:
            try:
                conn.close()
            except Exception:
                pass
    print("setup: nothing entered, restarting to try known networks again")
    microcontroller.reset()
