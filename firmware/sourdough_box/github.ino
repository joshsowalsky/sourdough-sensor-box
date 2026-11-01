// GitHub, over HTTPS with the token saved as gh_token (fine-grained, Contents
// read/write on the one repo):
//
// * Once a minute, latest.json on the data branch: the same file the PC
//   bridge writes and live.html reads. Each update is a parentless commit
//   force-moved onto the branch, so the branch never grows a history.
// * Every 10 minutes (or right away after GET /check), manifest.json on the
//   firmware branch: {"version":N,"size":B,"sha256":"..","restart":R}.
//   A higher version than FW_VERSION means download sourdough_box.bin from
//   that branch, check its SHA-256 and install it; a changed restart number
//   means restart. tools/release.py publishes both.

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <time.h>
#include "certs.h"

#define MAX_POINTS 360  // 6 hours at one a minute: what live.html charts

WiFiClientSecure tls;
uint32_t histT[MAX_POINTS];
int32_t histD[MAX_POINTS], histC[MAX_POINTS];  // hundredths of a cm / degree
int histN = 0;
bool histLoaded = false, ghDisabled = false;
int ghFailures = 0, ghOk = 0;
uint32_t ghLastOkAt = 0, minuteStart = 0, controlDueAt = 30000;
const char *ghStateText = "starting";

const char *githubState() { return ghStateText; }
long githubLastOkAge() { return ghOk ? (long)((millis() - ghLastOkAt) / 1000) : -1; }
int githubOkCount() { return ghOk; }
void requestControlCheck() { controlDueAt = millis(); }

bool minuteDue() {
  if (millis() - minuteStart < 60000) return false;
  minuteStart = millis();
  return true;
}

void startGithub() {
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  tls.setCACert(GITHUB_ROOTS);
  // Well inside the watchdog's minute (the default handshake wait is 2 minutes).
  tls.setHandshakeTimeout(20);  // seconds
  minuteStart = millis();
  if (ghToken.isEmpty()) {
    ghStateText = "off (no token)";
    note("github: off (no gh_token saved)");
  } else {
    ghStateText = "on";
    note("github: on, repo %s", ghRepo.c_str());
  }
}

bool clockSet() { return time(nullptr) > 1700000000; }

// ---------- dates ----------

long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

uint32_t parseIso(const char *s) {  // "2026-11-01T03:36:03+00:00"
  return daysFromCivil(atoi(s), atoi(s + 5), atoi(s + 8)) * 86400L + atoi(s + 11) * 3600L +
         atoi(s + 14) * 60L + atoi(s + 17);
}

String iso(uint32_t t) {
  time_t tt = t;
  struct tm g;
  gmtime_r(&tt, &g);
  char b[32];
  strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S+00:00", &g);
  return b;
}

// ---------- requests ----------

HTTPClient http;

// One request to api.github.com. Returns the HTTP status (negative when the
// connection failed); the reply body goes to *out when given.
int ghRequest(const char *method, const String &path, const uint8_t *body, size_t len, String *out,
              const char *accept = "application/vnd.github+json") {
  esp_task_wdt_reset();
  http.setReuse(true);
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  if (!http.begin(tls, "api.github.com", 443, path, true)) return -1;
  http.addHeader("Authorization", "Bearer " + ghToken);
  http.addHeader("Accept", accept);
  http.addHeader("User-Agent", "sourdough-feather");
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");
  if (body) http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest(method, (uint8_t *)body, len);
  if (out) *out = code > 0 ? http.getString() : "";
  http.end();
  esp_task_wdt_reset();
  if (code == 401) {
    ghDisabled = true;
    ghStateText = "token rejected (401)";
    note("github: token rejected (401); save a new one with the setup page or 'set gh_token'");
  }
  return code;
}

String shaOf(const String &reply) {
  int i = reply.indexOf("\"sha\":\"");
  return i < 0 ? "" : reply.substring(i + 7, i + 47);
}

// ---------- history ----------

void addPoint(uint32_t t, int32_t d, int32_t c) {
  if (histN == MAX_POINTS) {
    memmove(histT, histT + 1, sizeof(histT[0]) * (MAX_POINTS - 1));
    memmove(histD, histD + 1, sizeof(histD[0]) * (MAX_POINTS - 1));
    memmove(histC, histC + 1, sizeof(histC[0]) * (MAX_POINTS - 1));
    histN--;
  }
  histT[histN] = t;
  histD[histN] = d;
  histC[histN] = c;
  histN++;
}

