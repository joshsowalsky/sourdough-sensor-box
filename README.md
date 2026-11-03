# Sourdough sensor box

A 3D-printable housing for a sourdough rise monitor: an ESP32-S3 Feather, a VL53L4CD distance sensor and a DS2484 1-Wire adapter with a DS18B20 probe. It sits on a beaker or starter jar and measures how far the dough has risen.

- **3D viewer (assembled):** open `index.html` through GitHub Pages. Drag to rotate, pinch to zoom, tap a layer name to hide it.
- **Print layout viewer:** `print-layout.html`
- **Print files:** `print-files/` has `base.stl`, `lid.stl`, `collar.stl` and `PRINT-NOTES.txt` (PETG, 0.2 mm layers, no supports, about 84-104 g).
- **Pictures:** `render_assembled.png`, `render_nolid.png`, `render_exploded.png`

Dimensions for the beakers and jars are assumed, not measured. Print the rim plate (`collar.stl`) first as a fit test.

## Parts bought

**Adafruit** ($58.80 in parts)

| Qty | Part | Adafruit ID | Price | Role |
| --- | --- | --- | --- | --- |
| 1 | ESP32-S3 Feather, 8 MB flash, no PSRAM | 5323 | $17.50 | the board |
| 1 | VL53L4CD time-of-flight sensor (~1–1300 mm) | 5396 | $14.95 | distance to the starter |
| 1 | DS2484 I2C-to-1-Wire adapter | 5976 | $7.50 | connects the probe |
| 1 | Waterproof DS18B20 temperature probe | 381 | $9.95 | starter temp (not answering, see below) |
| 2 | STEMMA QT cable, 200 mm | 4401 | $2.50 total | sensor chain |
| 1 | STEMMA QT cable, 100 mm | 4210 | $0.95 | sensor chain |
| 1 | STEMMA QT cable, 400 mm | 5385 | $1.50 | sensor chain |
| 1 | Food-grade heat shrink, 3/8" × 12" | 1020 | $3.95 | probe sealing |

**Amazon**

| Qty | Part | ASIN | Price | Role |
| --- | --- | --- | --- | --- |
| 1 | Zulkit IP65 ABS project box, 100 × 68 × 50 mm, black | B07RQMJXK6 | $6.49 | the enclosure |
| 1 | Karter Scientific 250 mL tall-form beaker | B006VYY09Q | $7.31 | vessel |
| 1 | Pyrex 400 mL tall-form (Berzelius) beaker | – | $9.99 | vessel |
| 2 | YILIHCIA 34 oz sourdough starter jar kit (printed 100–900 mL scale, ~86 mm ID) | B0GZDCMDM8 | $7.98 each | vessels |

Room temperature comes from a SwitchBot Meter Plus that was already on hand.

## The setup in use (Nov 2026)

The printed housing above isn't what's on the jar. In use instead:

- **Box:** a Zulkit IP65 ABS project box, 100 × 68 × 50 mm, set on the vessel's rim. The VL53L4CD is mounted inside at the top and aims down through the full 50 mm of the box, then out a hole in the bottom into the vessel.
- **Offset:** so every distance includes about 5 cm of box before the beam reaches the rim. Empty Karter beaker: 17.1 cm, about 11.8 cm of beaker plus about 5.3 cm of box. The offset is constant, so rise is read as the *change* from the fed level and the offset cancels out.
- **Hole:** the sensor's cone (18° field of view) is about 1.6 cm wide where it leaves the box. Keep the hole at least 2 cm across and centred over the vessel. Starter on the hole or the sensor window blocks the reading.
- **Vessels**, as depth of starter per 10 g fed (0.854 mL per g of starter; inside diameters from catalog specs, not measured):

  | Vessel | Size (catalog) | cm per 10 g |
  | --- | --- | --- |
  | Karter 250 mL tall-form beaker | 60 mm OD × 120 mm (ISO 3819 tall form), ~57 mm ID | 0.33 |
  | Pyrex 400 mL tall-form beaker | 70 mm OD × 130 mm, ~67 mm ID | 0.24 |
  | YILIHCIA 34 oz starter jar | ~86 mm ID (from earlier camera-session notes) | 0.15 |

  Check, 2026-11-01: 50 g of Gus in the Karter read 15.6 cm against 17.1 cm empty, 1.5 cm deep against 1.7 cm predicted. So in that beaker 2x ≈ 14.0 cm, 3x ≈ 12.4 cm.
