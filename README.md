# Sourdough sensor box

A 3D-printable housing for a sourdough rise monitor: an ESP32-S3 Feather, a VL53L4CD distance sensor and a DS2484 1-Wire adapter with a DS18B20 probe. It sits on a beaker or starter jar and measures how far the dough has risen.

- **3D viewer (assembled):** open `index.html` through GitHub Pages. Drag to rotate, pinch to zoom, tap a layer name to hide it.
- **Print layout viewer:** `print-layout.html`
- **Print files:** `print-files/` has `base.stl`, `lid.stl`, `collar.stl` and `PRINT-NOTES.txt` (PETG, 0.2 mm layers, no supports, about 84-104 g).
- **Pictures:** `render_assembled.png`, `render_nolid.png`, `render_exploded.png`

Dimensions for the beakers and jars are assumed, not measured. Print the rim plate (`collar.stl`) first as a fit test.

## Electronics code

- **`feather/`** — CircuitPython 10 for the ESP32-S3 Feather. `code.py` reads the VL53L4CD (distance) and the DS18B20 probe through the DS2484, prints `distance: X cm | temp: Y C` over USB, and posts the same lines over Wi-Fi to the bridge about once a second. `wifisetup.py` joins a known Wi-Fi; if none is in range and the board isn't plugged into a computer, it opens an open network called **Sourdough-Setup** — join it from a phone, open http://192.168.4.1, pick a network and type its password (up to 5 are remembered). Needs `adafruit_vl53l4cd`, `adafruit_ds248x` and `adafruit_bus_device` in `lib/`. Copy `settings.example.toml` to `settings.toml` on the board and fill it in; the real one is git-ignored.
- **`bridge/bridge.py`** — runs on the PC (pyserial). Takes readings from USB serial or from Wi-Fi `POST /line`, serves the fast local view at `http://<PC address>:8765`, and once a minute force-pushes `latest.json` to the `data` branch, which `live.html` reads.

Probe wiring on the DS2484 terminals: red → V+, yellow → 1W, blue → GND.