// The last 6 hours already on GitHub, so a restart doesn't blank the chart.
void loadHistory() {
  stage("github load");
  String body;
  int code = ghRequest("GET", "/repos/" + ghRepo + "/contents/latest.json?ref=data", nullptr, 0, &body,
                       "application/vnd.github.raw+json");
  if (code != 200 && code != 404) {
    note("github: couldn't load history (HTTP %d)", code);
    return;
  }
  uint32_t since = time(nullptr) - MAX_POINTS * 60;
  int i = 0;
  while ((i = body.indexOf("\"t\":\"", i)) >= 0) {
    const char *p = body.c_str() + i;
    uint32_t t = parseIso(p + 5);
    int di = body.indexOf("\"d\":", i), ci = body.indexOf("\"c\":", i);
    if (di < 0 || ci < 0) break;
    float d = atof(body.c_str() + di + 4);
    const char *cv = body.c_str() + ci + 4;
    int32_t c = strncmp(cv, "null", 4) == 0 ? NO_TEMP : lroundf(atof(cv) * 100);
    if (t >= since) addPoint(t, lroundf(d * 100), c);
    i += 5;
  }
  histLoaded = true;
  note("github: loaded %d recent points, free heap %u", histN, ESP.getFreeHeap());
}

String num(int32_t v) {
  if (v == NO_TEMP) return "null";
  char b[16];
  snprintf(b, sizeof(b), "%.2f", v / 100.0);
  return b;
}

String buildLatest() {
  String j;
  j.reserve(80 + histN * 60);
  j += "{\"updated\":\"" + iso(histT[histN - 1]) + "\",\"distance_cm\":" + num(histD[histN - 1]) +
       ",\"temp_c\":" + num(histC[histN - 1]) + ",\"history\":[";
  for (int i = 0; i < histN; i++) {
    j += "{\"t\":\"" + iso(histT[i]) + "\",\"d\":" + num(histD[i]) + ",\"c\":" + num(histC[i]) + "}";
    if (i < histN - 1) j += ",";
  }
  j += "]}";
  return j;
}

bool publish() {
  String reply;
  String blob, tree, commit;
  {
    stage("github blob");
    String json = buildLatest();
    const char *head = "{\"encoding\":\"base64\",\"content\":\"", *tail = "\"}";
    size_t b64len = 0;
    mbedtls_base64_encode(nullptr, 0, &b64len, (const uint8_t *)json.c_str(), json.length());
    size_t total = strlen(head) + b64len + strlen(tail);
    uint8_t *body = (uint8_t *)malloc(total + 1);
    if (!body) {
      note("github: out of memory building the upload (%u bytes)", total);
      return false;
    }
    memcpy(body, head, strlen(head));
    size_t olen = 0;
    mbedtls_base64_encode(body + strlen(head), b64len + 1, &olen, (const uint8_t *)json.c_str(), json.length());
    memcpy(body + strlen(head) + olen, tail, strlen(tail));
    total = strlen(head) + olen + strlen(tail);
    json = String();
    int code = ghRequest("POST", "/repos/" + ghRepo + "/git/blobs", body, total, &reply);
    free(body);
    if (code != 201) return publishFailed("blob", code);
    blob = shaOf(reply);
  }
  stage("github tree");
  String t = "{\"tree\":[{\"path\":\"latest.json\",\"mode\":\"100644\",\"type\":\"blob\",\"sha\":\"" + blob + "\"}]}";
  int code = ghRequest("POST", "/repos/" + ghRepo + "/git/trees", (const uint8_t *)t.c_str(), t.length(), &reply);
  if (code != 201) return publishFailed("tree", code);
  tree = shaOf(reply);
  stage("github commit");
  String c = "{\"message\":\"sensor data\",\"tree\":\"" + tree + "\",\"parents\":[]}";
  code = ghRequest("POST", "/repos/" + ghRepo + "/git/commits", (const uint8_t *)c.c_str(), c.length(), &reply);
  if (code != 201) return publishFailed("commit", code);
  commit = shaOf(reply);
  stage("github ref");
  String r = "{\"sha\":\"" + commit + "\",\"force\":true}";
  code = ghRequest("PATCH", "/repos/" + ghRepo + "/git/refs/heads/data", (const uint8_t *)r.c_str(), r.length(), &reply);
  if (code != 200) return publishFailed("ref", code);
  return true;
}

bool publishFailed(const char *step, int code) {
  note("github: %s step failed (HTTP %d)", step, code);
  return false;
}