- **Logging:** `tof_logger.py` in the claude repo (`sourdough/`, scheduled task `\sourdough\tof`) saves every reading plus room temperature to `~/sourdough-tracker/tof_log.csv`. It also publishes the current run (the last row of `tof_runs.csv` and every reading since that feed) to `run.json` on the `runs` branch, which `live.html` shows as **This run**.

## Electronics code

- **`firmware/sourdough_box/`** — what the Feather runs now: an Arduino (C++) sketch for the ESP32-S3 Feather (no PSRAM), built with arduino-cli and the esp32 core 3.3 (`esp32:esp32:adafruit_feather_esp32s3_nopsram`, libraries Adafruit DS248x and STM32duino VL53L4CD). It sends the same `distance: X cm | temp: Y C | status: S | signal: N` lines to the bridge, publishes `latest.json` to the `data` branch once a minute, and serves its own live page at `http://<board address>/` (`/status` is JSON). If no saved Wi-Fi is in range it opens **Sourdough-Setup** (http://192.168.4.1) to add one, and optionally a new GitHub token. A 60-second hardware watchdog restarts it if it ever hangs, and the next start logs what it was doing.
  - **Updates and restarts over Wi-Fi:** bump `FW_VERSION` in `version.h`, then run `python firmware/tools/release.py --board <board address>`. It builds the sketch and force-pushes `sourdough_box.bin` and `manifest.json` to the `firmware` branch; the board checks that branch every 10 minutes (or at once on `GET /check`), verifies the SHA-256 and installs it. `release.py --restart` only asks the board to restart. A build that crashes 3 starts in a row before ever publishing goes back to the previous one.
  - **Settings** (Wi-Fi networks, `gh_token`, `gh_repo`, `bridge`) live in the board's flash, set from the setup page or over USB serial (`wifi add <name><TAB><password>`, `set gh_token ...`, `status`, `restart`); `firmware/tools/provision.py COM<n> settings.toml` copies them from a CircuitPython `settings.toml` without printing them.
  - **First flash over USB:** `arduino-cli upload -b esp32:esp32:adafruit_feather_esp32s3_nopsram -p COM<n> firmware/sourdough_box` (a 1200-baud touch on the port puts the board into its flashing mode). `tools/make_uf2.py` makes a `.uf2` for the double-press-reset drive instead.
- **`feather/`** — the earlier CircuitPython 10 version, kept for reference. It hung inside CircuitPython itself about every 30 minutes (and in HTTPS uploads when memory ran low), which is why the board moved to Arduino. `code.py` reads the VL53L4CD (distance) and the DS18B20 probe through the DS2484, prints `distance: X cm | temp: Y C` over USB, and posts the same lines over Wi-Fi to the bridge about once a second. `wifisetup.py` joins a known Wi-Fi; if none is in range and the board isn't plugged into a computer, it opens an open network called **Sourdough-Setup** — join it from a phone, open http://192.168.4.1, pick a network and type its password (up to 5 are remembered). `ghpublish.py` writes `latest.json` to the `data` branch once a minute straight from the board (a parentless commit force-moved onto the branch, so it never grows a history), so `live.html` works on any Wi-Fi; it needs `GITHUB_TOKEN` in `settings.toml`. Needs `adafruit_vl53l4cd`, `adafruit_ds248x`, `adafruit_bus_device`, `adafruit_requests` and `adafruit_connection_manager` in `lib/`. Copy `settings.example.toml` to `settings.toml` on the board and fill it in; the real one is git-ignored.
- **`bridge/bridge.py`** — runs on the PC (pyserial). Takes readings from USB serial or from Wi-Fi `POST /line` (and saves the board's messages to `feather-serial.log`, plus its last 60 USB lines whenever it goes quiet), serves the fast local view at `http://<PC address>:8765`, and once a minute force-pushes `latest.json` to the `data` branch, which `live.html` reads — but only while the Feather hasn't reported publishing it itself in the last 3 minutes, so the PC is a backup.

Probe wiring on the DS2484 terminals: red → V+, yellow → 1W, blue → GND.
