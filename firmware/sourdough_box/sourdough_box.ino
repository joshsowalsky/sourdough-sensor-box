// Sourdough sensor box: Adafruit Feather ESP32-S3 (no PSRAM) with a VL53L4CD
// distance sensor and a DS18B20 probe on a DS2484, both on STEMMA QT.
//
// Every reading goes to the PC bridge (SOURDOUGH bridge, POST /line) when it
// can be reached; once a minute the minute's median distance and the latest
// temperature go to latest.json on the data branch on GitHub (github.ino),
// which the live page reads. New builds and restart requests come from the
// firmware branch on GitHub, so the board never needs the USB cable.
//
// Settings live in flash (Preferences, namespace "sd"). Set them from the
// setup page (wifi_setup.ino) or over USB serial, one command per line:
//   wifi add <name><TAB><password>   wifi list   wifi clear
//   set gh_token <token>             set gh_repo <owner/repo>
//   set bridge <ip:port>             status      restart
// Values are never printed back.

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include <Adafruit_DS248x.h>
#include <vl53l4cd_class.h>
#include "version.h"

#define WATCHDOG_MS 60000  // restart if loop() stops coming round for a minute
#define NO_TEMP -999999

Preferences prefs;
WebServer web(80);

// Settings.
String ghToken, ghRepo, bridgeHost;
uint16_t bridgePort = 8765;

// What the program was doing, kept through a watchdog restart (RTC memory
// isn't cleared by a reset), so the next start can say where it got stuck.
RTC_NOINIT_ATTR char lastStage[32];
RTC_NOINIT_ATTR uint32_t lastStageMagic;
String previousStage;

void stage(const char *what) {
  strncpy(lastStage, what, sizeof(lastStage) - 1);
  lastStage[sizeof(lastStage) - 1] = 0;
  lastStageMagic = 0x5D5D5D5D;
  esp_task_wdt_reset();
}

// ---------- messages ----------
// Startup, errors and GitHub steps go to USB serial and, batched, to the PC
// bridge, which saves them in feather-serial.log.

std::vector<String> notes, outbox;

void note(const char *fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.println(buf);
  notes.push_back(buf);
  if (notes.size() > 100) notes.erase(notes.begin());
}

const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "watchdog (loop stuck)";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_BROWNOUT: return "brownout (power dipped)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_USB: return "USB reset";
    default: return "other";
  }
}

// ---------- sensors ----------

VL53L4CD tof(&Wire, -1);
bool tofOk = false;
uint32_t tofLastTry = 0;
float distanceCm = -1;
uint8_t distanceStatus = 255;
uint16_t distanceSignal = 0;
std::vector<float> minuteValid, minuteAll;

Adafruit_DS248x ds;
bool dsOk = false;
uint8_t rom[8];
bool romOk = false;
uint32_t dsLastTry = 0, tempLastStart = 0;
bool converting = false;
float tempC = NAN;
int tempFailures = 0;
String lastProbeMsg;

void startTof() {
  stage("tof init");
  if (tof.InitSensor() == 0 && tof.VL53L4CD_SetRangeTiming(200, 0) == 0 &&
      tof.VL53L4CD_StartRanging() == 0) {
    tofOk = true;
    note("distance sensor: ok");
  } else {
    note("distance sensor: not answering");
  }
}

void readTof() {
  uint8_t ready = 0;
  stage("tof read");
  if (tof.VL53L4CD_CheckForDataReady(&ready) != 0) {
    tofOk = false;
    note("distance sensor: lost");
    return;
  }
  if (!ready) return;
  VL53L4CD_Result_t r;
  tof.VL53L4CD_ClearInterrupt();
  tof.VL53L4CD_GetResult(&r);
  distanceCm = r.distance_mm / 10.0;
  distanceStatus = r.range_status;
  distanceSignal = r.signal_per_spad_kcps;
  if (minuteAll.size() < 600) minuteAll.push_back(distanceCm);
  if (r.range_status == 0 && minuteValid.size() < 600) minuteValid.push_back(distanceCm);
  char line[120];
  if (!isnan(tempC))
    snprintf(line, sizeof(line), "distance: %.1f cm | temp: %.2f C | status: %u | signal: %u",
             distanceCm, tempC, r.range_status, r.signal_per_spad_kcps);
  else
    snprintf(line, sizeof(line), "distance: %.1f cm | status: %u | signal: %u",
             distanceCm, r.range_status, r.signal_per_spad_kcps);
  Serial.println(line);
  outbox.push_back(line);
  if (outbox.size() > 40) outbox.erase(outbox.begin());
}