void publishMinute(float distance, float temp) {
  if (ghToken.isEmpty() || ghDisabled || WiFi.status() != WL_CONNECTED) return;
  if (!clockSet()) {
    ghStateText = "waiting for the clock";
    return;
  }
  if (!histLoaded) loadHistory();
  addPoint(time(nullptr), lroundf(distance * 100), isnan(temp) ? NO_TEMP : lroundf(temp * 100));
  if (publish()) {
    ghOk++;
    ghLastOkAt = millis();
    ghFailures = 0;
    ghStateText = "ok";
    Serial.println("github: ok");
    outbox.push_back("github: ok");  // tells the bridge it needn't publish
  } else {
    ghStateText = "failing";
    if (++ghFailures >= 5) {
      note("github: 5 failures in a row, restarting");
      delay(500);
      ESP.restart();
    }
  }
}

// ---------- builds and restarts from the firmware branch ----------

// Where the value of "key" starts in a flat JSON object, or -1.
int jsonValue(const String &j, const char *key) {
  int i = j.indexOf("\"" + String(key) + "\"");
  if (i < 0) return -1;
  i += strlen(key) + 2;
  while (i < (int)j.length() && (j[i] == ' ' || j[i] == ':')) i++;
  return i;
}

long jsonNumber(const String &j, const char *key) {
  int i = jsonValue(j, key);
  return i < 0 ? -1 : j.substring(i).toInt();
}

String jsonString(const String &j, const char *key) {
  int i = jsonValue(j, key);
  if (i < 0 || j[i] != '"') return "";
  return j.substring(i + 1, j.indexOf('"', i + 1));
}

bool installBuild(long version, long size, const String &want) {
  note("update: downloading build %ld (%ld bytes)", version, size);
  stage("update download");
  http.setReuse(false);
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(tls, "api.github.com", 443, "/repos/" + ghRepo + "/contents/sourdough_box.bin?ref=firmware", true))
    return false;
  http.addHeader("Authorization", "Bearer " + ghToken);
  http.addHeader("Accept", "application/vnd.github.raw+json");
  http.addHeader("User-Agent", "sourdough-feather");
  int code = http.GET();
  if (code != 200) {
    note("update: download failed (HTTP %d)", code);
    http.end();
    return false;
  }
  if (!Update.begin(size)) {
    note("update: no room for it (%s)", Update.errorString());
    http.end();
    return false;
  }
  WiFiClient *s = http.getStreamPtr();
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  static uint8_t buf[2048];
  long got = 0;
  uint32_t lastData = millis();
  while (got < size && millis() - lastData < 30000) {
    esp_task_wdt_reset();
    int n = s->read(buf, min((long)sizeof(buf), size - got));
    if (n <= 0) {
      delay(5);
      continue;
    }
    lastData = millis();
    mbedtls_sha256_update(&sha, buf, n);
    if (Update.write(buf, n) != (size_t)n) break;
    got += n;
  }
  http.end();
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);
  if (got != size || want != hex) {
    note("update: download incomplete or corrupted (%ld of %ld bytes); keeping build %d", got, size, FW_VERSION);
    Update.abort();
    return false;
  }
  if (!Update.end(true)) {
    note("update: install failed (%s)", Update.errorString());
    return false;
  }
  note("update: build %ld installed, restarting into it", version);
  return true;
}

void serviceGithub() {
  if ((long)(millis() - controlDueAt) < 0) return;
  controlDueAt = millis() + 10UL * 60 * 1000;
  if (ghToken.isEmpty() || ghDisabled || WiFi.status() != WL_CONNECTED) return;
  stage("github control");
  String m;
  int code = ghRequest("GET", "/repos/" + ghRepo + "/contents/manifest.json?ref=firmware", nullptr, 0, &m,
                       "application/vnd.github.raw+json");
  if (code != 200) {
    if (code != 404) note("update: couldn't read the firmware branch (HTTP %d)", code);
    return;
  }
  long restart = jsonNumber(m, "restart");
  if (restart >= 0) {
    if (!prefs.isKey("restart_seen")) {
      prefs.putLong("restart_seen", restart);
    } else if (prefs.getLong("restart_seen") != restart) {
      prefs.putLong("restart_seen", restart);
      note("restart requested from GitHub");
      delay(1000);
      ESP.restart();
    }
  }
  long version = jsonNumber(m, "version");
  if (version > FW_VERSION && installBuild(version, jsonNumber(m, "size"), jsonString(m, "sha256"))) {
    delay(1000);
    ESP.restart();
  }
}
