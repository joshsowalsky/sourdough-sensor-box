// Wi-Fi: up to 5 saved networks in flash. If none of them is in reach, the
// board opens its own open network, "Sourdough-Setup", with a page at
// http://192.168.4.1 (phones usually pop it up on their own) where a phone can
// add a network and, optionally, a new GitHub token.

#include <DNSServer.h>

#define MAX_SAVED 5

int savedCount() { return prefs.getInt("nets", 0); }
String savedSsid(int i) { return prefs.getString(("s" + String(i)).c_str(), ""); }
String savedPass(int i) { return prefs.getString(("p" + String(i)).c_str(), ""); }

void clearNetworks() { prefs.putInt("nets", 0); }

// Newest first; an existing entry for the same network is replaced.
void saveNetwork(const String &ssid, const String &pass) {
  std::vector<std::pair<String, String>> nets = {{ssid, pass}};
  for (int i = 0; i < savedCount() && nets.size() < MAX_SAVED; i++)
    if (savedSsid(i) != ssid) nets.push_back({savedSsid(i), savedPass(i)});
  for (size_t i = 0; i < nets.size(); i++) {
    prefs.putString(("s" + String(i)).c_str(), nets[i].first);
    prefs.putString(("p" + String(i)).c_str(), nets[i].second);
  }
  prefs.putInt("nets", nets.size());
}

bool tryNetwork(const String &ssid, const String &pass) {
  note("wifi: trying %s", ssid.c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());
  for (int i = 0; i < 150 && WiFi.status() != WL_CONNECTED; i++) {
    esp_task_wdt_reset();
    serviceSerial();
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect();
  return false;
}

bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("sourdough-box");
  if (!savedCount()) {
    note("wifi: no networks saved");
    return false;
  }
  int found = WiFi.scanNetworks();
  // Saved networks that are in range, strongest first (scan results are sorted).
  for (int s = 0; s < found; s++)
    for (int i = 0; i < savedCount(); i++)
      if (WiFi.SSID(s) == savedSsid(i) && tryNetwork(savedSsid(i), savedPass(i))) return true;
  // A hidden network doesn't show in a scan; try them all once anyway.
  for (int i = 0; i < savedCount(); i++)
    if (tryNetwork(savedSsid(i), savedPass(i))) return true;
  note("wifi: none of the saved networks answered");
  return false;
}

String htmlEscape(const String &s) {
  String o;
  for (char ch : s) {
    if (ch == '&') o += "&amp;";
    else if (ch == '<') o += "&lt;";
    else if (ch == '>') o += "&gt;";
    else if (ch == '"') o += "&quot;";
    else o += ch;
  }
  return o;
}

const char SETUP_HEAD[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Sourdough Box Setup</title>
<style>:root{--bg:#f4f3ee;--fg:#1f2421;--m:#66706a;--a:#b4691f;--f:#fff}
@media (prefers-color-scheme: dark){:root{--bg:#161a18;--fg:#ebeee9;--m:#9ba59f;--a:#e2994d;--f:#232825}}
body{margin:0;background:var(--bg);color:var(--fg);font:16px system-ui,sans-serif;padding:16px;max-width:30rem}
label{display:block;margin-top:14px;color:var(--m);font-size:.9rem}
input,select{width:100%;box-sizing:border-box;font:inherit;padding:10px;margin-top:4px;background:var(--f);color:var(--fg);border:1px solid var(--m);border-radius:6px}
button{margin-top:20px;font:inherit;padding:12px 18px;background:var(--a);color:#fff;border:0;border-radius:6px;width:100%}
p{color:var(--m)}</style></head><body><h2>Sourdough box setup</h2>)HTML";

void runSetupPortal() {
  int found = WiFi.scanNetworks();
  String options;
  for (int s = 0; s < found; s++) {
    String name = htmlEscape(WiFi.SSID(s));
    if (name.length() && options.indexOf("\"" + name + "\"") < 0)
      options += "<option value=\"" + name + "\">" + name + "</option>";
  }
  options += "<option value=\"\">Other (type it below)</option>";

  WiFi.mode(WIFI_AP);
  WiFi.softAP("Sourdough-Setup");
  DNSServer dns;
  dns.start(53, "*", WiFi.softAPIP());  // every name leads here, so phones show the page
  note("setup: join the Sourdough-Setup Wi-Fi and open http://192.168.4.1");

  bool saved = false;
  web.on("/", [&options] {
    String page = FPSTR(SETUP_HEAD);
    page += "<p>Pick the Wi-Fi this box should use. It keeps up to 5 and joins whichever is in range.</p>"
            "<form method=\"post\" action=\"/save\"><label>Network</label><select name=\"ssid\">" + options +
            "</select><label>Or type the network name</label><input name=\"other\" autocapitalize=\"none\">"
            "<label>Password</label><input name=\"pass\" type=\"password\">"
            "<label>GitHub token (only if replacing it; leave blank to keep the current one)</label>"
            "<input name=\"token\" type=\"password\" autocapitalize=\"none\">"
            "<button>Save and restart</button></form></body></html>";
    web.send(200, "text/html; charset=utf-8", page);
  });
  web.on("/save", HTTP_POST, [&saved] {
    String ssid = web.arg("other").length() ? web.arg("other") : web.arg("ssid");
    if (!ssid.length()) {
      web.send(400, "text/plain", "Pick or type a network name.");
      return;
    }
    saveNetwork(ssid, web.arg("pass"));
    if (web.arg("token").length()) prefs.putString("gh_token", web.arg("token"));
    String page = FPSTR(SETUP_HEAD);
    page += "<p>Saved " + htmlEscape(ssid) + ". The box is restarting and will join it. "
            "You can reconnect this phone to your usual Wi-Fi.</p></body></html>";
    web.send(200, "text/html; charset=utf-8", page);
    saved = true;
  });
  web.onNotFound([] {
    web.sendHeader("Location", "http://192.168.4.1/");
    web.send(302, "text/plain", "");
  });
  web.begin();

  uint32_t start = millis();
  while (!saved && millis() - start < 10UL * 60 * 1000) {  // no answer in 10 min: retry the saved ones
    esp_task_wdt_reset();
    dns.processNextRequest();
    web.handleClient();
    serviceSerial();
    delay(5);
  }
  delay(1500);  // let the "Saved" page reach the phone
  ESP.restart();
}
