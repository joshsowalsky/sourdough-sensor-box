import os
import time
import board
import digitalio
import supervisor

# If this program ever stops on an error, CircuitPython runs it again
# instead of sitting halted until someone unplugs the board.
supervisor.set_next_code_file(None, reload_on_error=True)

try:
    _pwr = digitalio.DigitalInOut(board.I2C_POWER)
    _pwr.switch_to_output(value=True)
    time.sleep(0.5)
except Exception as e:
    print("no I2C_POWER pin:", e)

i2c = board.STEMMA_I2C()
while not i2c.try_lock():
    pass
print("I2C devices found:", [hex(a) for a in i2c.scan()])
i2c.unlock()

tof = None
try:
    import adafruit_vl53l4cd
    tof = adafruit_vl53l4cd.VL53L4CD(i2c)
    tof.inter_measurement = 0
    tof.timing_budget = 200
    tof.start_ranging()
    print("Distance sensor: OK")
except Exception as e:
    print("Distance sensor not ready:", e)

from adafruit_ds248x import Adafruit_DS248x
ds = None
rom = None
last_try = -10
last_temp_read = -10
last_temp = None

# Wi-Fi: join a known network (settings.toml, or ones added in setup mode).
# If none is in reach and the board isn't plugged into a computer, start the
# "Sourdough-Setup" network so a phone can add one (see wifisetup.py).
# Readings go to the bridge on the PC at SOURDOUGH_BRIDGE ("ip:port").
pool = None
bridge = None
gh = None
try:
    import adafruit_connection_manager
    import microcontroller
    import supervisor
    import wifi
    import wifisetup
    pool = adafruit_connection_manager.get_radio_socketpool(wifi.radio)
    host, _, port = (os.getenv("SOURDOUGH_BRIDGE") or "").partition(":")
    if host:
        bridge = (host, int(port or 8765))
    if wifisetup.connect():
        print("wifi:", wifi.radio.ap_info.ssid if wifi.radio.ap_info else "?", wifi.radio.ipv4_address)
    elif not supervisor.runtime.usb_connected:
        wifisetup.setup_mode(pool)
    else:
        print("wifi: no known network; plugged into a computer, so staying on USB")
except Exception as e:
    print("wifi not available:", e)

# GitHub: once a minute, the minute's median distance and the latest temperature
# go straight to latest.json on GitHub (ghpublish.py), so the live page works
# from anywhere. Needs GITHUB_TOKEN in settings.toml.
if pool is not None:
    try:
        import ghpublish
        gh = ghpublish.Publisher(pool, adafruit_connection_manager.get_radio_ssl_context(wifi.radio))
        print("github:", "on" if gh.enabled else "off (no GITHUB_TOKEN in settings.toml)")
    except Exception as e:
        print("github: not available:", e)
minute_d = []
minute_start = time.monotonic()
gh_failures = 0

# Watchdog: restarts the board if the loop below stops coming round for 5
# minutes (say, a network call that hangs). Not used while on USB, so a
# paused or debugged program doesn't get reset from under you.
dog = None
if not supervisor.runtime.usb_connected:
    try:
        from microcontroller import watchdog as dog
        from watchdog import WatchDogMode
        dog.timeout = 300
        dog.mode = WatchDogMode.RESET
        dog.feed()
    except Exception as e:
        dog = None
        print("watchdog not available:", e)
outbox = []
last_send = 0
send_backoff = 1
last_wifi_note = -60
offline_since = None


def connect_probe():
    """Find the 1-Wire adapter and the probe. Safe to call again and again."""
    global ds, rom
    try:
        ds = Adafruit_DS248x(i2c)
    except Exception:
        ds = None
        rom = None
        return "1-Wire adapter: no probe connected yet (wires not in the terminals?)"
    found = bytearray(8)
    if ds.onewire_search(found):
        rom = found
        return "Probe found, ROM: " + " ".join("{:02X}".format(b) for b in rom)
    rom = None
    return "1-Wire adapter OK, but no probe found"


def send_lines(lines):
    """POST the batched lines to the bridge. Returns True if it got through."""
    body = "\n".join(lines).encode()
    head = ("POST /line HTTP/1.0\r\nHost: {}\r\nContent-Type: text/plain\r\n"
            "Content-Length: {}\r\n\r\n").format(bridge[0], len(body)).encode()
    s = pool.socket(pool.AF_INET, pool.SOCK_STREAM)
    s.settimeout(2)
    try:
        s.connect(bridge)
        data = head + body
        while data:
            n = s.send(data)
            data = data[n:]
        buf = bytearray(32)
        n = s.recv_into(buf)
        return bytes(buf[:n]).startswith(b"HTTP/1.0 2") or bytes(buf[:n]).startswith(b"HTTP/1.1 2")
    finally:
        s.close()


while True:
    now = time.monotonic()
    if rom is None and now - last_try > 5:
        last_try = now
        print(connect_probe())
    line = []
    if tof is not None:
        try:
            if tof.data_ready:
                tof.clear_interrupt()
                dist = tof.distance
                line.append("distance: {} cm".format(dist))
                if len(minute_d) < 600:
                    minute_d.append(dist)
        except Exception as e:
            line.append("distance error: {}".format(e))
    if ds is not None and rom is not None and now - last_temp_read >= 5:
        last_temp_read = now
        try:
            last_temp = ds.ds18b20_temperature(rom)
        except Exception as e:
            print("temp error:", e)
            rom = None
    if last_temp is not None and rom is not None:
        line.append("temp: {:.2f} C".format(last_temp))
    if line:
        text = " | ".join(line)
        print(text)
        if bridge:
            outbox.append(text)
            del outbox[:-40]
    if bridge and outbox and now - last_send >= send_backoff:
        last_send = now
        if wifi.radio.connected:
            try:
                if send_lines(outbox):
                    outbox = []
                    send_backoff = 1
            except Exception as e:
                print("send to bridge failed:", e)
                send_backoff = 30  # bridge not reachable (PC off, or another network)
        if now - last_wifi_note >= 60:
            last_wifi_note = now
            print("wifi:", wifi.radio.ipv4_address if wifi.radio.connected else "not connected",
                  "-> bridge", bridge)
    if now - minute_start >= 60:
        minute_start = now
        if gh is not None and gh.enabled and minute_d and wifi.radio.connected:
            minute_d.sort()
            temp = last_temp if rom is not None else None
            try:
                if gh.add(minute_d[len(minute_d) // 2], temp) and gh.publish():
                    gh_failures = 0
                    print("github: ok")
                    if bridge:
                        outbox.append("github: ok")  # tells the bridge it needn't publish
            except Exception as e:
                gh_failures += 1
                print("github: publish failed:", e)
                if bridge:
                    outbox.append("github: failed {}".format(e)[:150])
                # A fresh start clears fragmented memory; history reloads from GitHub.
                if gh_failures >= 5 and not supervisor.runtime.usb_connected:
                    print("github: 5 failures in a row, restarting")
                    microcontroller.reset()
        minute_d = []
    # Off Wi-Fi for 5 minutes on a charger: restart, which rejoins or opens setup.
    if pool is not None and not wifi.radio.connected and not supervisor.runtime.usb_connected:
        if offline_since is None:
            offline_since = now
        elif now - offline_since > 300:
            print("wifi: offline 5 minutes, restarting")
            microcontroller.reset()
    else:
        offline_since = None
    if dog is not None:
        dog.feed()
    time.sleep(0.15)
