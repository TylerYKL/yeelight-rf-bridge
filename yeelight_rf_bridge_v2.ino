/*
 * Yeelight RF Bridge v2 - Multi-Product Database + WiFi Manager
 * ------------------------------------------------------------------
 * Features:
 *   - Multi-product RF button database (LittleFS /products.json)
 *   - WiFi credentials stored in LittleFS /wifi.json (no hardcoding)
 *   - Long-press RST 5s -> setup AP mode (configure WiFi via web)
 *   - Long-press RST 10s, then tap RST 3x within 3s -> factory reset WiFi
 *   - Setup AP: SSID "setup-yeelight-XXXXXX", IP 192.168.5.1
 *   - Auto-exit setup when WiFi saved & connected, or after 10 min
 *
 * HARDWARE MOD for RST long-press detection:
 *   You MUST add two components:
 *     1. Resistor 22k   from RST pin  to  D0 (GPIO16)
 *     2. Capacitor 100uF (16V+) from D0 to GND
 *   How it works: holding RST discharges the cap through 22k.
 *   On release it charges slowly. setup() reads D0:
 *     LOW  = cap discharged  => RST held >= ~3s
 *     HIGH = cap charged     => normal reset
 *   Time constant = 22k * 100uF = 2.2s.
 *   If you skip this mod, normal reset still works but long-press
 *   detection is disabled.
 *
 * Wiring (unchanged):
 *   WL102 TX:  +->3V3  - ->GND  DAT->D2(GPIO4)  OUT->antenna
 *   RX470 RX:  +->5V   - ->GND  DATA->D1(GPIO5)  ANT->antenna
 *
 * Dependencies:
 *   - LittleFS (ESP8266 core built-in)
 *   - ArduinoJson v6.x (Library Manager)
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Updater.h>

const int TX_PIN = 4;   // D2 -> WL102 DAT
const int RX_PIN = 5;   // D1 -> RX470 DATA
const int RST_SENSE = 13; // D7 -> RST sense (via 20k + 100uF mod)
const int LED_PIN = 14;  // D5 -> status LED

// Firmware version for OTA check
const char FW_VERSION[] = "1.0.0";
const char UPDATE_MANIFEST_URL[] = "https://raw.githubusercontent.com/USER/REPO/main/manifest.json";

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdate;

// ==================== RTC memory (survives RST) ====================
struct RtcState {
  uint32_t magic;
  uint8_t phase;    // 0=normal boot, 1=waiting for taps after long hold
  uint8_t tapCount;
};
RtcState rtc;
const uint32_t RTC_MAGIC = 0x5945454C; // "YEEL"

// ==================== Seed data ====================
const uint16_t SEED_ON[]    = {3505,2505,3502,2514,301,899,898,312,888,315,886,414,886,320,881,324,285,909,294,905,298,903,301,899,303,900,894,313,297,897,306,893,311,890,905,306,301,891,312,888,907,305,896,307,300,893,902,308,299,895,901,312,887,312,296,898,898,312,296,898,306,894,307,893,905,306,300,892,311,889,313,887,908,302,897,404,304,896,900};
const uint16_t SEED_OFF[]   = {3522,2484,3519,2496,319,882,914,295,904,300,883,426,892,303,896,307,303,891,310,890,313,887,315,886,317,882,914,296,312,881,321,879,323,878,918,292,301,893,323,876,328,875,328,872,328,871,924,286,322,872,921,289,913,290,316,879,916,292,316,879,323,891,297,890,920,290,317,876,328,872,329,872,331,869,333,970,333,867,928};
const uint16_t SEED_FRONT[] = {3511,2499,3504,2512,308,892,904,306,894,310,890,409,893,314,887,317,290,903,299,900,303,896,308,896,306,898,896,306,303,892,312,889,313,887,909,302,305,888,316,884,319,881,914,297,310,883,912,298,311,884,906,310,894,303,306,889,906,304,305,890,312,887,317,883,912,298,308,887,318,883,317,883,321,878,919,395,308,886,914};
const uint16_t SEED_REAR[]  = {3516,2493,3514,2501,313,887,910,300,901,303,898,402,896,308,892,314,293,901,302,898,305,895,307,926,253,913,907,303,305,889,314,887,916,883,912,297,311,883,318,882,915,295,312,881,321,880,916,295,296,903,907,299,902,302,305,889,906,302,306,887,316,884,319,882,913,297,311,882,321,881,322,880,914,326,256,1007,322,876,919};
const uint16_t SEED_BOTH[]  = {3506,2504,3505,2512,305,905,890,309,892,311,889,410,891,315,884,321,288,905,298,902,301,899,304,895,308,895,900,307,300,895,309,900,293,898,906,305,303,890,313,888,907,302,898,305,303,890,905,306,303,892,903,307,892,310,299,896,890,324,294,897,306,893,310,888,907,305,303,890,312,888,316,885,909,301,901,399,309,889,906};
const uint16_t SEED_BRIGHT[]= {3511,2477,3500,2553,275,911,876,345,861,330,864,450,887,283,916,292,313,882,321,924,243,948,260,919,306,914,861,348,261,911,314,907,276,924,889,311,294,913,308,861,344,859,935,276,888,357,252,933,886,305,284,929,867,342,265,911,885,341,268,924,287,904,290,890,938,273,333,860,344,860,901,348,855,324,300,1010,277,923,873};
const uint16_t SEED_COLOR[] = {3514,2494,3514,2501,310,888,909,301,896,309,890,407,895,309,890,314,292,901,303,896,308,894,306,891,314,886,910,302,301,891,311,887,318,883,909,301,306,887,318,883,317,882,913,296,905,296,312,883,912,297,309,883,913,300,303,888,908,300,308,886,313,888,315,884,912,298,308,885,317,883,915,293,904,298,311,985,317,882,909};
const uint16_t SEED_CW[]    = {3537,2488,3503,2507,322,874,923,289,916,287,910,391,916,282,914,296,313,882,310,894,323,887,298,903,300,900,907,299,307,889,312,886,320,877,922,292,315,883,312,879,336,869,922,306,882,304,320,889,890,325,283,912,892,312,296,902,884,328,285,902,310,895,305,904,895,302,307,890,308,908,891,305,877,344,264,1029,275,914,894};
const uint16_t SEED_CCW[]   = {3516,2495,3512,2503,316,885,910,300,901,304,896,403,899,308,891,319,284,911,292,924,267,922,292,903,304,897,884,341,285,890,315,887,316,884,913,298,309,885,319,881,322,880,915,298,309,898,292,903,309,888,909,309,876,315,312,882,917,290,318,878,325,877,328,872,329,872,925,285,916,288,894,325,287,898,907,401,306,904,900};

#define N_OF(a) (sizeof(a)/sizeof(a[0]))

bool inSetupMode = false;
unsigned long setupStart = 0;
const unsigned long SETUP_TIMEOUT_MS = 10UL * 60UL * 1000UL; // 10 min

// ----- Runtime config (persisted in /config.json) -----
int cfgRepeatBtn = 5;
int cfgRepeatKnob = 20;
int cfgCaptureMs = 3000;

void loadConfig() {
  DynamicJsonDocument doc(512);
  loadJson("/config.json", doc);
  cfgRepeatBtn  = doc["repeatBtn"]  | 5;
  cfgRepeatKnob = doc["repeatKnob"] | 20;
  cfgCaptureMs  = doc["captureMs"]  | 3000;
}
void saveConfig() {
  DynamicJsonDocument doc(512);
  doc["repeatBtn"]  = cfgRepeatBtn;
  doc["repeatKnob"] = cfgRepeatKnob;
  doc["captureMs"]  = cfgCaptureMs;
  saveJson("/config.json", doc);
}

// ==================== RF send ====================
void sendFrame(const uint16_t* pulses, int n) {
  noInterrupts();
  for (int i = 0; i < n; i++) {
    digitalWrite(TX_PIN, (i & 1) ? LOW : HIGH);
    delayMicroseconds(pulses[i]);
  }
  digitalWrite(TX_PIN, LOW);
  interrupts();
}

// ----- Status LED -----
bool wifiConnected = false;
unsigned long ledFlashUntil = 0;  // brief flash for RF activity
void ledFlash(int ms) { ledFlashUntil = millis() + ms; }
void ledUpdate() {
  if (millis() < ledFlashUntil) { digitalWrite(LED_PIN, HIGH); return; }
  if (inSetupMode) {
    // slow distinct blink: 500ms on, 500ms off
    digitalWrite(LED_PIN, (millis() / 500) % 2);
  } else if (!wifiConnected) {
    // fast double-blink while trying to connect
    digitalWrite(LED_PIN, (millis() / 150) % 2);
  } else {
    // solid on when connected
    digitalWrite(LED_PIN, HIGH);
  }
}

// ==================== LittleFS helpers ====================
void loadJson(const char* path, JsonDocument& doc) {
  File f = LittleFS.open(path, "r");
  if (!f) { doc.to<JsonArray>(); return; }
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) doc.to<JsonArray>();
}

void saveJson(const char* path, JsonDocument& doc) {
  File f = LittleFS.open(path, "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

// ----- product DB -----
void loadProducts(JsonDocument& doc) { loadJson("/products.json", doc); }
void saveProducts(JsonDocument& doc) { saveJson("/products.json", doc); }

void seedIfEmpty() {
  DynamicJsonDocument doc(12288);
  loadProducts(doc);
  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() > 0) return;
  JsonObject p = arr.createNestedObject();
  p["id"] = "yeelight_0033";
  p["name"] = "Yeelight YLYTD-0033";
  JsonArray btns = p.createNestedArray("buttons");
  auto addBtn = [&](const char* name, const char* label, const uint16_t* pulses, int n) {
    JsonObject b = btns.createNestedObject();
    b["name"] = name; b["label"] = label;
    JsonArray d = b.createNestedArray("data");
    for (int i = 0; i < n; i++) d.add(pulses[i]);
  };
  addBtn("on","Power On",SEED_ON,N_OF(SEED_ON));
  addBtn("off","Power Off",SEED_OFF,N_OF(SEED_OFF));
  addBtn("front","Front",SEED_FRONT,N_OF(SEED_FRONT));
  addBtn("rear","Rear",SEED_REAR,N_OF(SEED_REAR));
  addBtn("both","Both",SEED_BOTH,N_OF(SEED_BOTH));
  addBtn("bright","Brightness",SEED_BRIGHT,N_OF(SEED_BRIGHT));
  addBtn("color","Color Temp",SEED_COLOR,N_OF(SEED_COLOR));
  addBtn("cw","CW Knob",SEED_CW,N_OF(SEED_CW));
  addBtn("ccw","CCW Knob",SEED_CCW,N_OF(SEED_CCW));
  saveProducts(doc);
  Serial.println("seed data written");
}

// ----- WiFi creds -----
bool loadWifi(String& ssid, String& pass) {
  DynamicJsonDocument doc(512);
  loadJson("/wifi.json", doc);
  if (!doc.is<JsonObject>()) return false;
  ssid = doc["ssid"] | "";
  pass = doc["pass"] | "";
  return ssid.length() > 0;
}
void saveWifi(const String& ssid, const String& pass) {
  DynamicJsonDocument doc(512);
  doc["ssid"] = ssid;
  doc["pass"] = pass;
  saveJson("/wifi.json", doc);
}
void forgetWifi() {
  LittleFS.remove("/wifi.json");
}

// ==================== Continuous RF receiver ====================
#define MAX_FRAMES 10
#define MAX_PULSES 300

volatile unsigned long rfLastTime = 0;
volatile uint16_t rfPulseBuf[MAX_PULSES];
volatile int rfPulseLen = 0;

struct RfFrame { uint16_t pulses[MAX_PULSES]; int len; };
RfFrame rfRing[MAX_FRAMES];
volatile int rfRingHead = 0;
volatile int rfRingCount = 0;

void IRAM_ATTR rfIsr() {
  unsigned long now = micros();
  unsigned long dur = now - rfLastTime;
  rfLastTime = now;
  if (dur < 40) return;
  if (dur > 8000) {
    if (rfPulseLen >= 10) {
      int idx = rfRingHead;
      for (int i = 0; i < rfPulseLen; i++) rfRing[idx].pulses[i] = rfPulseBuf[i];
      rfRing[idx].len = rfPulseLen;
      rfRingHead = (rfRingHead + 1) % MAX_FRAMES;
      if (rfRingCount < MAX_FRAMES) rfRingCount++;
    }
    rfPulseLen = 0;
    return;
  }
  if (rfPulseLen < MAX_PULSES) rfPulseBuf[rfPulseLen++] = (uint16_t)dur;
}

void rfStartListening() {
  rfPulseLen = 0; rfRingHead = 0; rfRingCount = 0;
  rfLastTime = micros();
  attachInterrupt(digitalPinToInterrupt(RX_PIN), rfIsr, CHANGE);
}

String rfPopFrame(int &outLen) {
  noInterrupts();
  if (rfRingCount == 0) { outLen = 0; interrupts(); return ""; }
  int tail = (rfRingHead - rfRingCount + MAX_FRAMES) % MAX_FRAMES;
  RfFrame &f = rfRing[tail];
  outLen = f.len;
  uint16_t tmp[MAX_PULSES];
  for (int i = 0; i < f.len; i++) tmp[i] = f.pulses[i];
  rfRingCount--;
  interrupts();
  if (outLen < 10) return "";
  String s = "[";
  for (int i = 0; i < outLen; i++) { if (i) s += ","; s += tmp[i]; }
  s += "]";
  return s;
}

String captureFrameTimeout(int timeoutMs) {
  int dummy;
  do { rfPopFrame(dummy); } while (dummy > 0);
  unsigned long t0 = millis();
  while (millis() - t0 < (unsigned long)timeoutMs) {
    delay(5);
    int len;
    String s = rfPopFrame(len);
    if (len >= 10) return s;
  }
  return "";
}
String captureFrame() { return captureFrameTimeout(cfgCaptureMs); }

// ==================== HTML ====================
const char HTML_HEAD[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name=viewport content="width=device-width,initial-scale=1">
<style>
body{font-family:system-ui;background:#111;color:#eee;padding:16px;margin:0;max-width:600px;margin:0 auto}
h2{margin:0 0 12px}a{color:#0a84ff;text-decoration:none}
.card{background:#1c1c1e;border-radius:12px;padding:14px;margin:10px 0}
.g{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px;margin-top:10px}
button{padding:16px 4px;font-size:15px;border:0;border-radius:10px;background:#2a2a2e;color:#fff;cursor:pointer}
button:active{background:#4a4a52}
.big{grid-column:span 3;background:#0a84ff}
input{width:100%;padding:10px;margin:6px 0;border:0;border-radius:8px;background:#2a2a2e;color:#fff;font-size:15px;box-sizing:border-box}
.row{display:flex;gap:8px}
.ok{color:#30d158}.err{color:#ff453a}
</style></head><body>
)HTML";

String renderIndex() {
  DynamicJsonDocument doc(12288);
  loadProducts(doc);
  String h = FPSTR(HTML_HEAD);
  h += "<h2>RF Bridge</h2>";
  if (inSetupMode) {
    h += "<div class=card style='background:#332200;color:#ffd60a'>SETUP MODE - configure WiFi below</div>";
  }
  for (JsonObject p : doc.as<JsonArray>()) {
    String id = p["id"] | "";
    String name = p["name"] | id;
    int n = p["buttons"].as<JsonArray>().size();
    h += "<div class=card><div style='font-size:17px;font-weight:600'>" + name + "</div>";
    h += "<div style='color:#888;font-size:13px;margin-top:2px'>" + String(n) + " buttons</div>";
    h += "<div class=row style='margin-top:10px'>";
    h += "<a href='/p/" + id + "' style='flex:1'><button style='width:100%'>Control</button></a>";
    h += "<a href='/p/" + id + "/learn' style='flex:1'><button style='width:100%;background:#333'>Learn</button></a>";
    h += "</div></div>";
  }
  h += "<div class=card><h3 style='margin:0 0 8px'>Add New Product</h3>";
  h += "<form action='/addproduct' method=get>";
  h += "<input name=name placeholder='Product name'>";
  h += "<button class=big style='width:100%'>Create</button></form></div>";
  h += "<div class=card>";
  h += "<div style='font-size:13px;color:#888'>Firmware v"; h += FW_VERSION; h += "</div>";
  h += "<div style='margin-top:8px'><a href='/wifi'>WiFi</a> | <a href='/debug'>RF Debug</a> | <a href='/update'>Upload .bin</a></div>";
  h += "<div style='margin-top:10px'><button onclick='checkUpdate()' style='width:100%'>Check for Update</button>";
  h += "<div id=upd style='margin-top:8px;font-size:14px'></div></div>";
  h += "</div>";
  h += "<script>";
  h += "async function checkUpdate(){";
  h += "var d=document.getElementById('upd');d.textContent='Checking...';";
  h += "try{var r=await fetch('/checkupdate');var j=await r.json();";
  h += "if(j.error){d.innerHTML='Error: '+j.error;return;}";
  h += "if(j.hasUpdate){var a=document.createElement('a');a.href='/doupdate?url='+encodeURIComponent(j.url);";
  h += "a.innerHTML='<button style=background:#30d158;width:100%;margin-top:6px>Install v'+j.latest+'</button>';";
  h += "d.innerHTML='New version available! ';d.appendChild(a);}";
  h += "else{d.innerHTML='Up to date (v'+j.current+')';}";
  h += "}catch(e){d.textContent='Network error';}";
  h += "}";
  h += "</script>";
  h += "</body></html>";
  return h;
}

String renderControl(String pid) {
  DynamicJsonDocument doc(12288);
  loadProducts(doc);
  JsonObject prod;
  for (JsonObject p : doc.as<JsonArray>()) {
    if (String(p["id"]) == pid) { prod = p; break; }
  }
  if (prod.isNull()) return "<meta charset=utf-8><h2>Not found</h2>";
  String h = FPSTR(HTML_HEAD);
  h += "<a href='/'>Back</a><h2>" + String(prod["name"].as<const char*>()) + "</h2>";
  h += "<div class=row style='margin-bottom:10px'>";
  h += "<button id=delbtn style='flex:1;background:#333' onclick='toggleDel()'>Delete Mode: OFF</button>";
  h += "</div>";
  h += "<div class=g id=grid>";
  for (JsonObject b : prod["buttons"].as<JsonArray>()) {
    String bn = b["name"] | "";
    String bl = b["label"] | bn;
    h += "<button class=bnt data-bn='" + bn + "' onclick='act(\"" + bn + "\")'>" + bl + "</button>";
  }
  h += "</div>";
  h += "<div class=card style='margin-top:16px'><a href='/p/" + pid + "/learn'>+ Learn new button</a></div>";
  h += "<div class=card style='margin-top:8px'><a href='/config'>RF Config (repeats / capture timeout)</a></div>";
  h += "<script>";
  h += "let del=false;const pid='" + pid + "';"
       "function toggleDel(){del=!del;document.getElementById('delbtn').textContent='Delete Mode: '+(del?'ON':'OFF');"
       "document.getElementById('delbtn').style.background=del?'#ff453a':'#333';"
       "document.querySelectorAll('.bnt').forEach(b=>b.style.background=del?'#7a1f1f':'#2a2a2e');}"
       "async function act(n){"
       "if(del){"
       "  if(!confirm('Delete button: '+n))return;"
       "  await fetch('/p/'+pid+'/delete/'+n);location.reload();"
       "}else{await fetch('/p/'+pid+'/tx/'+n);}"
       "}";
  h += "</script>";
  h += "</body></html>";
  return h;
}

String renderLearn(String pid) {
  String h = FPSTR(HTML_HEAD);
  h += "<a href='/p/" + pid + "'>Back</a><h2>Learn</h2>";
  h += "<div class=card><form action='/p/" + pid + "/docapture' method=get>";
  h += "<input name=btn placeholder='Button name (e.g. sleep)'>";
  h += "<input name=label placeholder='Label (e.g. Sleep)'>";
  h += "<button class=big style='width:100%'>Capture (press remote now)</button>";
  h += "</form></div></body></html>";
  return h;
}

String renderWifi() {
  String ssid, pass;
  loadWifi(ssid, pass);
  String h = FPSTR(HTML_HEAD);
  h += "<a href='/'>Back</a><h2>WiFi Settings</h2>";
  h += "<div class=card><form action='/savewifi' method=get>";
  h += "<input name=ssid id=ssid placeholder='WiFi name' value='" + ssid + "'>";
  h += "<input name=pass id=pass type=password placeholder='Password' value='" + pass + "'>";
  h += "<button class=big style='width:100%'>Save & Reboot</button></form></div>";
  h += "<div class=card>";
  h += "<button style='width:100%;background:#333' onclick=\"doScan()\">Scan Networks</button>";
  h += "<div id=scanlist style='margin-top:10px'></div>";
  h += "</div>";
  h += "<div class=card style='color:#888;font-size:13px'>";
  h += "Current: " + (ssid.length() ? ssid : "(not set)") + "<br>";
  h += "In setup mode, connect to AP 'setup-yeelight-XXXXXX' and visit 192.168.5.1";
  h += "</div>";
  h += "<script>";
  h += "async function doScan(){";
  h += "document.getElementById('scanlist').innerHTML='Scanning...';"
       "const r=await fetch('/scan');const j=await r.json();"
       "let h='';"
       "j.forEach(n=>{h+='<div style=\"padding:10px;background:#2a2a2e;border-radius:8px;margin:4px 0;cursor:pointer\" onclick=\"pick(this.dataset.ssid)\" data-ssid=\"'+n.ssid.replace(/\"/g,'&quot;')+'\">'+n.ssid+' <span style=\"color:#888;font-size:12px\">'+n.rssi+'dBm</span></div>'});"
       "document.getElementById('scanlist').innerHTML=h||'No networks found';}";
  h += "function pick(s){document.getElementById('ssid').value=s;document.getElementById('pass').focus();}";
  h += "</script>";
  h += "</body></html>";
  return h;
}

// ==================== Debug / RF sniffer ====================
String renderDebug() {
  // Build product dropdown
  DynamicJsonDocument doc(12288);
  loadProducts(doc);
  String opts;
  for (JsonObject p : doc.as<JsonArray>()) {
    opts += "<option value='" + String(p["id"] | "") + "'>" + String(p["name"] | "") + "</option>";
  }

  String h = FPSTR(HTML_HEAD);
  h += "<a href='/'>Back</a><h2>RF Debug</h2>";

  h += "<div class=card><h3 style='margin:0 0 8px'>Live Sniffer</h3>";
  h += "<div class=row style='gap:6px;align-items:center;margin-bottom:8px'>";
  h += "<select id=fop onchange='renderHist()' style='flex:0 0 auto;padding:8px;border:0;border-radius:8px;background:#2a2a2e;color:#fff;font-size:14px'>";
  h += "<option value=all>All pulses</option>";
  h += "<option value=lt>Less than</option>";
  h += "<option value=gt>Greater than</option>";
  h += "<option value=eq>Equal to</option>";
  h += "<option value=ne>Not equal to</option>";
  h += "</select>";
  h += "<input id=fval type=number placeholder='count' style='width:90px;margin:0' oninput='renderHist()'>";
  h += "<span style='color:#888;font-size:12px;white-space:nowrap'>pulses</span>";
  h += "<button id=pauseBtn onclick='togglePause()' style='margin-left:auto;padding:8px 14px;font-size:13px;background:#333'>Pause</button>";
  h += "</div>";
  h += "<div id=status style='color:#888;font-size:13px'>waiting for signal...</div>";
  h += "<div id=history style='margin-top:8px'></div>";
  h += "<div id=frame style='font-family:monospace;font-size:12px;background:#000;border-radius:8px;padding:10px;margin-top:8px;max-height:180px;overflow:auto;word-break:break-all'></div>";
  h += "<div style='color:#888;font-size:12px;margin-top:6px'>Press & hold a remote button. Last 10 frames shown.</div>";
  h += "</div>";

  h += "<div class=card><h3 style='margin:0 0 8px'>Save Captured Frame</h3>";
  h += "<form action='/savedebug' method=get>";
  h += "<select name=pid style='width:100%;padding:10px;margin:6px 0;border:0;border-radius:8px;background:#2a2a2e;color:#fff'>" + opts + "</select>";
  h += "<input name=btn placeholder='Button ID (e.g. bright1)'>";
  h += "<input name=label placeholder='Label (e.g. Brightness up)'>";
  h += "<textarea name=data id=pasted style='width:100%;height:80px;padding:10px;margin:6px 0;border:0;border-radius:8px;background:#000;color:#0f0;font-family:monospace;font-size:12px' placeholder='[3500,2500,...]  pulses will auto-fill from sniffer, or paste manually'></textarea>";
  h += "<button class=big style='width:100%;background:#30d158' onclick='testTx()'>Test Transmit</button>";
  h += "<button class=big style='width:100%;margin-top:6px'>Save Button</button>";
  h += "</form></div>";

  h += "<script>";
  h += "let hist=[];let paused=false;"
       "function togglePause(){paused=!paused;const b=document.getElementById('pauseBtn');b.textContent=paused?'Resume':'Pause';b.style.background=paused?'#ff9f0a':'#333';if(paused)document.getElementById('status').innerHTML='<span style=color:#ff9f0a>Paused - not receiving</span>';else document.getElementById('status').innerHTML='waiting for signal...';}"
       "function match(f){"
       "const op=document.getElementById('fop').value;"
       "const v=parseInt(document.getElementById('fval').value)||0;"
       "if(op==='all')return true;"
       "if(op==='lt')return f.count<v;"
       "if(op==='gt')return f.count>v;"
       "if(op==='eq')return f.count===v;"
       "if(op==='ne')return f.count!==v;"
       "return true;"
       "}"
       "function renderHist(){"
       "const list=hist.filter(match);"
       "let hh='';"
       "list.forEach((f)=>{hh+='<div style=\"padding:8px;background:#2a2a2e;border-radius:8px;margin:4px 0;cursor:pointer;display:flex;justify-content:space-between\" onclick=\"pick(this)\" data-i=\"'+hist.indexOf(f)+'\"><span>'+f.count+' pulses</span><span style=color:#888>'+f.ms+'ms</span></div>'});"
       "document.getElementById('history').innerHTML=hh||'<div style=color:#888;font-size:13px;padding:8px>No frames match filter</div>';"
       "}"
       "async function sniff(){"
       "if(paused){setTimeout(sniff,300);return;}"
       "try{"
       "const r=await fetch('/sniff');"
       "const j=await r.json();"
       "if(j.count>0){"
       "  hist.unshift({count:j.count,ms:j.ms,data:j.data});"
       "  if(hist.length>10)hist.pop();"
       "  const f0=hist[0];"
       "  document.getElementById('status').innerHTML='<span style=color:#30d158>'+f0.count+' pulses, '+f0.ms+'ms</span>';"
       "  document.getElementById('frame').textContent=f0.data;"
       "  document.getElementById('pasted').value=f0.data;"
       "  renderHist();"
       "}"
       "}catch(e){}"
       "setTimeout(sniff,300);}"
       "function pick(el){let f=hist[parseInt(el.dataset.i)];document.getElementById('frame').textContent=f.data;document.getElementById('pasted').value=f.data;}"
       "async function testTx(){"
       "const data=document.getElementById('pasted').value.trim();"
       "if(data.length<10){alert('No pulse data to test');return;}"
       "const btn=event.target;btn.textContent='Sending...';btn.disabled=true;"
       "try{await fetch('/test?data='+encodeURIComponent(data));}catch(e){}"
       "btn.textContent='Test Transmit';btn.disabled=false;"
       "}"
       "sniff();";
  h += "</script>";
  h += "</body></html>";
  return h;
}

void handleSniff() {
  int len;
  String pulses = rfPopFrame(len);
  DynamicJsonDocument doc(2048);
  doc["count"] = 0;
  doc["ms"] = 0;
  doc["data"] = "[]";
  if (len >= 10) {
    DynamicJsonDocument pdoc(2048);
    deserializeJson(pdoc, pulses);
    JsonArray arr = pdoc.as<JsonArray>();
    long total = 0;
    for (int i = 0; i < arr.size(); i++) total += arr[i].as<long>();
    doc["count"] = arr.size();
    doc["ms"] = total / 1000;
    doc["data"] = pulses;
    ledFlash(80);
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleSaveDebug() {
  String pid = server.arg("pid");
  String btn = server.arg("btn");
  String label = server.arg("label");
  String data = server.arg("data");
  if (pid == "" || btn == "" || data.length() < 10) {
    server.send(400, "text/html; charset=utf-8",
      "<meta charset=utf-8><body style='background:#111;color:#fff;padding:20px'>"
      "<h3>Missing fields</h3><a href='/debug'>Back</a></body>");
    return;
  }
  DynamicJsonDocument doc(16384); loadProducts(doc);
  for (JsonObject p : doc.as<JsonArray>()) {
    if (String(p["id"]) != pid) continue;
    JsonArray btns = p["buttons"].as<JsonArray>();
    int idx = -1;
    for (int i = 0; i < btns.size(); i++) if (String(btns[i]["name"]) == btn) { idx = i; break; }
    JsonObject b = (idx >= 0) ? btns[idx] : btns.createNestedObject();
    b["name"] = btn;
    b["label"] = (label != "") ? label : btn;
    JsonArray d = b.createNestedArray("data");
    DynamicJsonDocument pdoc(2048);
    deserializeJson(pdoc, data);
    for (int i = 0; i < pdoc.as<JsonArray>().size(); i++) d.add(pdoc[i].as<int>());
    saveProducts(doc);
    server.send(200, "text/html; charset=utf-8",
      "<meta charset=utf-8><body style='background:#111;color:#fff;padding:20px'>"
      "<h3 class=ok>Saved: " + label + "</h3>"
      "<p style='color:#888'>" + String(pdoc.as<JsonArray>().size()) + " pulses stored.</p>"
      "<a href='/debug'>Continue sniffing</a> | <a href='/p/" + pid + "'>Control page</a></body>");
    return;
  }
  server.send(404, "text/plain", "product not found");
}

void handleTest() {
  String data = server.arg("data");
  if (data.length() < 10) { server.send(400, "text/plain", "no data"); return; }
  DynamicJsonDocument pdoc(2048);
  DeserializationError e = deserializeJson(pdoc, data);
  if (e) { server.send(400, "text/plain", "bad json"); return; }
  JsonArray arr = pdoc.as<JsonArray>();
  int n = arr.size();
  if (n < 4) { server.send(400, "text/plain", "too short"); return; }
  uint16_t pulses[300];
  for (int i = 0; i < n && i < 300; i++) pulses[i] = arr[i].as<int>();
  // Send 5 repeats (test mode)
  ledFlash(200);
  for (int k = 0; k < 5; k++) { sendFrame(pulses, n); delay(15); }
  server.send(200, "text/plain", "ok");
}

String renderConfig() {
  String h = FPSTR(HTML_HEAD);
  h += "<a href='/'>Back</a><h2>RF Config</h2>";
  h += "<div class=card><form action='/saveconfig' method=get>";
  h += "<label style='font-size:13px;color:#888'>Button repeats (normal keys: on/off/front...)</label>";
  h += "<input name=rptBtn type=number value='" + String(cfgRepeatBtn) + "'>";
  h += "<label style='font-size:13px;color:#888'>Knob repeats (brightness/color/knob)</label>";
  h += "<input name=rptKnob type=number value='" + String(cfgRepeatKnob) + "'>";
  h += "<label style='font-size:13px;color:#888'>Capture timeout (ms)</label>";
  h += "<input name=capMs type=number value='" + String(cfgCaptureMs) + "'>";
  h += "<button class=big style='width:100%'>Save</button>";
  h += "</form></div>";
  h += "<div class=card style='color:#888;font-size:13px'>";
  h += "Repeats: how many times the frame is re-sent when you tap a button. More = more reliable, longer delay.<br>";
  h += "Capture timeout: how long to wait for a remote frame (learn mode).";
  h += "</div>";
  h += "</body></html>";
  return h;
}

void handleSaveConfig() {
  cfgRepeatBtn  = server.arg("rptBtn").toInt();
  cfgRepeatKnob = server.arg("rptKnob").toInt();
  cfgCaptureMs  = server.arg("capMs").toInt();
  if (cfgRepeatBtn < 1) cfgRepeatBtn = 1;
  if (cfgRepeatKnob < 1) cfgRepeatKnob = 1;
  if (cfgCaptureMs < 500) cfgCaptureMs = 500;
  saveConfig();
  server.send(200, "text/html; charset=utf-8",
    "<meta charset=utf-8><body style='background:#111;color:#fff;padding:20px'>"
    "<h3 class=ok>Saved</h3><a href='/config'>Back</a></body>");
}

// ==================== Routes ====================
void handleRoot() { server.send(200, "text/html; charset=utf-8", renderIndex()); }
void handleControl() {
  String u = server.uri(); String pid = u.substring(3, u.indexOf("/", 3));
  server.send(200, "text/html; charset=utf-8", renderControl(pid));
}
void handleLearn() {
  String u = server.uri(); String pid = u.substring(3, u.indexOf("/", 3));
  server.send(200, "text/html; charset=utf-8", renderLearn(pid));
}
void handleWifi() { server.send(200, "text/html; charset=utf-8", renderWifi()); }

void handleScan() {
  // Scan nearby WiFi networks and return JSON list
  int n = WiFi.scanNetworks();
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.to<JsonArray>();
  // sort by RSSI strongest first (bubble sort small list)
  int order[20];
  for (int i = 0; i < n && i < 20; i++) order[i] = i;
  for (int i = 0; i < n && i < 20; i++)
    for (int j = i+1; j < n && j < 20; j++)
      if (WiFi.RSSI(order[j]) > WiFi.RSSI(order[i])) { int t=order[i]; order[i]=order[j]; order[j]=t; }
  for (int i = 0; i < n && i < 20; i++) {
    if (WiFi.SSID(order[i]).length() == 0) continue; // skip hidden
    JsonObject o = arr.createNestedObject();
    o["ssid"] = WiFi.SSID(order[i]);
    o["rssi"] = WiFi.RSSI(order[i]);
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleTx() {
  String u = server.uri();
  String rest = u.substring(3);
  String pid = rest.substring(0, rest.indexOf("/"));
  String btn = rest.substring(rest.indexOf("/tx/") + 4);
  DynamicJsonDocument doc(12288); loadProducts(doc);
  for (JsonObject p : doc.as<JsonArray>()) {
    if (String(p["id"]) != pid) continue;
    for (JsonObject b : p["buttons"].as<JsonArray>()) {
      if (String(b["name"]) != btn) continue;
      JsonArray d = b["data"].as<JsonArray>();
      int n = d.size(); uint16_t pulses[300];
      for (int i = 0; i < n; i++) pulses[i] = d[i];
      int reps = (btn=="bright"||btn=="color"||btn=="cw"||btn=="ccw") ? cfgRepeatKnob : cfgRepeatBtn;
      ledFlash(200);
      for (int k = 0; k < reps; k++) { sendFrame(pulses, n); delay(15); }
      server.send(200, "text/plain", "ok"); return;
    }
  }
  server.send(404, "text/plain", "nf");
}

void handleDelete() {
  String u = server.uri();
  String rest = u.substring(3);
  String pid = rest.substring(0, rest.indexOf("/"));
  String btn = rest.substring(rest.indexOf("/delete/") + 8);
  DynamicJsonDocument doc(12288); loadProducts(doc);
  for (JsonObject p : doc.as<JsonArray>()) {
    if (String(p["id"]) != pid) continue;
    JsonArray btns = p["buttons"].as<JsonArray>();
    int idx = -1;
    for (int i = 0; i < btns.size(); i++) if (String(btns[i]["name"]) == btn) { idx = i; break; }
    if (idx >= 0) {
      btns.remove(idx);
      saveProducts(doc);
    }
    server.send(200, "text/plain", "ok");
    return;
  }
  server.send(404, "text/plain", "nf");
}

void handleCapture() {
  String u = server.uri();
  String rest = u.substring(3);
  String pid = rest.substring(0, rest.indexOf("/"));
  String btn = server.arg("btn");
  String label = server.arg("label");
  if (btn == "") { server.send(400, "text/plain", "no btn"); return; }
  String pulses = captureFrame();
  if (pulses == "") {
    server.send(200, "text/html; charset=utf-8",
      "<meta charset=utf-8><body style='background:#111;color:#fff;padding:20px'>"
      "<h3>No signal</h3><a href='/p/" + pid + "/learn'>Try again</a></body>");
    return;
  }
  DynamicJsonDocument doc(16384); loadProducts(doc);
  for (JsonObject p : doc.as<JsonArray>()) {
    if (String(p["id"]) != pid) continue;
    JsonArray btns = p["buttons"].as<JsonArray>();
    int idx = -1;
    for (int i = 0; i < btns.size(); i++) if (String(btns[i]["name"]) == btn) { idx = i; break; }
    JsonObject b = (idx >= 0) ? btns[idx] : btns.createNestedObject();
    b["name"] = btn;
    b["label"] = (label != "") ? label : btn;
    JsonArray d = b.createNestedArray("data");
    DynamicJsonDocument pdoc(2048);
    deserializeJson(pdoc, pulses);
    for (int i = 0; i < pdoc.as<JsonArray>().size(); i++) d.add(pdoc[i].as<int>());
    saveProducts(doc);
    server.send(200, "text/html; charset=utf-8",
      "<meta charset=utf-8><body style='background:#111;color:#fff;padding:20px'>"
      "<h3>Saved: " + label + "</h3><a href='/p/" + pid + "'>Back</a></body>");
    return;
  }
  server.send(404, "text/plain", "nf");
}

void handleAddProduct() {
  String name = server.arg("name");
  if (name == "") { server.send(400, "text/plain", "no name"); return; }
  String pid = "p" + String((unsigned long)millis());
  DynamicJsonDocument doc(8192); loadProducts(doc);
  JsonObject p = doc.as<JsonArray>().createNestedObject();
  p["id"] = pid; p["name"] = name; p.createNestedArray("buttons");
  saveProducts(doc);
  server.sendHeader("Location", "/p/" + pid, true);
  server.send(302, "text/plain", "");
}

void handleSaveWifi() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid == "") { server.send(400, "text/plain", "no ssid"); return; }
  saveWifi(ssid, pass);
  server.send(200, "text/html; charset=utf-8",
    "<meta charset=utf-8><body style='background:#111;color:#fff;padding:20px'>"
    "<h3>Saved: " + ssid + "</h3><p>Rebooting to connect...</p></body>");
  delay(1500);
  ESP.restart();
}

// ==================== WiFi modes ====================
String macSuffix() {
  uint8_t m[6];
  WiFi.macAddress(m);
  char buf[7];
  snprintf(buf, sizeof(buf), "%02X%02X%02X", m[3], m[4], m[5]);
  return String(buf);
}

void startSetupAP() {
  inSetupMode = true;
  setupStart = millis();
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,5,1), IPAddress(192,168,5,1), IPAddress(255,255,255,0));
  String ssid = "setup-yeelight-" + macSuffix();
  WiFi.softAP(ssid.c_str());
  Serial.print("SETUP AP: "); Serial.println(ssid);
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());
}

bool startStation() {
  String ssid, pass;
  if (!loadWifi(ssid, pass)) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.print("Connecting to "); Serial.print(ssid);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Connected! IP: "); Serial.println(WiFi.localIP());
    wifiConnected = true;
    return true;
  }
  Serial.println(" fail");
  return false;
}

// ==================== RST long-press detection ====================
void readRtc() {
  ESP.rtcUserMemoryRead(0, (uint32_t*)&rtc, sizeof(rtc));
  if (rtc.magic != RTC_MAGIC) {
    rtc.magic = RTC_MAGIC;
    rtc.phase = 0;
    rtc.tapCount = 0;
  }
}
void writeRtc() {
  ESP.rtcUserMemoryWrite(0, (uint32_t*)&rtc, sizeof(rtc));
}

// Returns true if boot was triggered by a long RST hold
bool detectLongHold() {
  pinMode(RST_SENSE, INPUT);
  delayMicroseconds(50);
  return (digitalRead(RST_SENSE) == LOW);
}

// ==================== Setup ====================
void setup() {
  // MUST be first: read D0 before anything else charges the cap.
  bool longHold = detectLongHold();

  Serial.begin(115200);
  delay(200);
  Serial.println("\nRF Bridge v2 booting...");

  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, LOW);
  pinMode(RX_PIN, INPUT);
  rfStartListening();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if (!LittleFS.begin()) { Serial.println("LFS fail"); }
  loadConfig();
  seedIfEmpty();

  readRtc();

  Serial.print("phase="); Serial.print(rtc.phase);
  Serial.print(" taps="); Serial.print(rtc.tapCount);
  Serial.print(" longHold="); Serial.println(longHold);

  if (longHold) {
    // Fresh release after holding RST.
    // If we were already waiting for taps (phase==1), count this as a tap.
    if (rtc.phase == 1) {
      rtc.tapCount++;
      writeRtc();
      Serial.print("Tap #"); Serial.println(rtc.tapCount);
      if (rtc.tapCount >= 3) {
        // Factory reset: forget WiFi, clear RTC, go to setup
        Serial.println("FACTORY RESET");
        forgetWifi();
        rtc.phase = 0; rtc.tapCount = 0;
        writeRtc();
        startSetupAP();
        beginServer();
        return;
      }
      // Wait for more taps (3s), then if no more, enter setup
      delay(3000);
    } else {
      // First long hold. Set phase=1, wait 3s for potential taps.
      rtc.phase = 1;
      rtc.tapCount = 0;
      writeRtc();
      delay(3000);
      // If still phase=1 and no taps happened, this is a plain 5s hold -> setup
      // (If a tap happened, we already booted again and handled it above)
    }
    // After waiting, if we're still here, it was a plain long hold -> setup mode
    rtc.phase = 0; rtc.tapCount = 0;
    writeRtc();
    startSetupAP();
  } else {
    // Normal reset. Clear RTC tap state.
    if (rtc.phase == 1) {
      // Quick tap after a long hold but not enough taps -> ignore, reset state
      rtc.phase = 0; rtc.tapCount = 0;
      writeRtc();
    }
    // Try STA; if no creds or connect fails -> setup AP
    if (!startStation()) {
      startSetupAP();
    }
  }

  beginServer();

  // OTA + mDNS
  if (MDNS.begin("yeelight")) {
    Serial.println("mDNS: http://yeelight.local");
    MDNS.addService("http", "tcp", 80);
  }
  ArduinoOTA.setHostname("yeelight");
  ArduinoOTA.onStart([](){ Serial.println("OTA start"); });
  ArduinoOTA.onEnd([](){ Serial.println("\nOTA done"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){
    Serial.printf("OTA: %u%%\r", (progress*100)/total);
  });
  ArduinoOTA.onError([](ota_error_t e){ Serial.printf("OTA err %d\n", e); });
  ArduinoOTA.begin();
  Serial.println("OTA ready");
}

// ==================== OTA update check ====================
void handleCheckUpdate() {
  HTTPClient http;
  WiFiClientSecure sec;
  WiFiClient plain;
  bool useSec = String(UPDATE_MANIFEST_URL).startsWith("https");
  if (useSec) sec.setInsecure();
  if (useSec) http.begin(sec, UPDATE_MANIFEST_URL);
  else http.begin(plain, UPDATE_MANIFEST_URL);
  int code = http.GET();
  if (code != 200) {
    server.send(200, "application/json", "{\"error\":\"fetch failed\",\"code\":" + String(code) + "}");
    http.end();
    return;
  }
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) { server.send(200, "application/json", "{\"error\":\"bad manifest\"}"); return; }
  String latest = doc["version"] | "?";
  String url = doc["url"] | "";
  bool hasUpdate = (latest != FW_VERSION);
  String json = "{\"current\":\"" + String(FW_VERSION) + "\",\"latest\":\"" + latest + "\",\"hasUpdate\":" + (hasUpdate ? "true" : "false") + ",\"url\":\"" + url + "\"}";
  server.send(200, "application/json", json);
}

void handleDoUpdate() {
  String url = server.arg("url");
  if (url.length() == 0) { server.send(400, "text/plain", "missing url"); return; }
  server.send(200, "text/html; charset=utf-8", "<meta http-equiv=refresh content=5;url=/>Updating...<br>Do not power off.");
  delay(200);
  HTTPClient http;
  WiFiClientSecure sec;
  WiFiClient plain;
  bool useSec = url.startsWith("https");
  if (useSec) sec.setInsecure();
  if (useSec) http.begin(sec, url);
  else http.begin(plain, url);
  int code = http.GET();
  if (code != 200) { Serial.printf("update fail %d\n", code); return; }
  WiFiClient* stream = http.getStreamPtr();
  if (!Update.begin(http.getSize())) { Serial.println("Update.begin fail"); http.end(); return; }
  uint8_t buf[1024];
  int written = 0;
  while (http.connected() && (written < http.getSize())) {
    int avail = http.connected() ? stream->available() : 0;
    if (avail) {
      int n = stream->readBytes(buf, min((size_t)1024, (size_t)avail));
      written += Update.write(buf, n);
    }
  }
  if (written == http.getSize() && Update.end(true)) {
    Serial.println("Update OK, restarting...");
    delay(500);
    ESP.restart();
  } else {
    Serial.printf("Update failed at %d/%d\n", written, http.getSize());
  }
  http.end();
}
void beginServer() {
  httpUpdate.setup(&server, "/update");
  server.on("/", handleRoot);
  server.on("/addproduct", handleAddProduct);
  server.on("/wifi", handleWifi);
  server.on("/scan", handleScan);
  server.on("/savewifi", handleSaveWifi);
  server.on("/debug", [](){ server.send(200, "text/html; charset=utf-8", renderDebug()); });
  server.on("/sniff", handleSniff);
  server.on("/savedebug", handleSaveDebug);
  server.on("/test", handleTest);
  server.on("/config", [](){ server.send(200, "text/html; charset=utf-8", renderConfig()); });
  server.on("/saveconfig", handleSaveConfig);
  server.on("/checkupdate", handleCheckUpdate);
  server.on("/doupdate", handleDoUpdate);
  server.onNotFound([]() {
    String u = server.uri();
    if (u.endsWith("/learn")) handleLearn();
    else if (u.indexOf("/docapture") > 0) handleCapture();
    else if (u.indexOf("/delete/") > 0) handleDelete();
    else if (u.indexOf("/tx/") > 0) handleTx();
    else if (u.startsWith("/p/")) handleControl();
    else server.send(404, "text/plain", "404");
  });
  server.begin();
  Serial.println("web server ready");
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  ledUpdate();
  // Setup mode timeout: 10 min -> reboot
  if (inSetupMode && millis() - setupStart > SETUP_TIMEOUT_MS) {
    Serial.println("setup timeout, rebooting");
    ESP.restart();
  }
}
