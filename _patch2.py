import io

p = r"C:\Users\TY\Documents\Doubao\YEELIGHT\yeelight_rf_bridge_v2.ino"
with io.open(p, "r", encoding="utf-8") as f:
    c = f.read()

# 1. Add WiFiClientSecure include
c = c.replace(
    "#include <ESP8266HTTPClient.h>",
    "#include <ESP8266HTTPClient.h>\n#include <WiFiClientSecure.h>"
)

# 2. handleCheckUpdate
old_check = """void handleCheckUpdate() {
  HTTPClient http;
  http.begin(UPDATE_MANIFEST_URL);
  int code = http.GET();"""

new_check = """void handleCheckUpdate() {
  HTTPClient http;
  WiFiClientSecure sec;
  WiFiClient plain;
  bool useSec = String(UPDATE_MANIFEST_URL).startsWith("https");
  if (useSec) sec.setInsecure();
  if (useSec) http.begin(sec, UPDATE_MANIFEST_URL);
  else http.begin(plain, UPDATE_MANIFEST_URL);
  int code = http.GET();"""

c = c.replace(old_check, new_check)

# 3. handleDoUpdate
old_upd = """  HTTPClient http;
  http.begin(url);
  int code = http.GET();"""

new_upd = """  HTTPClient http;
  WiFiClientSecure sec;
  WiFiClient plain;
  bool useSec = url.startsWith("https");
  if (useSec) sec.setInsecure();
  if (useSec) http.begin(sec, url);
  else http.begin(plain, url);
  int code = http.GET();"""

c = c.replace(old_upd, new_upd)

with io.open(p, "w", encoding="utf-8", newline="\n") as f:
    f.write(c)
print("done")