uint8_t crc8(const uint8_t *p, int n) {  // Dallas/Maxim 1-Wire CRC
  uint8_t crc = 0;
  while (n--) {
    uint8_t b = *p++;
    for (int i = 0; i < 8; i++) {
      uint8_t mix = (crc ^ b) & 1;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      b >>= 1;
    }
  }
  return crc;
}

void probeNote(const String &msg) {
  if (msg != lastProbeMsg) note("%s", msg.c_str());
  lastProbeMsg = msg;
}

void matchRom() {
  ds.OneWireWriteByte(0x55);
  for (int i = 0; i < 8; i++) ds.OneWireWriteByte(rom[i]);
}

// The probe works in two halves so loop() never waits the 750 ms a
// conversion takes: start it, then read it on a later round.
void serviceProbe() {
  uint32_t now = millis();
  if (!dsOk) {
    if (now - dsLastTry < 30000 && dsLastTry) return;
    dsLastTry = now;
    stage("1-wire adapter init");
    dsOk = ds.begin(&Wire);
    if (!dsOk) probeNote("1-Wire adapter: not answering");
    return;
  }
  if (!romOk) {
    if (now - dsLastTry < 30000) return;
    dsLastTry = now;
    stage("probe search");
    if (!ds.OneWireReset()) {
      probeNote(ds.shortDetected() ? "probe: wires shorted" : "probe: not answering (check the screw terminals)");
      return;
    }
    ds.OneWireSearchReset();
    if (ds.OneWireSearch(rom) && rom[0] == 0x28 && crc8(rom, 7) == rom[7]) {
      romOk = true;
      tempFailures = 0;
      char id[40];
      snprintf(id, sizeof(id), "probe: found %02X%02X%02X%02X%02X%02X%02X%02X",
               rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
      probeNote(id);
    } else {
      probeNote("probe: bus answered but no DS18B20 found");
    }
    return;
  }
  if (!converting && now - tempLastStart >= 5000) {
    stage("probe convert");
    tempLastStart = now;
    if (ds.OneWireReset()) {
      matchRom();
      ds.OneWireWriteByte(0x44);
      converting = true;
    } else {
      tempFailure();
    }
  } else if (converting && now - tempLastStart >= 800) {
    stage("probe read");
    converting = false;
    uint8_t s[9];
    bool ok = ds.OneWireReset();
    if (ok) {
      matchRom();
      ds.OneWireWriteByte(0xBE);
      for (int i = 0; i < 9 && ok; i++) ok = ds.OneWireReadByte(&s[i]);
    }
    if (ok && crc8(s, 8) == s[8]) {
      tempC = (int16_t)((s[1] << 8) | s[0]) / 16.0;
      tempFailures = 0;
    } else {
      tempFailure();
    }
  }
}

void tempFailure() {
  if (++tempFailures >= 3) {
    romOk = false;
    tempC = NAN;
    dsLastTry = millis();
    probeNote("probe: lost (3 bad reads in a row)");
  }
}

// ---------- PC bridge ----------

uint32_t lastSend = 0, sendBackoff = 1000;

bool sendLines() {
  String body;
  for (auto &l : notes) body += l + "\n";
  for (auto &l : outbox) body += l + "\n";
  stage("bridge send");
  WiFiClient c;
  if (!c.connect(bridgeHost.c_str(), bridgePort, 2000)) return false;
  c.setTimeout(2000);  // milliseconds, for the reply below
  c.printf("POST /line HTTP/1.0\r\nHost: %s\r\nContent-Type: text/plain\r\nContent-Length: %u\r\n\r\n",
           bridgeHost.c_str(), body.length());
  c.print(body);
  char status[16] = {0};
  size_t n = c.readBytes(status, 12);
  c.stop();
  return n >= 10 && strncmp(status + 9, "2", 1) == 0;
}

void serviceBridge() {
  if (bridgeHost.isEmpty() || (outbox.empty() && notes.empty())) return;
  if (millis() - lastSend < sendBackoff || WiFi.status() != WL_CONNECTED) return;
  lastSend = millis();
  if (sendLines()) {
    outbox.clear();
    notes.clear();
    sendBackoff = 1000;
  } else {
    sendBackoff = 30000;  // bridge not reachable (PC off, or another network)
  }
}

// ---------- serial commands ----------

String serialLine;

void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.startsWith("wifi add ")) {
    String rest = cmd.substring(9);
    int tab = rest.indexOf('\t');
    if (tab < 1) { Serial.println("usage: wifi add <name><TAB><password>"); return; }
    saveNetwork(rest.substring(0, tab), rest.substring(tab + 1));
    Serial.println("ok: network saved");
  } else if (cmd == "wifi list") {
    for (int i = 0; i < savedCount(); i++) Serial.printf("  %s\n", savedSsid(i).c_str());
  } else if (cmd == "wifi clear") {
    clearNetworks();
    Serial.println("ok: networks cleared");
  } else if (cmd.startsWith("set ")) {
    int sp = cmd.indexOf(' ', 4);
    String key = cmd.substring(4, sp < 0 ? cmd.length() : sp), val = sp < 0 ? "" : cmd.substring(sp + 1);
    if (key == "gh_token" || key == "gh_repo" || key == "bridge") {
      prefs.putString(key.c_str(), val);
      Serial.printf("ok: %s %s\n", key.c_str(), val.isEmpty() ? "cleared" : "saved");
      loadSettings();
    } else {
      Serial.println("unknown setting");
    }
  } else if (cmd == "status") {
    printStatus();
  } else if (cmd == "restart") {
    Serial.println("restarting");
    delay(200);
    ESP.restart();
  } else if (cmd.length()) {
    Serial.println("commands: wifi add/list/clear, set gh_token/gh_repo/bridge, status, restart");
  }
}

void serviceSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (serialLine.length()) handleCommand(serialLine);
      serialLine = "";
    } else if (serialLine.length() < 300) {
      serialLine += ch;
    }
  }
}

void loadSettings() {
  ghToken = prefs.getString("gh_token", "");
  ghRepo = prefs.getString("gh_repo", "joshsowalsky/sourdough-sensor-box");
  String b = prefs.getString("bridge", "192.168.1.238:8765");
  int colon = b.indexOf(':');
  bridgeHost = colon < 0 ? b : b.substring(0, colon);
  bridgePort = colon < 0 ? 8765 : b.substring(colon + 1).toInt();
}

String statusJson() {
  char buf[400];
  snprintf(buf, sizeof(buf),
           "{\"version\":%d,\"uptime_s\":%lu,\"free_heap\":%u,\"min_free_heap\":%u,"
           "\"wifi\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"distance_cm\":%.1f,\"distance_status\":%u,"
           "\"signal\":%u,\"temp_c\":%s,\"github\":\"%s\",\"last_github_ok_s\":%ld}",
           FW_VERSION, millis() / 1000, ESP.getFreeHeap(), ESP.getMinFreeHeap(),
           WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI(), distanceCm,
           distanceStatus, distanceSignal, isnan(tempC) ? "null" : String(tempC, 2).c_str(),
           githubState(), githubLastOkAge());
  return buf;
}

void printStatus() {
  Serial.println(statusJson());
}

// ---------- local web page ----------

const char LOCAL_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Sourdough Box</title>
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
async function tick(){try{const j=await (await fetch('/status',{cache:'no-store'})).json();
document.getElementById('d').textContent=j.distance_status==0?j.distance_cm.toFixed(1):'?';
document.getElementById('c').textContent=j.temp_c==null?'no probe':j.temp_c.toFixed(1);
document.getElementById('s').textContent='Live from the box, version '+j.version+(j.distance_status?' (no clear reading, status '+j.distance_status+')':'');}
catch(e){document.getElementById('s').textContent='Lost connection to the box'}}
setInterval(tick,300);tick();
</script></body></html>)HTML";

void startWeb() {
  web.on("/", [] { web.send_P(200, "text/html; charset=utf-8", LOCAL_PAGE); });
  web.on("/status", [] {
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", statusJson());
  });
  // Ask the board to look at the firmware branch now, instead of waiting for
  // its next check. Safe to leave open: it only acts on what's on GitHub.
  web.on("/check", [] {
    requestControlCheck();
    web.send(202, "text/plain", "checking GitHub for a new build or a restart request\n");
  });
  web.begin();
}

// ---------- main ----------

uint32_t offlineSince = 0;
bool markedValid = false;

