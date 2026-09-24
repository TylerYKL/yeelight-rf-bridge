import io

p = r"C:\Users\TY\Documents\Doubao\YEELIGHT\yeelight_rf_bridge_v2.ino"
with io.open(p, "r", encoding="utf-8") as f:
    c = f.read()

old = '''void handleCheckUpdate() {
  HTTPClient http;
  WiFiClientSecure sec;
  WiFiClient plain;
  bool useSec = String(UPDATE_MANIFEST_URL).startsWith("https");
  if (useSec) sec.setInsecure();
  if (useSec) http.begin(sec, UPDATE_MANIFEST_URL);
  else http.begin(plain, UPDATE_MANIFEST_URL);
  int code = http.GET();
  if (code != 200) {
    server.send(200, "application/json", "{\\"error\\":\\"fetch failed\\",\\"code\\":" + String(code) + "}");
    http.end();
    return;
  }'''

new = '''void handleCheckUpdate() {
  HTTPClient http;
  static WiFiClientSecure sec;
  WiFiClient plain;
  bool useSec = String(UPDATE_MANIFEST_URL).startsWith("https");
  if (useSec) { sec.setInsecure(); sec.setTimeout(10000); }
  http.setTimeout(10000);
  if (useSec) http.begin(sec, UPDATE_MANIFEST_URL);
  else http.begin(plain, UPDATE_MANIFEST_URL);
  int code = http.GET();
  Serial.printf("checkupdate http=%d\\n", code);
  if (code != 200) {
    server.send(200, "application/json", "{\\"error\\":\\"HTTP " + String(code) + " " + http.errorToString(code) + "\\"}");
    http.end();
    return;
  }'''

c = c.replace(old, new)

with io.open(p, "w", encoding="utf-8", newline="\n") as f:
    f.write(c)
print("done")