// Lets the bootloader roll back to the previous build if this one never
// reports healthy (see markHealthy). Called by the Arduino core at startup.
extern "C" bool verifyRollbackLater() { return true; }

void markHealthy(const char *why) {
  if (markedValid) return;
  markedValid = true;
  esp_ota_mark_app_valid_cancel_rollback();
  if (prefs.getInt("good_build", 0) != FW_VERSION) {
    prefs.putInt("good_build", FW_VERSION);
    note("build %d confirmed healthy (%s)", FW_VERSION, why);
  }
}

// The bootloader that came with the board (TinyUF2's) doesn't roll back a bad
// update on its own, so do it here: if a build that has never been confirmed
// healthy crashes or hangs 3 times running, boot the previous build again from
// the other app slot.
RTC_NOINIT_ATTR uint32_t crashStreak;

void rollBackIfFailing(bool abnormal) {
  if (lastStageMagic != 0x5D5D5D5D) crashStreak = 0;  // power-on: RTC memory is junk
  crashStreak = abnormal ? crashStreak + 1 : 0;
  if (crashStreak < 3 || prefs.getInt("good_build", 0) == FW_VERSION) return;
  const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
  esp_app_desc_t desc;
  if (other && esp_ota_get_partition_description(other, &desc) == ESP_OK &&
      esp_ota_set_boot_partition(other) == ESP_OK) {
    crashStreak = 0;
    note("build %d failed 3 starts in a row; going back to the previous build", FW_VERSION);
    delay(500);
    ESP.restart();
  }
}

void setup() {
  Serial.setTxTimeoutMs(0);  // never wait on USB when no computer is reading
  Serial.begin(115200);
  delay(500);

  esp_task_wdt_config_t wdt = {.timeout_ms = WATCHDOG_MS, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  esp_task_wdt_add(NULL);

  esp_reset_reason_t why = esp_reset_reason();
  bool abnormal = why == ESP_RST_TASK_WDT || why == ESP_RST_INT_WDT || why == ESP_RST_WDT || why == ESP_RST_PANIC;
  if (lastStageMagic == 0x5D5D5D5D && abnormal) previousStage = lastStage;
  note("started build %d, reset reason: %s%s%s", FW_VERSION, resetReasonName(why),
       previousStage.length() ? ", was in: " : "", previousStage.c_str());
  prefs.begin("sd", false);
  rollBackIfFailing(abnormal);
  stage("setup");
  loadSettings();

#ifdef PIN_I2C_POWER
  pinMode(PIN_I2C_POWER, OUTPUT);
  digitalWrite(PIN_I2C_POWER, HIGH);
  delay(100);
#endif
  Wire.begin();
  Wire.setClock(100000);
  Wire.setTimeOut(50);  // a stuck I2C device can't hold the program up
  tof.begin();
  startTof();
  serviceProbe();

  stage("wifi connect");
  if (!connectWifi()) runSetupPortal();  // returns only by restarting
  note("wifi: %s %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  WiFi.setAutoReconnect(true);
  startWeb();
  startGithub();
}

void loop() {
  uint32_t now = millis();
  serviceSerial();
  if (!tofOk && now - tofLastTry > 30000) {
    tofLastTry = now;
    startTof();
  }
  if (tofOk) readTof();
  serviceProbe();
  serviceBridge();
  stage("web");
  web.handleClient();
  serviceGithub();

  float median = NAN;
  if (minuteDue()) {
    auto &v = minuteValid.empty() ? minuteAll : minuteValid;
    if (!v.empty()) {
      std::sort(v.begin(), v.end());
      median = v[v.size() / 2];
    }
    minuteValid.clear();
    minuteAll.clear();
    if (!isnan(median)) publishMinute(median, tempC);
  }
  if (githubOkCount() > 0) markHealthy("published to GitHub");
  if (millis() > 10UL * 60 * 1000) markHealthy("10 minutes up");

  // Off Wi-Fi for 5 minutes: restart, which rejoins or opens setup.
  if (WiFi.status() != WL_CONNECTED) {
    if (!offlineSince) offlineSince = now;
    else if (now - offlineSince > 5UL * 60 * 1000) {
      note("wifi: offline 5 minutes, restarting");
      delay(200);
      ESP.restart();
    }
  } else {
    offlineSince = 0;
  }
  stage("idle");
  delay(20);
}
